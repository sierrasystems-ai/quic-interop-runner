# quic-go QMux Interop Endpoint

Reference QMux interop endpoint for the interop runner. It builds against
[`sierrasystems-ai/quic-go` branch `cursor/qmux-review-fixes-d3a5`](https://github.com/sierrasystems-ai/quic-go/tree/cursor/qmux-review-fixes-d3a5)
and overlays client/server binaries that speak QMux over TLS/TCP.

## Build

```bash
./build.sh
```

This produces the local image `quic-go-qmux-interop:local` registered in
`implementations_qmux.json`.

## Supported test cases

| TESTCASE | ALPN | Application |
| --- | --- | --- |
| `handshake` | `hq-qmux` | HTTP/0.9 over QMux |
| `transfer` | `hq-qmux` | HTTP/0.9 over QMux |
| `http3` | `h3-qmux` | HTTP/3 over QMux |
