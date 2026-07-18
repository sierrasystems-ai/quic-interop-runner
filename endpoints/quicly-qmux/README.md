# quicly QMux interop endpoint

HTTP/0.9 over QMux endpoint built from [h2o/quicly#662](https://github.com/h2o/quicly/pull/662) (`kazuho/qmux-01`).

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
