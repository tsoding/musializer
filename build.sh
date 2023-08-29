#!/bin/sh

set -xe

CFLAGS="-Wall -Wextra -ggdb `pkg-config --cflags raylib`"
LIBS="`pkg-config --libs raylib` `pkg-config --libs glfw3` -lm -ldl -lpthread"

mkdir -p ./build/
if [ ! -z "${HOTRELOAD}" ]; then
    clang $CFLAGS -o ./build/libplug.so -fPIC -shared ./src/plug.c ./src/ffmpeg_linux.c $LIBS
    clang $CFLAGS -DHOTRELOAD ${AUTORELOAD:+"-DAUTORELOAD"} ${AUTORELOAD:+"-D_GNU_SOURCE"} -o ./build/musializer ./src/musializer.c $LIBS -L./build/ -Wl,-rpath=./build/ -Wl,-rpath=./
else
    clang $CFLAGS  -o ./build/musializer ./src/plug.c  ./src/ffmpeg_linux.c ./src/musializer.c $LIBS -L./build/
fi

cp -r ./resources/ ./build/
