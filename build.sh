#!/usr/bin/env bash
set -euo pipefail

CXX=${CXX:-g++}
CXXFLAGS=${CXXFLAGS:--std=c++11 -Wall -Wextra -I.}
LDFLAGS=${LDFLAGS:-}
OUTPUT=${1:-main}

"$CXX" $CXXFLAGS \
  portfw.cpp \
  network/tcp_socket.cpp \
  network/Proxy.cpp \
  $LDFLAGS \
  -lssl \
  -lcrypto \
  -pthread \
  -o "$OUTPUT"

echo "Build complete: $OUTPUT"
