#!/bin/sh
set -eu

# Get env
PREFIX="${PREFIX:-/usr/local}"
INSTALLPATH="$PREFIX/bin/speed"

# Build and install
clang -O3 -lpthread -o "speed.bin" "src/speed.c"
install -v -m 0755 "speed.bin" "$INSTALLPATH"