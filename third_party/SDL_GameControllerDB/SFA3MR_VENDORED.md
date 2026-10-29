# SDL_GameControllerDB (vendored)

- Source: https://github.com/mdqinc/SDL_GameControllerDB, `gamecontrollerdb.txt` from master
  (downloaded for SFA3MR 1.1.3), unchanged.
- Licence: zlib (`LICENSE`).
- Use: SDL reads it at start for the pads it has no mapping of its own for (CMake copies it next to
  `sfa3mr.exe`; host_sdl.c). A `gamecontrollerdb.txt` in the work folder is read after it and wins.
- To update: replace `gamecontrollerdb.txt` with the current one from the source.
