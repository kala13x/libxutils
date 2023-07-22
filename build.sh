#!/bin/bash
# This source is part of "libxutils" project
# 2015-2023  Sun Dro (s.kalatoz@gmail.com)

PROJ_PATH=$(dirname $(readlink -f "$0"))
TOOL_PATH=$PROJ_PATH/examples
LIB_PATH=$PROJ_PATH

TOOLS_DONE=0
MAKE_TOOL=0
CPU_COUNT=1

if [ ! -z "$1" ]; then
    MAKE_TOOL=$1
fi

if [ $MAKE_TOOL == 0 ]; then
    echo "Specify build tool (cmake / smake)"
    echo "example: $0 smake"
    exit 1
fi

# List of possible OpenSSL library locations
SSL_LD_PATH=(
    "/lib"
    "/lib64"
    "/usr/lib"
    "/usr/lib64"
    "/usr/local/lib"
    "/usr/local/lib64"
    "/usr/local/ssl/lib"
    "/usr/local/ssl/lib64"
)

find_lib() {
    for directory in "${SSL_LD_PATH[@]}"; do
        path=$(find "$directory" -name "$@")

        if [ -n "$path" ]; then
            echo "$path"
            return
        fi

    done
    echo ""
}

clean_dir() {
    cd "$@" 
    [ -f ./Makefile ] && make clean
    [ -d ./obj ] && rm -rf ./obj
    [ -d ./build ] && rm -rf ./build
    [ -d ./CMakeFiles ] && rm -rf ./CMakeFiles
    [ -f ./CMakeCache.txt ] && rm -rf ./CMakeCache.txt
    [ -f ./cmake_install.cmake ] && rm -rf ./cmake_install.cmake
    [ -f ./install_manifest.txt ] && rm -rf ./install_manifest.txt
    cd $PROJ_PATH
}

clean_project() {
    clean_dir $PROJ_PATH/examples
    clean_dir $PROJ_PATH
}

update_cpu_count() {
    if [ $OSTYPE == linux-gnu ]; then
        CPU_COUNT=$(nproc)
    fi
}

build_tools() {
    [ "$TOOLS_DONE" -eq 1 ] && return
    cd $PROJ_PATH/examples

    if [[ $MAKE_TOOL == "cmake" ]]; then
        mkdir -p build && cd build && cmake ..
        TOOL_PATH=$PROJ_PATH/examples/build
    fi

    make -j $CPU_COUNT
    cd $PROJ_PATH
    TOOLS_DONE=1
}

build_library() {
    cd $PROJ_PATH

    if [[ $MAKE_TOOL == "cmake" ]]; then
        mkdir -p build && cd build && cmake ..
        LIB_PATH=$PROJ_PATH/build
    fi

    make -j $CPU_COUNT
    cd $PROJ_PATH
}

install_tools() {
    build_tools
    cd $TOOL_PATH
    sudo make install
    cd $PROJ_PATH
}

install_library() {
    cd $LIB_PATH
    sudo make install
    cd $PROJ_PATH
}

echo "Checking OpenSSL libraries."
LIB_CRYPTO=$(find_lib "libcrypto.so")
LIB_SSL=$(find_lib "libssl.so")

# If the OpenSSL libraries are found, set the environment variable
if [ -n "$LIB_CRYPTO" ] && [ -n "$LIB_SSL" ]; then
    echo "OpenSSL libraries found!"
    echo "Crypto: $LIB_CRYPTO"
    echo "SSL: $LIB_SSL"
    export XUTILS_USE_SSL=y
else
    echo 'OpenSSL libraries not found!'
fi

# Build the library
update_cpu_count
clean_project
build_library

for arg in "$@"; do
    if [[ $arg == "--examples" ]]; then
        build_tools
    fi

    if [[ $arg == "--install" ]]; then
        install_library
        install_tools
    fi

    if [[ $arg == "--cleanup" ]]; then
        clean_project
    fi
done

exit 0
