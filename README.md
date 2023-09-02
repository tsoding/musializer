# Musializer

**THIS SOFTWARE IS UNFINISHED!!! Don't have any high expectations. Read [CONTRIBUTING.md](CONTRIBUTING.md) if you have a strong irresistable desire to contribute something here.**

<p align=center>
  <img src="./logo/logo-256.png">
</p>

The project aims to make a tool for creating beautiful music visualizations and rendering high quality videos of them.

## Demo

*Music by [@nu11](https://soundcloud.com/nu11_ft) from [https://soundcloud.com/nu11_ft/nu11-wip-works-2016-2022](https://soundcloud.com/nu11_ft/nu11-wip-works-2016-2022) at 20:38*

https://github.com/tsoding/musializer/assets/165283/8b9f9653-9b3d-4c04-9569-338fa19af071

## Build

Dependencies:
- [raylib](https://www.raylib.com/) and all its transitive dependencies.
- [ffmpeg](https://ffmpeg.org/) executable available in `PATH` environment variable. (it is called as a child process)

The project provides a bunch of build shell scripts that have the following naming scheme `build_<platform>_<compiler>.sh`. Pick the appropriate one.

### POSIX

```console
$ ./build_posix_clang.sh
$ ./build/musializer
```

Keep in mind that the application needs [./resources/](./resources/) to be present in the folder it is ran from.

### Windows

PS = PowerShell
$ = MSYS MingW64 window

1. Download [MSYS2](https://www.msys2.org/)
2. Exit the application it opens (it's the wrong one)
3. Open PowerShell, cause it's funner
4. `PS cd C:\msys64\` or where ever you installed MSYS
5. `PS .\mingw64.exe`
6. `$ pacman -S --needed base-devel mingw-w64-x86_64-toolchain`
   -  if the previous command didn't work, try running `$ pacman -Sy msys2-keyring; pacman -Syu` to update keys, and then try running the command above again
7. `PS mv .\mingw64\bin\windres.exe .\mingw64\bin\x86_64-w64-mingw32-windres.exe` for some stupid ass reason, in MSYS, windres.exe doesn't have the prefix
8. `$ cd {musializer root}`
   -  if you don't have Musializer cloned yet, you can do `PS git clone https://github.com/tsoding/musializer.git`
   - Also remember to have [RayLib 4.5.0](https://github.com/raysan5/raylib/releases/download/4.5.0/raylib-4.5.0_win64_mingw-w64.zip) in the same directory but in its folder
9. `$ ./build_windows_mingw.sh`
10. ???
11. Profit!

Also, a side note, inside the MSYS, Ctrl+Ins = copy and Shift+Ins = paste

## Hot Reloading

**Only on Linux for now**

```console
$ export HOTRELOAD=1
$ ./build_posix.sh
$ ./build/musializer
```

Keep the app running. Rebuild with `./build.sh`. Hot reload by focusing on the window of the app and pressing <kbd>r</kbd>.

The way it works is by putting the majority of the logic of the application into a `libplug` dynamic library and just reloading it when requested. The [rpath](https://en.wikipedia.org/wiki/Rpath) (aka hard-coded run-time search path) for that library is set to `.` and `./build/`. See [build.sh](./build.sh) for more information on how everything is configured.
