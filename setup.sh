#! /bin/bash

sudo apt-get install pkg-config libcapstone-dev
mkdir -p build
cd build || exit
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
make -j
