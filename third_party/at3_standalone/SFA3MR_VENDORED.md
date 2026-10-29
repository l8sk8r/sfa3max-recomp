# Vendored: ATRAC3 / ATRAC3+ decoder

- Source: PPSSPP `ext/at3_standalone` (https://github.com/hrydgard/ppsspp), commit
  53a71276186f63cf1605823099be728eef982a59 (2026-10-24), files copied unchanged.
- Origin: FFmpeg's atrac3 / atrac3plus decoders, extracted to be standalone.
- Licence: **GNU LGPL 2.1 or later** (see the headers of each file), compatible with this project's
  GPL-3.0-or-later.
- `shim/` holds three small headers (written for SFA3MR) that replace the PPSSPP headers the decoder
  includes: `ppsspp_config.h`, `Common/Log.h`, `Common/MemoryUtil.h`.
