#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE="ccache /media/astrako/DATOS/toolchain/gcc-linaro-4.9.4/bin/aarch64-linux-gnu-"
export ANDROID_MAJOR_VERSION=o
export ANDROID_PLATFORM_VERSION=9

make O=./out $1
make O=./out -j64
