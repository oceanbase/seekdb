# wepoll

Windows-only epoll implementation backed by AFD/IOCP. Used by OceanBase on
Windows to provide the `epoll_*` API surface that `ob_sql_nio.cpp` and friends
rely on.

## Vendoring

Two files are expected here:

- `wepoll.h`
- `wepoll.c`

Drop the latest tagged release from
<https://github.com/piscisaureus/wepoll> (BSD-2-Clause) into this directory.
Do not modify; treat as upstream.

## Build

`CMakeLists.txt` in this directory builds a static `wepoll` library on Windows
only. It fails fast at configure time if the sources are missing so we do not
silently link an empty stub.

Enable use of wepoll across the tree by passing `-DOB_USE_WEPOLL=ON` to CMake.
When unset, the legacy `WSAPoll`-based shim in `ob_sql_nio.cpp` remains the
default so we can A/B compare.

## License

BSD-2-Clause. Record the version drop here when updating:

| Version | Date | Notes |
|---------|------|-------|
|         |      |       |
