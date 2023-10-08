#!/bin/bash

# Convenient script to build this project with CMake

rm -rf build
mkdir build
pushd build

#/usr/lib/aarch64-linux-gnu/qt5/bin/qmake ..
/usr/local/Qt-5.15.10/bin/qmake ..

make -j4

popd

cp lunch.sh build/release
