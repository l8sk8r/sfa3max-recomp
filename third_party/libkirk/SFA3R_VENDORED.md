# Vendored: libkirk (KIRK, the PSP's crypto engine)

- Source: PPSSPP `ext/libkirk` (https://github.com/hrydgard/ppsspp), master at commit
  aa9dfa85f6d69479527980e038971ebf6d2682e5 (2026-10-26). Files copied unchanged: `kirk_engine.c/h`,
  `kirk_common.h`, `AES.c/h`, `SHA1.c/h`, `bn.c`, `ec.c`.
- Origin: "KIRK-ENGINE" by Draan and the PSP scene (see `kirk_engine.c`).
- Licence: **GNU GPL 3.0 or later** (see the file headers), the same as this project.
- Used by `src/runtime/psp_savecrypt.c` to encrypt and decrypt PSP save data for OPTION MODE >
  SAVE LOAD > IMPORT / EXPORT. That file's save data and PARAM.SFO code is ported from PPSSPP's
  `Core/HLE/sceChnnlsv.cpp`, `Core/Dialog/SavedataParam.cpp` and `Core/ELF/ParamSFO.cpp`
  (GPL 2.0 or later).
