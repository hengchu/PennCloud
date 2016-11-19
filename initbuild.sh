#!/bin/bash

cd thirdparty/protobuf
./autogen.sh

cd ../..

cd build
ln -s ../CMakeLists.txt
mkdir protobuf-build
cd protobuf-build

../../thirdparty/protobuf/configure --prefix=`pwd` CXXFLAGS='-std=gnu++11'
make -j2 && make install


cd ..
cmake .
