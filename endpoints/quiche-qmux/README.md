# quiche QMux interop endpoint

HTTP/0.9 and HTTP/3 over QMux endpoint built from [LPardue/quiche `@qmux-support`](https://github.com/LPardue/quiche/commits/qmux-support/).

Uses the upstream `qmux-demo` binaries with ALPNs remapped to the interop suite tokens (`hq-qmux`, `h3-qmux`), plus interop overlays for multi-URL dumps and chunked HTTP/0.9 sends under flow control.

Transfer uses small initial windows (`max_data=128KiB`, `max_stream_data=64KiB`) so multi-MB downloads exercise `MAX_DATA` / `MAX_STREAM_DATA`, matching the suite and quic-go. Library patches on `qmux-support`:

1. Emit organic connection `MAX_DATA` updates from flow control (UDP send path already did)
2. Refresh `tx_cap` after receiving `MAX_DATA`
3. Round-robin flushable streams when emitting QMux records
4. Keep still-flushable streams queued when the current QMux record cannot fit another STREAM header
5. Suppress duplicate connection `DATA_BLOCKED` frames for the same limit

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
