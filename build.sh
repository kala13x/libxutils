#!/bin/bash
# This source is part of "libxutils" project
# 2015-2023  Sun Dro (s.kalatoz@gmail.com)

PROJ_PATH=$(pwd -P)/$(dirname "$0")
TOOL_PATH=$PROJ_PATH/tools
LIB_PATH=$PROJ_PATH
MAKE_TOOL="cmake"

EXAMPLES_DONE=0
TOOLS_DONE=0
CPU_COUNT=1

# Check if its linux or cmake is not installed
if [ $OSTYPE == linux-gnu ]; then
    if ! command -v cmake &> /dev/null; then
        MAKE_TOOL="make"
    fi
fi

# Parse command line arguments
for arg in "$@"; do
    if [[ $arg == --tool=* || $arg == -m=* ]]; then
        MAKE_TOOL="${arg#*=}"
        echo "Using tool: $MAKE_TOOL"
    fi
done

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

        if [ -d "$directory" ]; then
            path=$(find "$directory" -name "$@")

            if [ -n "$path" ]; then
                echo "$path"
                return
            fi
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
    clean_dir $PROJ_PATH/tools
    clean_dir $PROJ_PATH
}

update_cpu_count() {
    if [ $OSTYPE == linux-gnu ]; then
        CPU_COUNT=$(nproc)
    fi
}

build_tools() {
    [ "$TOOLS_DONE" -eq 1 ] && return
    cd $PROJ_PATH/tools

    if [[ $MAKE_TOOL == "cmake" ]]; then
        mkdir -p build && cd build && cmake ..
        TOOL_PATH=$PROJ_PATH/tools/build
    fi

    make -j $CPU_COUNT
    cd $PROJ_PATH
    TOOLS_DONE=1
}

build_examples() {
    [ "$EXAMPLES_DONE" -eq 1 ] && return
    cd $PROJ_PATH/examples

    if [[ $MAKE_TOOL == "cmake" ]]; then
        mkdir -p build && cd build && cmake ..
    fi

    make -j $CPU_COUNT
    cd $PROJ_PATH
    EXAMPLES_DONE=1
}

build_library() {
    cd $PROJ_PATH

    if [[ $MAKE_TOOL == "make" ]]; then
        make -j $CPU_COUNT
        LIB_PATH=$PROJ_PATH
    elif [[ $MAKE_TOOL == "smake" ]]; then
        smake . && make -j $CPU_COUNT
        LIB_PATH=$PROJ_PATH
    elif [[ $MAKE_TOOL == "cmake" ]]; then
        mkdir -p build && cd build
        cmake .. && make -j $CPU_COUNT
        LIB_PATH=$PROJ_PATH/build
    else
        echo "Unknown build tool: $MAKE_TOOL"
        echo "Specify cmake, smake or make)"
        exit 1
    fi

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

search_and_config_ssl() {
    [[ $MAKE_TOOL == "cmake" ]] && return
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
        export XUTILS_USE_SSL=n
    fi
}

# Build the library
search_and_config_ssl
update_cpu_count
clean_project
build_library

for arg in "$@"; do
    if [[ $arg == "--examples" || $arg == "-e" ]]; then
        build_examples
    fi

    if [[ $arg == "--tools" || $arg == "-t" ]]; then
        build_tools
    fi

    if [[ $arg == "--install" || $arg == "-i" ]]; then
        install_library
        install_tools
    fi
done

# Do cleanup last
for arg in "$@"; do
    if [[ $$arg == "--clean" || $arg == "-c" ]]; then
        clean_project
    fi
done

exit 0
