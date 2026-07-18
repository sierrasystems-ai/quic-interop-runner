# quiche QMux interop endpoint

HTTP/0.9 and HTTP/3 over QMux endpoint built from [LPardue/quiche `@qmux-support`](https://github.com/LPardue/quiche/commits/qmux-support/).

Uses the upstream `qmux-demo` binaries with ALPNs remapped to the interop suite tokens (`hq-qmux`, `h3-qmux`), overlays that dump multi-file downloads and send HTTP/0.9 responses one stream at a time under flow control, and a small library patch so `recv_qmux` refreshes send capacity after `MAX_DATA` (required for peers with small initial connection windows such as quic-go).

| TESTCASE | ALPN | Application |
| --- | --- | --- |
| `handshake` | `hq-qmux` | HTTP/0.9 over QMux |
| `transfer` | `hq-qmux` | HTTP/0.9 over QMux |
| `http3` | `h3-qmux` | HTTP/3 over QMux |

## Build

```bash
./endpoints/quiche-qmux/build.sh
```

Produces `quiche-qmux-interop:local`.

## Run

```bash
python3 run.py -p qmux -s quiche -c quiche -t handshake,transfer,http3
```
