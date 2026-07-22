#!/bin/bash
set -e

# QMux interop uses a shared bridge (qmuxnet). Disable TX checksum offload and
# create log dirs without applying the ns-3 leftnet/rightnet routes from /setup.sh.
ethtool -K eth0 tx off || true
mkdir -p /logs/qlog

echo "Using commit:" "$(cat commit.txt)"

if [ "$ROLE" == "client" ]; then
    # Wait for the simulator synchronizer socket.
    /wait-for-it.sh sim:57832 -s -t 10
    echo "Starting QMux client..."
    echo "Client params: $CLIENT_PARAMS"
    echo "Test case: $TESTCASE"
    QUIC_GO_LOG_LEVEL=debug ./client $CLIENT_PARAMS $REQUESTS
else
    echo "Running QMux server."
    QUIC_GO_LOG_LEVEL=debug ./server "$@"
fi
