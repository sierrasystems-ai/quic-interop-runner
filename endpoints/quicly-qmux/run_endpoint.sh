#!/bin/bash
set -e

# QMux interop uses a shared bridge (qmuxnet). Disable TX checksum offload and
# create log dirs without applying the ns-3 leftnet/rightnet routes from /setup.sh.
ethtool -K eth0 tx off || true
mkdir -p /logs/qlog

echo "Using commit:" "$(cat commit.txt)"

case "$TESTCASE" in
    handshake|transfer) ;;
    http3)
        echo "quicly QMux endpoint does not implement HTTP/3 over QMux yet"
        exit 127
        ;;
    *)
        echo "unsupported test case: $TESTCASE"
        exit 127
        ;;
esac

if [ "$ROLE" == "client" ]; then
    /wait-for-it.sh sim:57832 -s -t 10
    echo "Starting quicly QMux client..."
    echo "Test case: $TESTCASE"
    echo "Requests: $REQUESTS"

    SERVER=""
    PATHS=""
    for REQ in $REQUESTS; do
        HOSTPORT=$(echo "$REQ" | cut -f3 -d'/')
        SERVER=$(echo "$HOSTPORT" | cut -f1 -d':')
        PORT=$(echo "$HOSTPORT" | cut -f2 -d':')
        FILEPATH=$(echo "$REQ" | cut -f4- -d'/')
        PATHS="$PATHS /$FILEPATH"
    done
    if [ -z "$PORT" ] || [ "$PORT" = "$SERVER" ]; then
        PORT=443
    fi

    cd /downloads
    # shellcheck disable=SC2086
    /quicly/qmux_interop client -o /downloads "$SERVER" "$PORT" $PATHS
else
    echo "Starting quicly QMux server for test: $TESTCASE"
    ls -l /www || true
    /quicly/qmux_interop server -c /certs/cert.pem -k /certs/priv.key -d /www 0.0.0.0 443
fi
