# QMux

QMux ([draft-ietf-quic-qmux](https://datatracker.ietf.org/doc/draft-ietf-quic-qmux/)) provides QUIC's stream and datagram operations over a single bi-directional byte stream such as TLS over TCP.

This interop suite exercises QMux version 1 as specified in [draft-ietf-quic-qmux-02](https://www.ietf.org/archive/id/draft-ietf-quic-qmux-02.txt). Endpoints run over **TLS 1.3 on TCP port 443**.

For this initial test version, QMux runs use `docker-compose.qmux.yml`: client and server share a `qmuxnet` bridge for TLS/TCP, and a lightweight TCP synchronizer fills the `sim:57832` role that endpoints wait on. Path simulation through ns-3 is left for a later revision once TCP forwarding through the FdNetDevice is wired up for QMux.

The Interop Runner mounts `/www` into your server Docker container, containing one or more randomly generated files. Your server is expected to listen on TCP port 443 and serve files from this directory.

The Interop Runner mounts `/downloads` into your client Docker container (initially empty). Your client is expected to store downloaded files into this directory. The URLs of the files to download are passed using the `REQUESTS` environment variable (space-separated).

After the transfer is completed, the client container is expected to exit with status 0 (or status 1 on error). The Interop Runner verifies that the client downloaded the expected files with matching contents.

The Interop Runner generates a key and certificate chain, mounted into `/certs`. The server loads its private key from `priv.key` and the certificate chain from `cert.pem`.

## Application Protocols and ALPN

As required by Section 8.1 of the QMux draft, application protocols using QMux over TLS MUST negotiate an ALPN identifier distinct from the same application's QUIC mapping. For this interop suite:

| Application | ALPN | Used by test cases |
| --- | --- | --- |
| HTTP/0.9 over QMux | `hq-qmux` | `handshake`, `transfer` |
| HTTP/3 over QMux | `h3-qmux` | `http3` |

These identifiers identify the application running over **QMux draft-02**. Endpoints MUST abort the TLS handshake when ALPN negotiation fails.

HTTP/0.9 request framing matches the QUIC interop suite: the client opens a bidirectional QMux stream, sends `GET <path>\r\n`, and closes the send side. The server responds with the raw file contents on the same stream and closes it.

## Test Cases

The name in parentheses is the value of the `TESTCASE` environment variable passed into your Docker container.

* **Handshake** (`handshake`): Tests successful QMux setup over TLS. The client establishes a single QMux connection and downloads one small file using HTTP/0.9 (`hq-qmux`).

* **Transfer** (`transfer`): Tests stream multiplexing and flow control over QMux. The client should use small initial stream- and connection-level flow control windows such that transfers on the order of 1 MB require window updates. The client establishes a single QMux connection and downloads multiple files concurrently using HTTP/0.9 (`hq-qmux`).

* **HTTP/3** (`http3`): Tests HTTP/3 over QMux. The client downloads multiple files using HTTP/3 with ALPN `h3-qmux`, requesting and transferring them in parallel on a single QMux connection.

## Building an Endpoint

Reference endpoints:

* [quic-go QMux](endpoints/quic-go-qmux/) — [quic-go QMux branch](https://github.com/sierrasystems-ai/quic-go/tree/cursor/qmux-review-fixes-d3a5) (`hq-qmux`, `h3-qmux`)
* [quicly QMux](endpoints/quicly-qmux/) — [h2o/quicly#662](https://github.com/h2o/quicly/pull/662) / `kazuho/qmux-01` (`hq-qmux`; HTTP/3 not yet)

```bash
./endpoints/quic-go-qmux/build.sh
./endpoints/quicly-qmux/build.sh
python3 run.py -p qmux -s quic-go -c quic-go -t handshake,transfer,http3
python3 run.py -p qmux -s quicly -c quicly -t handshake,transfer
```
