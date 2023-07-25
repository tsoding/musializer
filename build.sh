#!/bin/sh

set -xe

CFLAGS="-Wall -Wextra -ggdb `pkg-config --cflags raylib`"
LIBS="`pkg-config --libs raylib` -lglfw -lm -ldl -lpthread"

mkdir -p ./build/
clang $CFLAGS -o ./build/libplug.so -fPIC -shared ./src/plug.c $LIBS
clang $CFLAGS -o ./build/musializer ./src/musializer.c $LIBS -L./build/
clang -o ./build/fft ./src/fft.c -lm
