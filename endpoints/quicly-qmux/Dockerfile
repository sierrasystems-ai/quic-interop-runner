FROM h2oserver/h2o-ci:ubuntu2004 AS builder

USER root
WORKDIR /

ARG QUICLY_REPO=https://github.com/h2o/quicly.git
ARG QUICLY_REF=kazuho/qmux-01

RUN git clone --depth 1 --branch "${QUICLY_REF}" "${QUICLY_REPO}" quicly \
 && cd quicly \
 && git submodule update --init --recursive

WORKDIR /quicly

# Upstream kazuho/qmux-01 fixes:
# 1) return SENDBUF_FULL when a new QMux record cannot hold min_space
# 2) schedule connection-level MAX_DATA updates from QMux receive
# 3) skip no-op MAX_DATA forced by repeated DATA_BLOCKED for the same limit
COPY patches/0001-qmux-sendbuf-full-when-record-too-small.patch \
     patches/0002-qmux-schedule-max-data.patch \
     patches/0003-qmux-skip-noop-max-data.patch \
     /tmp/
RUN patch -p1 < /tmp/0001-qmux-sendbuf-full-when-record-too-small.patch \
 && patch -p1 < /tmp/0002-qmux-schedule-max-data.patch \
 && patch -p1 < /tmp/0003-qmux-skip-noop-max-data.patch

# Overlay the HTTP/0.9-over-QMux interop endpoint and register it in CMake.
COPY qmux_interop.c examples/qmux_interop.c
RUN printf '\nADD_EXECUTABLE(qmux_interop ${PICOTLS_OPENSSL_FILES} examples/qmux_interop.c)\nTARGET_LINK_LIBRARIES(qmux_interop quicly ${OPENSSL_CRYPTO_LIBRARIES} ${CMAKE_DL_LIBS} m)\n' >> CMakeLists.txt \
 && cmake -DWITH_FUSION=OFF . \
 && make -j"$(nproc)" qmux_interop \
 && git rev-parse HEAD > commit.txt

FROM martenseemann/quic-network-simulator-endpoint:latest

WORKDIR /quicly

# Runtime image is newer Ubuntu; ship OpenSSL 1.1 used by the ubuntu2004 build.
COPY --from=builder /usr/lib/x86_64-linux-gnu/libcrypto.so.1.1 /usr/lib/x86_64-linux-gnu/
COPY --from=builder /quicly/qmux_interop /quicly/commit.txt ./
COPY run_endpoint.sh .
RUN chmod +x run_endpoint.sh \
 && ldconfig

ENTRYPOINT [ "./run_endpoint.sh" ]
