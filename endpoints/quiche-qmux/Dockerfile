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

# Overlay interop client with multi-URL HTTP/0.9 dumps to /downloads.
COPY qmux-client.rs qmux-demo/src/bin/qmux-client.rs

RUN apt-get update \
 && apt-get install -y cmake \
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
