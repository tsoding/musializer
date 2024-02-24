# Makefile to rebuild plug.c. Unfortunately you must manually delete libplug.lib before executing this script
# or it will say "access denied". I tried to find a workaround to delete it from this script but I failed, good luck
# to who is willing to try it. 
# Hours wasted trying to delete it within the Makefile: 1.5 

LIBS = -lm -lpthread -lwinmm -lgdi32 -l:raylib.dll

all:
	gcc -Wall -Wextra -ggdb -I./build/ -I./raylib/raylib-5.0/src/ -fPIC -shared -o ./build/libplug.lib ./src/plug.c ./src/ffmpeg_windows.c ${LIBS} -L./build/