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
        CLIENT_FC="--max-data 10000000 --max-stream-data 1000000"
        SERVER_FC="--max-data 10000000 --max-stream-data 1000000"
        ;;
    transfer)
        HTTP_VERSION="0.9"
        # Client: keep receive windows large enough that peers like quicly do not
        # hit fragile STREAM_DATA_BLOCKED paths (quicly has segfaulted at 64KiB).
        # Server: connection window large for multi-MB bodies; stream window is
        # mostly irrelevant for tiny HTTP/0.9 requests.
        CLIENT_FC="--max-data 16000000 --max-stream-data 1000000"
        SERVER_FC="--max-data 16000000 --max-stream-data 1000000"
        ;;
    http3)
        HTTP_VERSION="h3"
        CLIENT_FC="--max-data 10000000 --max-stream-data 1000000"
        SERVER_FC="--max-data 10000000 --max-stream-data 1000000"
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
        $CLIENT_FC \
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
        $SERVER_FC \
        $SERVER_PARAMS
fi
