/*
 * HTTP/0.9 over QMux interop endpoint for quicly (h2o/quicly#662, kazuho/qmux-01).
 *
 * TLS 1.3 over TCP with ALPN "hq-qmux", then quicly QMux stream multiplexing,
 * then HTTP/0.9 (GET <path>\r\n).
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "picotls.h"
#include "picotls/openssl.h"
#include "quicly.h"
#include "quicly/defaults.h"
#include "quicly/streambuf.h"
#include "../deps/picotls/t/util.h"

#define ALPN_HQ_QMUX "hq-qmux"
#define DOWNLOAD_DIR_DEFAULT "/downloads"
#define WWW_DIR_DEFAULT "/www"

struct st_stream_data_t {
    quicly_streambuf_t streambuf;
    FILE *download_fp; /* client */
    int responded;     /* server */
};

struct st_conn_ctx_t {
    int is_server;
    int transfer_ok;
    size_t num_paths;
    size_t paths_started;
    size_t responses_done;
    char **paths; /* client: URL paths like "/file" */
    const char *www_dir;
    const char *download_dir;
};

static ptls_context_t tlsctx;
static quicly_context_t qctx;
static ptls_iovec_t alpn_hq = {(uint8_t *)ALPN_HQ_QMUX, sizeof(ALPN_HQ_QMUX) - 1};
static ptls_openssl_sign_certificate_t sign_certificate;
static int socket_writable = 1;

static int on_client_hello_cb(ptls_on_client_hello_t *self, ptls_t *tls, ptls_on_client_hello_parameters_t *params)
{
    size_t i;
    (void)self;
    for (i = 0; i != params->negotiated_protocols.count; ++i) {
        if (params->negotiated_protocols.list[i].len == alpn_hq.len &&
            memcmp(params->negotiated_protocols.list[i].base, alpn_hq.base, alpn_hq.len) == 0)
            return ptls_set_negotiated_protocol(tls, (const char *)alpn_hq.base, alpn_hq.len);
    }
    return PTLS_ALERT_NO_APPLICATION_PROTOCOL;
}

static ptls_on_client_hello_t on_client_hello = {on_client_hello_cb};

static int qmux_writable_cb(quicly_qmux_writable_t *self, quicly_conn_t *conn)
{
    (void)self;
    (void)conn;
    return socket_writable;
}

static quicly_qmux_writable_t qmux_writable = {qmux_writable_cb};

static void on_stop_sending(quicly_stream_t *stream, quicly_error_t err)
{
    fprintf(stderr, "STOP_SENDING: %" PRIu64 "\n", QUICLY_ERROR_GET_ERROR_CODE(err));
    quicly_close(stream->conn, QUICLY_ERROR_FROM_APPLICATION_ERROR_CODE(0), "stop_sending");
}

static void on_receive_reset(quicly_stream_t *stream, quicly_error_t err)
{
    fprintf(stderr, "RESET_STREAM: %" PRIu64 "\n", QUICLY_ERROR_GET_ERROR_CODE(err));
    quicly_close(stream->conn, QUICLY_ERROR_FROM_APPLICATION_ERROR_CODE(0), "reset_stream");
}

static int send_file_response(quicly_stream_t *stream, const char *www_dir, const char *path)
{
    char filepath[1024];
    const char *rel = path;
    char buf[8192];
    FILE *fp;
    size_t n;

    while (*rel == '/')
        ++rel;
    if (*rel == '\0')
        rel = "index.html";
    if (strstr(rel, "..") != NULL) {
        static const char msg[] = "not found\n";
        quicly_streambuf_egress_write(stream, msg, sizeof(msg) - 1);
        quicly_streambuf_egress_shutdown(stream);
        return 0;
    }

    snprintf(filepath, sizeof(filepath), "%s/%s", www_dir, rel);
    fp = fopen(filepath, "rb");
    if (fp == NULL) {
        static const char msg[] = "not found\n";
        fprintf(stderr, "file not found: %s\n", filepath);
        quicly_streambuf_egress_write(stream, msg, sizeof(msg) - 1);
        quicly_streambuf_egress_shutdown(stream);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (quicly_streambuf_egress_write(stream, buf, n) != 0) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    quicly_streambuf_egress_shutdown(stream);
    return 0;
}

static void on_receive(quicly_stream_t *stream, size_t off, const void *src, size_t len)
{
    struct st_stream_data_t *sd = stream->data;
    struct st_conn_ctx_t *ctx = *quicly_get_data(stream->conn);
    ptls_iovec_t input;

    if (quicly_streambuf_ingress_receive(stream, off, src, len) != 0)
        return;
    input = quicly_streambuf_ingress_get(stream);
    if (input.base == NULL)
        return;

    if (ctx->is_server) {
        if (sd->responded)
            return;
        if (quicly_recvstate_transfer_complete(&stream->recvstate)) {
            char *req = malloc(input.len + 1);
            char method[16], path[512];
            if (req == NULL)
                return;
            memcpy(req, input.base, input.len);
            req[input.len] = '\0';
            {
                char *eol = strstr(req, "\r\n");
                if (eol != NULL)
                    *eol = '\0';
            }
            if (sscanf(req, "%15s %511s", method, path) == 2 && strcmp(method, "GET") == 0) {
                sd->responded = 1;
                send_file_response(stream, ctx->www_dir, path);
            } else {
                static const char msg[] = "bad request\n";
                quicly_streambuf_egress_write(stream, msg, sizeof(msg) - 1);
                quicly_streambuf_egress_shutdown(stream);
                sd->responded = 1;
            }
            free(req);
            quicly_streambuf_ingress_shift(stream, input.len);
        }
    } else {
        if (sd->download_fp != NULL && input.len > 0)
            fwrite(input.base, 1, input.len, sd->download_fp);
        quicly_streambuf_ingress_shift(stream, input.len);
        if (quicly_recvstate_transfer_complete(&stream->recvstate)) {
            if (sd->download_fp != NULL) {
                fclose(sd->download_fp);
                sd->download_fp = NULL;
            }
            ctx->responses_done++;
            if (ctx->responses_done >= ctx->num_paths)
                ctx->transfer_ok = 1;
        }
    }
}

static void on_stream_destroy(quicly_stream_t *stream, quicly_error_t err)
{
    struct st_stream_data_t *sd = stream->data;
    (void)err;
    if (sd != NULL && sd->download_fp != NULL) {
        fclose(sd->download_fp);
        sd->download_fp = NULL;
    }
    quicly_streambuf_destroy(stream, err);
}

static quicly_error_t on_stream_open(quicly_stream_open_t *self, quicly_stream_t *stream)
{
    static const quicly_stream_callbacks_t stream_callbacks = {
        on_stream_destroy, quicly_streambuf_egress_shift, quicly_streambuf_egress_emit, on_stop_sending, on_receive,
        on_receive_reset};
    quicly_error_t ret;
    (void)self;
    if ((ret = quicly_streambuf_create(stream, sizeof(struct st_stream_data_t))) != 0)
        return ret;
    stream->callbacks = &stream_callbacks;
    return 0;
}

static quicly_stream_open_t stream_open = {on_stream_open};

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int create_listen_socket(const char *host, const char *port)
{
    struct addrinfo hints = {0}, *res, *rp;
    int fd = -1, on = 1, ret;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG | AI_NUMERICSERV;
    if ((ret = getaddrinfo(host, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        if ((fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1)
            continue;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd == -1 || listen(fd, 16) != 0) {
        if (fd != -1)
            close(fd);
        return -1;
    }
    return fd;
}

static int connect_tcp(const char *host, const char *port)
{
    struct addrinfo hints = {0}, *res, *rp;
    int fd = -1, ret;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
    if ((ret = getaddrinfo(host, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        if ((fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1)
            continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int flush_write_buf(int fd, ptls_buffer_t *buf)
{
    while (buf->off != 0) {
        ssize_t n = write(fd, buf->base, buf->off);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                socket_writable = 0;
                return 0;
            }
            return -1;
        }
        if (n == 0)
            return -1;
        memmove(buf->base, buf->base + n, buf->off - (size_t)n);
        buf->off -= (size_t)n;
    }
    socket_writable = 1;
    return 0;
}

static int qmux_flush_to_tls(quicly_conn_t *conn, ptls_t *tls, ptls_buffer_t *encbuf)
{
    uint8_t qbuf[16384];
    size_t qlen;
    quicly_error_t ret;

    for (;;) {
        qlen = sizeof(qbuf);
        ret = quicly_qmux_send(conn, qbuf, &qlen);
        if (ret == QUICLY_ERROR_FREE_CONNECTION)
            return 1; /* closed */
        if (ret != 0) {
            fprintf(stderr, "quicly_qmux_send: %" PRId64 "\n", (int64_t)ret);
            return -1;
        }
        if (qlen == 0)
            return 0;
        if (ptls_send(tls, encbuf, qbuf, qlen) != 0)
            return -1;
    }
}

static int client_open_requests(quicly_conn_t *conn, struct st_conn_ctx_t *ctx)
{
    while (ctx->paths_started < ctx->num_paths) {
        quicly_stream_t *stream;
        struct st_stream_data_t *sd;
        char req[600], outfile[1024];
        const char *url = ctx->paths[ctx->paths_started];
        const char *path;
        const char *slash;
        int n;
        quicly_error_t ret;

        /* accept full URLs or bare paths */
        path = url;
        if (strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0) {
            const char *p = strstr(url, "://");
            p += 3;
            path = strchr(p, '/');
            if (path == NULL)
                path = "/";
        }

        if ((ret = quicly_open_stream(conn, &stream, 0)) != 0) {
            fprintf(stderr, "quicly_open_stream: %" PRId64 "\n", (int64_t)ret);
            return -1;
        }
        sd = stream->data;
        slash = strrchr(path, '/');
        snprintf(outfile, sizeof(outfile), "%s/%s", ctx->download_dir, slash != NULL ? slash + 1 : path);
        sd->download_fp = fopen(outfile, "wb");
        if (sd->download_fp == NULL) {
            perror(outfile);
            return -1;
        }
        n = snprintf(req, sizeof(req), "GET %s\r\n", path);
        if (quicly_streambuf_egress_write(stream, req, (size_t)n) != 0)
            return -1;
        quicly_streambuf_egress_shutdown(stream);
        fprintf(stderr, "requested %s -> %s\n", path, outfile);
        ctx->paths_started++;
    }
    return 0;
}

static int qmux_feed_pending(quicly_conn_t *conn, ptls_buffer_t *pending)
{
    while (pending->off > 0) {
        size_t consumed = pending->off;
        quicly_error_t qret = quicly_qmux_receive(conn, pending->base, &consumed);
        if (qret == QUICLY_ERROR_IS_CLOSING)
            return 1;
        if (qret != 0) {
            fprintf(stderr, "quicly_qmux_receive: %" PRId64 "\n", (int64_t)qret);
            return -1;
        }
        if (consumed == 0)
            break; /* incomplete record; wait for more bytes */
        if (consumed < pending->off)
            memmove(pending->base, pending->base + consumed, pending->off - consumed);
        pending->off -= consumed;
    }
    return 0;
}

static int run_qmux_session(int fd, ptls_t *tls, quicly_conn_t *conn, struct st_conn_ctx_t *ctx, ptls_buffer_t *early_appdata)
{
    ptls_buffer_t encbuf, qpending;
    uint8_t rbuf[16384];
    int ret = 0;

    ptls_buffer_init(&encbuf, "", 0);
    ptls_buffer_init(&qpending, "", 0);
    set_nonblock(fd);
    *quicly_get_data(conn) = ctx;

    if (early_appdata != NULL && early_appdata->off != 0) {
        if (ptls_buffer_reserve(&qpending, early_appdata->off) != 0) {
            ret = -1;
            goto Exit;
        }
        memcpy(qpending.base, early_appdata->base, early_appdata->off);
        qpending.off = early_appdata->off;
        early_appdata->off = 0;
        {
            int feed = qmux_feed_pending(conn, &qpending);
            if (feed < 0) {
                ret = -1;
                goto Exit;
            }
            if (feed > 0)
                goto closed;
        }
    }

    /* server: send transport parameters immediately */
    if (ctx->is_server) {
        if (qmux_flush_to_tls(conn, tls, &encbuf) < 0 || flush_write_buf(fd, &encbuf) != 0) {
            ret = -1;
            goto Exit;
        }
    }

    while (quicly_get_state(conn) < QUICLY_STATE_CLOSING) {
        fd_set rfds, wfds;
        struct timeval tv;
        int64_t now, timeout_at, delta;
        int nfds;

        if (!ctx->is_server && ctx->paths_started < ctx->num_paths &&
            quicly_get_state(conn) == QUICLY_STATE_CONNECTED) {
            if (client_open_requests(conn, ctx) != 0) {
                ret = -1;
                goto Exit;
            }
        }

        {
            int closed = qmux_flush_to_tls(conn, tls, &encbuf);
            if (closed < 0) {
                ret = -1;
                goto Exit;
            }
            if (flush_write_buf(fd, &encbuf) != 0) {
                ret = -1;
                goto Exit;
            }
            if (closed > 0)
                break;
        }

        if (ctx->transfer_ok) {
            quicly_close(conn, 0, "");
            qmux_flush_to_tls(conn, tls, &encbuf);
            flush_write_buf(fd, &encbuf);
            break;
        }

        now = qctx.now->cb(qctx.now);
        timeout_at = quicly_get_first_timeout(conn);
        delta = timeout_at > now ? timeout_at - now : 0;
        if (delta > 1000)
            delta = 1000;
        tv.tv_sec = (time_t)(delta / 1000);
        tv.tv_usec = (suseconds_t)((delta % 1000) * 1000);

        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(fd, &rfds);
        if (encbuf.off != 0)
            FD_SET(fd, &wfds);
        nfds = select(fd + 1, &rfds, &wfds, NULL, &tv);
        if (nfds < 0) {
            if (errno == EINTR)
                continue;
            ret = -1;
            goto Exit;
        }

        if (FD_ISSET(fd, &wfds)) {
            socket_writable = 1;
            if (flush_write_buf(fd, &encbuf) != 0) {
                ret = -1;
                goto Exit;
            }
        }

        if (FD_ISSET(fd, &rfds)) {
            ssize_t n = read(fd, rbuf, sizeof(rbuf));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                ret = -1;
                goto Exit;
            }
            if (n == 0)
                break;

            size_t off = 0;
            while (off < (size_t)n) {
                ptls_buffer_t decryptbuf;
                size_t consumed = (size_t)n - off;
                ptls_buffer_init(&decryptbuf, "", 0);
                if ((ret = ptls_receive(tls, &decryptbuf, rbuf + off, &consumed)) != 0) {
                    fprintf(stderr, "ptls_receive: %d\n", ret);
                    ptls_buffer_dispose(&decryptbuf);
                    ret = -1;
                    goto Exit;
                }
                off += consumed;
                if (decryptbuf.off != 0) {
                    if (ptls_buffer_reserve(&qpending, decryptbuf.off) != 0) {
                        ptls_buffer_dispose(&decryptbuf);
                        ret = -1;
                        goto Exit;
                    }
                    memcpy(qpending.base + qpending.off, decryptbuf.base, decryptbuf.off);
                    qpending.off += decryptbuf.off;
                }
                ptls_buffer_dispose(&decryptbuf);

                {
                    int feed = qmux_feed_pending(conn, &qpending);
                    if (feed < 0) {
                        ret = -1;
                        goto Exit;
                    }
                    if (feed > 0)
                        goto closed;
                }
            }
            ret = 0;
        }
    }

    if (!ctx->is_server)
        ret = ctx->transfer_ok ? 0 : -1;
    goto Exit;

closed:
    /* peer closed; success if client finished downloads (or we are the server) */
    ret = (ctx->is_server || ctx->transfer_ok) ? 0 : -1;

Exit:
    ptls_buffer_dispose(&encbuf);
    ptls_buffer_dispose(&qpending);
    return ret;
}

/* Returns 0 on success. Any TLS application data already read after the
 * handshake is appended to early_appdata for the QMux session to consume. */
static int tls_handshake_loop(int fd, ptls_t *tls, ptls_handshake_properties_t *hsprop, int is_server,
                              ptls_buffer_t *early_appdata)
{
    ptls_buffer_t sendbuf;
    uint8_t rbuf[16384];
    int ret = 0;

    ptls_buffer_init(&sendbuf, "", 0);

    if (!is_server) {
        ret = ptls_handshake(tls, &sendbuf, NULL, NULL, hsprop);
        if (ret != 0 && ret != PTLS_ERROR_IN_PROGRESS)
            goto Exit;
        if (flush_write_buf(fd, &sendbuf) != 0) {
            ret = -1;
            goto Exit;
        }
    }

    while (!ptls_handshake_is_complete(tls)) {
        fd_set rfds;
        struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
            ret = -1;
            goto Exit;
        }
        ssize_t n = read(fd, rbuf, sizeof(rbuf));
        if (n <= 0) {
            ret = -1;
            goto Exit;
        }
        size_t off = 0;
        while (off < (size_t)n) {
            size_t consumed = (size_t)n - off;
            if (ptls_handshake_is_complete(tls)) {
                /* Remaining bytes are encrypted 1-RTT; decrypt into early_appdata. */
                ptls_buffer_t decryptbuf;
                ptls_buffer_init(&decryptbuf, "", 0);
                if ((ret = ptls_receive(tls, &decryptbuf, rbuf + off, &consumed)) != 0) {
                    ptls_buffer_dispose(&decryptbuf);
                    goto Exit;
                }
                off += consumed;
                if (decryptbuf.off != 0) {
                    if (ptls_buffer_reserve(early_appdata, decryptbuf.off) != 0) {
                        ptls_buffer_dispose(&decryptbuf);
                        ret = -1;
                        goto Exit;
                    }
                    memcpy(early_appdata->base + early_appdata->off, decryptbuf.base, decryptbuf.off);
                    early_appdata->off += decryptbuf.off;
                }
                ptls_buffer_dispose(&decryptbuf);
                ret = 0;
                continue;
            }
            ret = ptls_handshake(tls, &sendbuf, rbuf + off, &consumed, hsprop);
            if (ret != 0 && ret != PTLS_ERROR_IN_PROGRESS)
                goto Exit;
            off += consumed;
            if (flush_write_buf(fd, &sendbuf) != 0) {
                ret = -1;
                goto Exit;
            }
            if (ret == PTLS_ERROR_IN_PROGRESS)
                ret = 0;
        }
    }

    {
        const char *alpn = ptls_get_negotiated_protocol(tls);
        if (alpn == NULL || strcmp(alpn, ALPN_HQ_QMUX) != 0) {
            fprintf(stderr, "ALPN mismatch: %s\n", alpn ? alpn : "(none)");
            ret = -1;
            goto Exit;
        }
    }

Exit:
    ptls_buffer_dispose(&sendbuf);
    return ret;
}

static void init_contexts(int small_windows)
{
    qctx = quicly_spec_context;
    qctx.tls = &tlsctx;
    qctx.stream_open = &stream_open;
    qctx.qmux_writable = &qmux_writable;
    qctx.transport_params.max_idle_timeout = 60 * 1000;
    qctx.transport_params.max_streams_bidi = 100;
    if (small_windows) {
        /* Match the suite's transfer windows so transfers exercise both
         * connection-level and per-stream flow-control updates. */
        qctx.transport_params.max_stream_data.bidi_local = 64 * 1024;
        qctx.transport_params.max_stream_data.bidi_remote = 64 * 1024;
        qctx.transport_params.max_data = 128 * 1024;
    }

    memset(&tlsctx, 0, sizeof(tlsctx));
    tlsctx.random_bytes = ptls_openssl_random_bytes;
    tlsctx.get_time = &ptls_get_time;
    tlsctx.key_exchanges = ptls_openssl_key_exchanges;
    tlsctx.cipher_suites = ptls_openssl_cipher_suites;
    tlsctx.require_dhe_on_psk = 1;
}

static void usage(const char *cmd)
{
    printf("Usage:\n"
           "  %s server [-c cert -k key] [-d www-dir] [host] [port]\n"
           "  %s client [-o download-dir] host port path [path...]\n",
           cmd, cmd);
}

int main(int argc, char **argv)
{
    const char *cert_file = "/certs/cert.pem";
    const char *key_file = "/certs/priv.key";
    const char *www_dir = WWW_DIR_DEFAULT;
    const char *download_dir = DOWNLOAD_DIR_DEFAULT;
    int ch;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    optind = 1;
    /* role is argv[1]; options follow */
    if (strcmp(argv[1], "server") == 0) {
        argc--;
        argv++;
        while ((ch = getopt(argc, argv, "c:k:d:h")) != -1) {
            switch (ch) {
            case 'c':
                cert_file = optarg;
                break;
            case 'k':
                key_file = optarg;
                break;
            case 'd':
                www_dir = optarg;
                break;
            default:
                usage("qmux_interop");
                return 1;
            }
        }
        const char *host = optind < argc ? argv[optind] : "0.0.0.0";
        const char *port = optind + 1 < argc ? argv[optind + 1] : "443";

        init_contexts(0);
        load_certificate_chain(&tlsctx, cert_file);
        load_private_key(&tlsctx, key_file);
        (void)sign_certificate;
        tlsctx.on_client_hello = &on_client_hello;

        int listenfd = create_listen_socket(host, port);
        if (listenfd < 0) {
            perror("listen");
            return 1;
        }
        fprintf(stderr, "quicly-qmux server on %s:%s www=%s\n", host, port, www_dir);

        for (;;) {
            struct sockaddr_storage ss;
            socklen_t sslen = sizeof(ss);
            int connfd = accept(listenfd, (struct sockaddr *)&ss, &sslen);
            int on = 1;
            ptls_t *tls;
            quicly_conn_t *conn;
            ptls_handshake_properties_t hsprop = {{0}};
            struct st_conn_ctx_t ctx = {0};
            int ret;

            if (connfd < 0) {
                perror("accept");
                continue;
            }
            setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

            tls = ptls_new(&tlsctx, 1);
            if (tls == NULL) {
                close(connfd);
                continue;
            }

            ptls_buffer_t early_appdata;
            ptls_buffer_init(&early_appdata, "", 0);
            ret = tls_handshake_loop(connfd, tls, &hsprop, 1, &early_appdata);
            if (ret != 0) {
                fprintf(stderr, "TLS handshake failed\n");
                ptls_buffer_dispose(&early_appdata);
                ptls_free(tls);
                close(connfd);
                continue;
            }

            conn = quicly_qmux_new(&qctx, 0, NULL);
            if (conn == NULL) {
                ptls_buffer_dispose(&early_appdata);
                ptls_free(tls);
                close(connfd);
                continue;
            }

            ctx.is_server = 1;
            ctx.www_dir = www_dir;
            run_qmux_session(connfd, tls, conn, &ctx, &early_appdata);
            ptls_buffer_dispose(&early_appdata);
            quicly_free(conn);
            ptls_free(tls);
            close(connfd);
            /* one connection is enough for the interop runner */
            break;
        }
        close(listenfd);
        return 0;
    }

    if (strcmp(argv[1], "client") == 0) {
        argc--;
        argv++;
        while ((ch = getopt(argc, argv, "o:h")) != -1) {
            switch (ch) {
            case 'o':
                download_dir = optarg;
                break;
            default:
                usage("qmux_interop");
                return 1;
            }
        }
        if (argc - optind < 3) {
            usage("qmux_interop");
            return 1;
        }
        const char *host = argv[optind];
        const char *port = argv[optind + 1];
        char **paths = &argv[optind + 2];
        size_t num_paths = (size_t)(argc - optind - 2);

        init_contexts(1); /* small windows for transfer interop */
        /* no certificate verification (interop runner uses ephemeral certs) */

        int fd = connect_tcp(host, port);
        if (fd < 0) {
            perror("connect");
            return 1;
        }
        {
            int on = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        }

        ptls_t *tls = ptls_new(&tlsctx, 0);
        ptls_handshake_properties_t hsprop = {{0}};
        quicly_conn_t *conn;
        struct st_conn_ctx_t ctx = {0};
        int ret;

        ptls_set_server_name(tls, host, strlen(host));
        hsprop.client.negotiated_protocols.count = 1;
        hsprop.client.negotiated_protocols.list = &alpn_hq;

        ptls_buffer_t early_appdata;
        ptls_buffer_init(&early_appdata, "", 0);
        ret = tls_handshake_loop(fd, tls, &hsprop, 0, &early_appdata);
        if (ret != 0) {
            fprintf(stderr, "TLS handshake failed\n");
            ptls_buffer_dispose(&early_appdata);
            ptls_free(tls);
            close(fd);
            return 1;
        }

        conn = quicly_qmux_new(&qctx, 1, NULL);
        if (conn == NULL) {
            ptls_buffer_dispose(&early_appdata);
            ptls_free(tls);
            close(fd);
            return 1;
        }

        ctx.is_server = 0;
        ctx.paths = paths;
        ctx.num_paths = num_paths;
        ctx.download_dir = download_dir;
        ret = run_qmux_session(fd, tls, conn, &ctx, &early_appdata);
        ptls_buffer_dispose(&early_appdata);
        quicly_free(conn);
        ptls_free(tls);
        close(fd);
        return ret == 0 ? 0 : 1;
    }

    usage(argv[0]);
    return 1;
}
