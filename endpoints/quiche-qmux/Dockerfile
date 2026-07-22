FROM rust:1.86 AS builder

WORKDIR /build

ARG QUICHE_REPO=https://github.com/LPardue/quiche.git
ARG QUICHE_REF=qmux-support

RUN git clone --depth 1 --branch "${QUICHE_REF}" "${QUICHE_REPO}" . \
 && git rev-parse HEAD > commit.txt

# Align ALPNs with the interop suite (hq-qmux / h3-qmux).
RUN find . -type f \( -name '*.rs' -o -name '*.h' -o -name '*.c' -o -name '*.md' \) \
      -exec grep -l 'hqx-01\|h3qx-01' {} + \
    | while read -r f; do \
        sed -i 's/hqx-01/hq-qmux/g; s/h3qx-01/h3-qmux/g' "$f"; \
      done

# Upstream qmux-support library fixes needed for small-window transfer:
# 1) emit organic MAX_DATA from connection flow control (UDP send path does)
# 2) refresh tx_cap after MAX_DATA (QUIC recv path already does this)
# 3) round-robin flushable streams so one body cannot starve siblings
COPY patches/0001-qmux-update-tx-cap-on-recv.patch \
     patches/0002-qmux-round-robin-flushable-streams.patch \
     patches/0003-qmux-emit-max-data-from-flow-control.patch \
     /tmp/
RUN patch -p1 < /tmp/0001-qmux-update-tx-cap-on-recv.patch \
 && patch -p1 < /tmp/0002-qmux-round-robin-flushable-streams.patch \
 && patch -p1 < /tmp/0003-qmux-emit-max-data-from-flow-control.patch

# Overlay interop client/server: multi-URL dumps + chunked HTTP/0.9 sends.
COPY qmux-client.rs qmux-demo/src/bin/qmux-client.rs
COPY qmux-server.rs qmux-demo/src/bin/qmux-server.rs

RUN apt-get update \
 && apt-get install -y cmake clang libclang-dev pkg-config \
 && rm -rf /var/lib/apt/lists/* \
 && cargo build --release -p qmux-demo

FROM martenseemann/quic-network-simulator-endpoint:latest

WORKDIR /quiche

COPY --from=builder \
    /build/target/release/qmux-client \
    /build/target/release/qmux-server \
    /build/commit.txt \
    ./
COPY run_endpoint.sh .
RUN chmod +x run_endpoint.sh

ENV RUST_LOG=info

ENTRYPOINT [ "./run_endpoint.sh" ]
