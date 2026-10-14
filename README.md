# EXNOA Engine JPEG XL Plugin

A 32-bit Windows image plugin for the EXNOA engine. It decodes JPEG XL data through the PNG loader interface and sends ordinary PNG data to the original PNG loader. Tested in an EXNOA engine game.

## Image loading

The engine calls `GetExec`, `GetImageInfo`, and `GetImage` to select and use image plugins. This plugin reports `.png`, checks each payload signature, and decodes JPEG XL images to 8-bit BGRA. Pixel rows are returned bottom-up with metadata `[width, height, 4]` in Windows local-memory handles.

The original PNG loader is loaded on first fallback from the same directory as the plugin. Its addresses are computed from its loaded module base, so ASLR stays enabled. The engine executable is not patched.

## Build

Use an i686 MinGW-w64 C++17 toolchain with static libjxl, Brotli, Highway, and winpthread libraries:

```powershell
./build.ps1 -ToolchainRoot $env:MINGW32_ROOT -OutDir $env:JXL_BUILD
```

The build creates `LoaderJxl.dll` and `hook_selftest.exe`. The pthread runtime is static; the plugin imports only `KERNEL32.dll` and `msvcrt.dll`.

## Test

Create a fixture and encode it with cjxl:

```powershell
& "$env:JXL_BIN/cjxl.exe" ./fixture.png ./fixture.jxl -q 90 -e 8 --alpha_distance=0 --num_threads=6
& "$env:JXL_BUILD/hook_selftest.exe" "$env:JXL_BUILD/LoaderJxl.dll" ./fixture.jxl
```

The self-test loads the plugin and checks its exports, image dimensions, metadata, and bottom-up BGRA output.

## Archive tool

`tools/pipeline.py` extracts, converts, rebuilds, and verifies indexed ARC archives. Pass source, work, output, and codec locations as arguments or environment variables:

```powershell
python tools/pipeline.py extract "$env:ENGINE_ARCHIVE" "$env:WORK_DIR/extracted"
python tools/pipeline.py convert "$env:WORK_DIR/extracted" "$env:WORK_DIR/convert" --bin "$env:JXL_BIN" --threads 6
python tools/pipeline.py build "$env:WORK_DIR/extracted" "$env:ENGINE_ARCHIVE" "$env:WORK_DIR/convert" "$env:OUTPUT_ARCHIVE"
python tools/pipeline.py verify "$env:OUTPUT_ARCHIVE"
```

Conversion uses quality 90, effort 8, alpha distance 0, and up to six codec threads. Files are processed one at a time. A JXL candidate replaces a PNG only when it is smaller and its decoded dimensions and alpha match. Only RGB/RGBA PNGs are considered; font, rule, mask, label, and thumbnail images are skipped. Other formats are copied unchanged. Source archives are read-only inputs.

## Installation and recovery

Use the filename expected by the engine's PNG loader configuration. Keep the original PNG loader beside the plugin as `LoaderPng.original.dll`; the plugin forwards non-JXL data to it. Back up the original archives before installing converted archives.

To restore, replace the plugin with the saved PNG loader and restore the original archives. The plugin has been tested in an EXNOA engine game.

Animated JPEG XL, non-identity orientation, and unsupported extra channels are not supported. Color-profile equivalence is not guaranteed. Third-party license notices are in `LICENSES/`.
