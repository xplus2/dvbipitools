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
* `-DDIPIRADIOHEAD_TLS=OFF` to build dipiradiohead without HTTPS support (no OpenSSL needed);
  default is `ON`, auto-falls back to off if OpenSSL isn't found (same via the legacy Makefile,
  detected through `pkg-config`)

## Packaging
```sh
dpkg-buildpackage -b -us -uc
```
Build-Depends: `debhelper (>= 13)`, `cmake`, `libssl-dev` (`libssl-dev` is only needed for
dipiradiohead's HTTPS support; dipirec/dipiscan don't use it)

## Licence

GPL-3.0-or-later. See `LICENSE` and `NOTICE`.
