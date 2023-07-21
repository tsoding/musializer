#!/bin/sh

set -xe

CFLAGS="-Wall -Wextra `pkg-config --cflags raylib`"
LIBS="`pkg-config --libs raylib` -lglfw -lm -ldl -lpthread"

mkdir -p ./build/
clang $CFLAGS -o ./build/musializer ./src/musializer.c $LIBS
clang -o ./build/fft ./src/fft.c -lm
