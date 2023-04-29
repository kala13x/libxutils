#!/bin/bash

LIB_CRYPTO=/usr/lib64/libcrypto.so
LIB_SSL=/usr/lib64/libssl.so

if [ -f "$LIB_CRYPTO" ] && [ -f "$LIB_SSL"  ]; then
    export XUTILS_USE_SSL=y
fi

if [ $OSTYPE == linux-gnu ]; then
    CPU_COUNT=$(nproc)
else
    CPU_COUNT=1
fi

MAKE_TOOL=0

if [ ! -z "$1" ]; then
    MAKE_TOOL=$1
fi

if [ $MAKE_TOOL == 0 ]; then
    echo "Specify build tool (cmake / smake)"
    echo "example: $0 smake"
    exit 1
fi

./clean.sh

# Generate Makefile and build library
$MAKE_TOOL . && make -j $CPU_COUNT

if [ ! -z "$2" ]; then
    if [ $2 == "--examples" ]; then
        cd examples
        make -j $CPU_COUNT
        cd ..
    elif [ $2 == "--install" ]; then
        sudo make install
        cd examples
        make -j $CPU_COUNT
        sudo make install
    fi
fi

exit 0
