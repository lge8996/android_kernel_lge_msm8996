#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE=/home/astrako/android/kernel/toolchain/aarch64-linux-android-4.9/bin/aarch64-linux-android-
export ANDROID_MAJOR_VERSION=o

make O=./out $1
make O=./out -j64
