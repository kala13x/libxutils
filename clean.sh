#!/bin/bash

cd examples
make clean
cd ..

if [ -f ./Makefile ]; then
    make clean
fi

if [ -d ./obj ]; then
    rm -rf ./obj
fi

if [ -d ./build ]; then
    rm -rf ./build
fi

if [ -d ./CMakeFiles ]; then
    rm -rf ./CMakeFiles
fi

if [ -f ./CMakeCache.txt ]; then
    rm -f ./CMakeCache.txt
fi

if [ -f ./cmake_install.cmake ]; then
    rm -f ./cmake_install.cmake
fi

if [ -f ./install_manifest.txt ]; then
    rm -f ./install_manifest.txt
fi
