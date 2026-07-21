# dvbipitools

Command line tools for DVB-IPI streams.

## Tools

* [dipirec](src/dipirec/README.md) - record a DVB-IPI multicast
* [dipiscan](src/dipiscan/README.md) - scan an IP range for DVB-IPI multicasts
* [dipiradiohead](src/dipiradiohead/README.md) - use public Ice-/Shoutcast radio sources for multicast distribution

## Build

Your choice. Go for the classics: `./configure --release && make` or use CMake:
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Options: 
* `-DCMAKE_BUILD_TYPE=Debug|Release` / `--debug|--release`
* `-DDVBIPITOOLS_STATIC=ON` / `--static` for a static link

## Packaging
```sh
dpkg-buildpackage -b -us -uc
```
Build-Depends: `debhelper (>= 13)`, `cmake`, `libssl-dev`

## Licence

GPL-3.0-or-later. See `LICENSE` and `NOTICE`.
