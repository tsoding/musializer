# Musializer

<p align=center>
  <img src="./resources/logo/logo-256.png">
</p>

> [!WARNING]
> This software is unfinished. Keep your expectations low.

The project aims to make a tool for creating beautiful music visualizations and rendering high quality videos of them.

*Please, read [CONTRIBUTING.md](CONTRIBUTING.md) before making a PR.*

## Demo

*Music by [@nu11](https://soundcloud.com/nu11_ft) from [https://soundcloud.com/nu11_ft/nu11-wip-works-2016-2022](https://soundcloud.com/nu11_ft/nu11-wip-works-2016-2022) at 20:38*

https://github.com/tsoding/musializer/assets/165283/8b9f9653-9b3d-4c04-9569-338fa19af071

## Download Binaries

- Windows: [musializer-alpha1-win64.zip](https://github.com/tsoding/musializer/releases/download/alpha1/musializer-alpha1-win64.zip)
- Linux: *in progress*

## Build from Source

External Dependencies:
- [ffmpeg](https://ffmpeg.org/) executable available in `PATH` environment variable. (it is called as a child process)

We are using Custom Build System written entirely in C called `nob`. It is similar to [nobuild](https://github.com/tsoding/nobuild) in spirit. [nob.h](./nob.h) is the Build System and [nob.c](./nob.c) is the program that builds Musializer.

Before using `nob` you need to bootstrap it. Just compile it with the available C compiler. On Linux it's usually `$ cc -o nob nob.c` on Windows with MSVC from within `vcvarsall.bat` it's `$ cl.exe nob.c`. You only need to boostrap it once. After the bootstrap you can just keep running the same executable over and over again. It even tries to rebuild itself if you modify [nob.c](./nob.c) (which may fail sometimes, so in that case be ready to reboostrap it).

I really recommend to read [nob.c](./nob.c) and [nob.h](./nob.h) to get an idea of how it all actually works. The Build System is a work in progress, so if something breaks be ready to dive into it.

### Linux

```console
$ cc -o nob nob.c # only once
$ ./nob
$ ./build/musializer
```

Keep in mind that the application needs [./resources/](./resources/) to be present in the folder it is ran from.

### Windows MSVC

From within `vcvarsall.bat` do

```console
> cl.exe nob.c # only once
> nob.exe
> build\musializer.exe
```

### Cross Compilation from Linux to Windows using MinGW-w64

Install [MinGW-w64](https://www.mingw-w64.org/) from your distro repository.

```console
$ cc -o nob nob.c # only once
$ ./nob config -t win64-mingw
$ ./nob
$ wine ./build/musializer.exe
```

## Hot Reloading

**Only on Linux for now**

```console
$ cc -o nob nob.c # only once
$ ./nob config -r
$ ./nob
$ ./build/musializer
```

Keep the app running. Rebuild with `./nob`. Hot reload by focusing on the window of the app and pressing <kbd>r</kbd>.

The way it works is by putting the majority of the logic of the application into a `libplug` dynamic library and just reloading it when requested. The [rpath](https://en.wikipedia.org/wiki/Rpath) (aka hard-coded run-time search path) for that library is set to `.` and `./build/`. See [build.sh](./build.sh) for more information on how everything is configured.

## Usage

| Keyboard Shortcut | Function                                      |
| ----------------- | --------------------------------------------- |
| M                 | Capture and visualise sound from microphone   |
| Q                 | Restart playback                              |
| F                 | Render with ffmpeg                            |


