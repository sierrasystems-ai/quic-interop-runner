# quicly QMux interop endpoint

HTTP/0.9 over QMux endpoint built from [h2o/quicly#662](https://github.com/h2o/quicly/pull/662) (`kazuho/qmux-01`).

Library patch on that branch:

1. `do_allocate_qmux_frame`: return `SENDBUF_FULL` when remaining buffer capacity is smaller than `min_space` (avoids STREAM header overrun / SIGSEGV under tight peer flow control).

| TESTCASE | ALPN | Application |
| --- | --- | --- |
| `handshake` | `hq-qmux` | HTTP/0.9 over QMux |
| `transfer` | `hq-qmux` | HTTP/0.9 over QMux |
| `http3` | — | unsupported (exit 127) |

## Build

```bash
./endpoints/quicly-qmux/build.sh
```

Produces `quicly-qmux-interop:local`.

## Run

```bash
python3 run.py -p qmux -s quicly -c quicly -t handshake,transfer
```
