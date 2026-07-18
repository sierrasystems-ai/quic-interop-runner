FROM martenseemann/quic-network-simulator-endpoint:latest AS builder

ARG TARGETPLATFORM
RUN echo "TARGETPLATFORM: ${TARGETPLATFORM}"

RUN apt-get update && apt-get install -y wget tar git ca-certificates && rm -rf /var/lib/apt/lists/*

ENV GOVERSION=1.26.0

RUN platform=$(echo ${TARGETPLATFORM} | tr '/' '-') && \
  filename="go${GOVERSION}.${platform}.tar.gz" && \
  wget https://dl.google.com/go/${filename} && \
  tar xfz ${filename} && \
  rm ${filename}

ENV PATH="/go/bin:${PATH}"

# QMux-capable quic-go branch used for the initial interop endpoint.
ARG QUIC_GO_REPO=https://github.com/sierrasystems-ai/quic-go.git
ARG QUIC_GO_REF=cursor/qmux-review-fixes-d3a5

RUN git clone --depth 1 --branch "${QUIC_GO_REF}" "${QUIC_GO_REPO}" /quic-go
WORKDIR /quic-go

# Overlay interop endpoints that exercise DialQMux / AcceptQMux.
COPY client/main.go interop/client/main.go
COPY server/main.go interop/server/main.go
COPY http09/client.go interop/http09/client.go
COPY http09/server.go interop/http09/server.go
COPY run_endpoint.sh interop/run_endpoint.sh

RUN git rev-parse HEAD | tee commit.txt
RUN go build -o server -ldflags="-X github.com/quic-go/quic-go/qlog.quicGoVersion=$(git describe --always --long --dirty)" interop/server/main.go
RUN go build -o client -ldflags="-X github.com/quic-go/quic-go/qlog.quicGoVersion=$(git describe --always --long --dirty)" interop/client/main.go

FROM martenseemann/quic-network-simulator-endpoint:latest

WORKDIR /quic-go

COPY --from=builder /quic-go/commit.txt /quic-go/server /quic-go/client ./
COPY --from=builder /quic-go/interop/run_endpoint.sh ./

RUN chmod +x run_endpoint.sh

ENTRYPOINT [ "./run_endpoint.sh" ]
