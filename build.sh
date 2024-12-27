#!/bin/bash
# Ensure the build directory exists
mkdir -p build
cd build
cmake -Wno-dev ..
make
cd ..
