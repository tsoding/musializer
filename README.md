# Musializer

**THIS SOFTWARE IS UNFINISHED!!! Don't have any high expectations. Read [CONTRIBUTING.md](CONTRIBUTING.md) if you have a strong irresistable desire to contribute something here.**

https://github.com/tsoding/musializer/assets/165283/c97f8deb-52fb-422d-bcd7-964f9c2cfc78

*Music by [@nu11](https://soundcloud.com/nu11_ft) from [https://soundcloud.com/nu11_ft/nu11-wip-works-2016-2022](https://soundcloud.com/nu11_ft/nu11-wip-works-2016-2022) at 20:38*

## Quick Start

Dependencies:
- [raylib](https://www.raylib.com/) and all its transitive dependencies.

*Only Linux is supported for now. Windows soon.*

```console
$ ./build.sh
$ ./build/musializer
```

## Hot Reloading

<!--
TODO: Use rpath to eliminate the need for LD_LIBRARY_PATH
- https://en.wikipedia.org/wiki/Rpath
-->

```console
$ export HOTRELOAD=1
$ export LD_LIBRARY_PATH="./build/:$LD_LIBRARY_PATH"
$ ./build.sh
$ ./build/musializer
```

Keep the app running. Rebuild with `./build.sh`. Hot reload by focusing on the window of the app and pressing <kbd>r</kbd>.
