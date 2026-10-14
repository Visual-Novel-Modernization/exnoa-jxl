# EXNOA Engine JPEG XL Plugin

A source-only 32-bit Windows image plugin for the EXNOA engine's image-loader interface. It adds JPEG XL decoding to the PNG loader slot while preserving the engine's existing PNG implementation as a fallback. This plugin has been tested with one EXNOA engine game; the tested title and build identifiers are intentionally not published here.

## Interface and behavior

The engine discovers image plugins through the `GetExec`, `GetImageInfo`, and `GetImage` exports. This implementation registers `.png` because converted archive entries keep their original extension. It checks the payload signature before decoding; ordinary PNG data is forwarded to the preserved original PNG loader.

JPEG XL images are decoded to 8-bit BGRA, returned bottom-up with metadata `[width, height, 4]`, and allocated using Windows local-memory handles. The fallback module is loaded lazily from the plugin's own directory. Its entry points are resolved using the loaded module base plus verified RVAs, so ASLR remains enabled. The plugin does not patch engine code.

## Build

Use an i686 MinGW-w64 C++17 toolchain with compatible static libjxl, Brotli, Highway, and winpthread libraries. Pass the toolchain and output directories explicitly:

```powershell
./build.ps1 -ToolchainRoot $env:MINGW32_ROOT -OutDir $env:JXL_BUILD
```

The build produces `LoaderJxl.dll` and `hook_selftest.exe`. The pthread runtime is linked statically. The DLL imports only Windows `KERNEL32.dll` and `msvcrt.dll`; no MinGW/MSYS runtime DLL is needed on the target system. Keep generated binaries outside the repository.

## Isolated test

Use a fixture image you control:

```powershell
& "$env:JXL_BIN/cjxl.exe" ./fixture.png ./fixture.jxl -q 90 -e 8 --alpha_distance=0 --num_threads=6
& "$env:JXL_BUILD/hook_selftest.exe" "$env:JXL_BUILD/LoaderJxl.dll" ./fixture.jxl
```

The self-test loads the built plugin and verifies its exported decoder interface, dimensions, metadata, and bottom-up BGRA output. Synthetic testing does not replace manual rendering checks in the target application.

## Archive utility

`tools/pipeline.py` reads the tested engine's indexed ARC container format. It can extract entries, convert eligible RGB/RGBA PNGs, rebuild an archive, and check index and payload bounds. Original archives are read-only inputs; choose separate working and output directories using command-line arguments or environment variables.

```powershell
python tools/pipeline.py extract "$env:ENGINE_ARCHIVE" "$env:WORK_DIR/extracted"
python tools/pipeline.py convert "$env:WORK_DIR/extracted" "$env:WORK_DIR/convert" --bin "$env:JXL_BIN" --threads 6
python tools/pipeline.py build "$env:WORK_DIR/extracted" "$env:ENGINE_ARCHIVE" "$env:WORK_DIR/convert" "$env:OUTPUT_ARCHIVE"
python tools/pipeline.py verify "$env:OUTPUT_ARCHIVE"
```

Conversion uses quality 90, effort 8, and alpha distance 0. It retains a JXL candidate only when smaller than its PNG source and verifies decoded dimensions and alpha exactly. It processes files serially with at most six codec threads. Only ordinary RGB/RGBA PNGs are candidates; names suggesting fonts, rules, masks, labels, or thumbnails are excluded. Audio, video, scripts, JSON, and unknown formats are preserved.

## Install, test, and restore

For the tested loader configuration, install the built plugin as the configured PNG loader and preserve the original loader beside it as `LoaderPng.original.dll`. The replacement forwards non-JXL images to that backup. No change to the engine executable is made. Preserve original archives before installing converted outputs.

Manually test image loading, transparency, menus, transitions, save/load, and ordinary PNG fallback. To restore, remove the replacement, restore the saved original PNG loader and original archives, then remove any plugin runtime files that are not part of the original installation. Never overwrite or delete the only original copy.

## Limitations and licensing

Animated JXL, non-identity orientation, and unsupported extra channels are rejected. Color-profile equivalence is not guaranteed. The self-test cannot establish visual correctness for every application state; live rendering was manually tested on one EXNOA engine game.

Third-party license notices are in `LICENSES/`. Do not add application assets, archives, extracted scripts, binaries, local configuration, runtime reports, credentials, caches, or machine-specific paths to this repository.
