FROM h2oserver/h2o-ci:ubuntu2004 AS builder

USER root
WORKDIR /

ARG QUICLY_REPO=https://github.com/h2o/quicly.git
ARG QUICLY_REF=kazuho/qmux-01

RUN git clone --depth 1 --branch "${QUICLY_REF}" "${QUICLY_REPO}" quicly \
 && cd quicly \
 && git submodule update --init --recursive

WORKDIR /quicly

# Overlay the HTTP/0.9-over-QMux interop endpoint and register it in CMake.
COPY qmux_interop.c examples/qmux_interop.c
RUN printf '\nADD_EXECUTABLE(qmux_interop ${PICOTLS_OPENSSL_FILES} examples/qmux_interop.c)\nTARGET_LINK_LIBRARIES(qmux_interop quicly ${OPENSSL_CRYPTO_LIBRARIES} ${CMAKE_DL_LIBS} m)\n' >> CMakeLists.txt \
 && cmake -DWITH_FUSION=OFF . \
 && make -j"$(nproc)" qmux_interop \
 && git rev-parse HEAD > commit.txt

FROM martenseemann/quic-network-simulator-endpoint:latest

WORKDIR /quicly

COPY --from=builder /quicly/qmux_interop /quicly/commit.txt ./
COPY run_endpoint.sh .
RUN chmod +x run_endpoint.sh

ENTRYPOINT [ "./run_endpoint.sh" ]
