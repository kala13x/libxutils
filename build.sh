#!/bin/bash
./clean.sh

MAKE_TOOL=0
CPU_COUNT=1

if [ ! -z "$1" ]; then
    MAKE_TOOL=$1
fi

# Check argument
if [ $MAKE_TOOL == 0 ]; then
    echo "Specify build tool (cmake / smake)"
    echo "example: $0 smake"
    exit 1
fi

# Detect CPU count
if [ $OSTYPE == linux-gnu ]; then
    CPU_COUNT=$(nproc)
fi

# List of possible OpenSSL library locations
LIB_CRYPTO_PATHS=(
    "/lib/libcrypto.so"
    "/lib64/libcrypto.so"
    "/usr/lib/libcrypto.so"
    "/usr/lib64/libcrypto.so"
    "/usr/local/lib/libcrypto.so"
    "/usr/local/lib64/libcrypto.so"
    "/usr/local/ssl/lib/libcrypto.so"
    "/usr/local/ssl/lib64/libcrypto.so"
)

LIB_SSL_PATHS=(
    "/lib/libssl.so"
    "/lib64/libssl.so"
    "/usr/lib/libssl.so"
    "/usr/lib64/libssl.so"
    "/usr/local/lib/libssl.so"
    "/usr/local/lib64/libssl.so"
    "/usr/local/ssl/lib/libssl.so"
    "/usr/local/ssl/lib64/libssl.so"
)

# Default values
LIB_CRYPTO=""
LIB_SSL=""

# Function to find the library file from a list of possible locations
find_lib() {
    local paths=("$@")
    for path in "${paths[@]}"; do
        if [ -f "$path" ]; then
            echo "$path"
            return
        fi
    done
    echo ""
}

# Detect the operating system
echo "Checking OpenSSL libraries."

LIB_CRYPTO=$(find_lib "${LIB_CRYPTO_PATHS[@]}")
LIB_SSL=$(find_lib "${LIB_SSL_PATHS[@]}")

# If the OpenSSL libraries are found, set the environment variable
if [ -n "$LIB_CRYPTO" ] && [ -n "$LIB_SSL" ]; then
    echo "OpenSSL libraries found!"
    echo "Crypto: $LIB_CRYPTO"
    echo "SSL: $LIB_SSL"
    export XUTILS_USE_SSL=y
else
    echo 'OpenSSL libraries not found!'
fi

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
