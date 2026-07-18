#!/bin/bash
set -e

# QMux interop uses a shared bridge (qmuxnet). Disable TX checksum offload and
# create log dirs without applying the ns-3 leftnet/rightnet routes from /setup.sh.
ethtool -K eth0 tx off || true
mkdir -p /logs/qlog

echo "Using commit:" "$(cat commit.txt)"

case "$TESTCASE" in
    handshake)
        HTTP_VERSION="0.9"
        FC_OPTS="--max-data 10000000 --max-stream-data 1000000"
        ;;
    transfer)
        HTTP_VERSION="0.9"
        # Small windows so transfer exercises stream/connection flow control.
        FC_OPTS="--max-data 131072 --max-stream-data 65536"
        ;;
    http3)
        HTTP_VERSION="h3"
        FC_OPTS="--max-data 10000000 --max-stream-data 1000000"
        ;;
    *)
        echo "unsupported test case: $TESTCASE"
        exit 127
        ;;
esac

if [ "$ROLE" == "client" ]; then
    /wait-for-it.sh sim:57832 -s -t 10
    echo "Starting quiche QMux client..."
    echo "Test case: $TESTCASE"
    echo "Requests: $REQUESTS"

    # shellcheck disable=SC2086
    RUST_LOG=info ./qmux-client \
        --no-verify \
        --http-version "$HTTP_VERSION" \
        --dump-dir /downloads \
        $FC_OPTS \
        $CLIENT_PARAMS \
        $REQUESTS
else
    echo "Starting quiche QMux server for test: $TESTCASE"
    ls -l /www || true
    # shellcheck disable=SC2086
    RUST_LOG=info ./qmux-server \
        --listen 0.0.0.0:443 \
        --cert /certs/cert.pem \
        --key /certs/priv.key \
        --root /www \
        $FC_OPTS \
        $SERVER_PARAMS
fi
