#!/bin/bash
# This source is part of "libxutils" project
# 2015-2023  Sun Dro (s.kalatoz@gmail.com)

PROJ_PATH=$(pwd -P)/$(dirname "$0")

clean_path() {
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

clean_path $PROJ_PATH
clean_path $PROJ_PATH/tools
clean_path $PROJ_PATH/examples
