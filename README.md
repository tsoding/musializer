# Musializer

**THIS SOFTWARE IS UNFINISHED!!! Don't have any high expectations. Read [CONTRIBUTING.md](CONTRIBUTING.md) if you have a strong irresistable desire to contribute something here.**

The project aims to make a tool for creating beautiful music visualizations and rendering high quality videos of them.

## Demo

*Music by [@nu11](https://soundcloud.com/nu11_ft) from [https://soundcloud.com/nu11_ft/nu11-wip-works-2016-2022](https://soundcloud.com/nu11_ft/nu11-wip-works-2016-2022) at 20:38*

https://github.com/tsoding/musializer/assets/165283/8b9f9653-9b3d-4c04-9569-338fa19af071

## Quick Start

**Only Linux is supported for now. Windows soon.**

Dependencies:
- [raylib](https://www.raylib.com/) and all its transitive dependencies.
- [ffmpeg](https://ffmpeg.org/) executable available in `PATH` environment variable. (it is called as a child process)

```console
$ ./build.sh
$ ./build/musializer
```

Keep in mind that the application needs [./resources/](./resources/) to be present in the folder it is ran from.

## Hot Reloading

```console
$ export HOTRELOAD=1
$ ./build.sh
$ ./build/musializer
```

Keep the app running. Rebuild with `./build.sh`. Hot reload by focusing on the window of the app and pressing <kbd>r</kbd>.

The way it works is by putting the majority of the logic of the application into a `libplug` dynamic library and just reloading it when requested. The [rpath](https://en.wikipedia.org/wiki/Rpath) (aka hard-coded run-time search path) for that library is set to `.` and `./build/`. See [build.sh](./build.sh) for more information on how everything is configured.
