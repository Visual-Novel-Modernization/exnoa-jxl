param(
  [Parameter(Mandatory = $true)][string]$ToolchainRoot,
  [string]$OutDir = (Join-Path $PSScriptRoot 'bin')
)
$ErrorActionPreference = 'Stop'
$gxx = Join-Path $ToolchainRoot 'bin/g++.exe'
$inc = Join-Path $ToolchainRoot 'include'
$lib = Join-Path $ToolchainRoot 'lib'
foreach ($required in @($gxx, (Join-Path $inc 'jxl/decode.h'), (Join-Path $lib 'libjxl.a'), (Join-Path $lib 'libwinpthread.a'))) {
  if (-not (Test-Path -LiteralPath $required)) { throw "Missing prerequisite: $required" }
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
# Link codecs and pthread runtime statically.
$common = @('-m32', '-O2', '-std=gnu++17', '-DJXL_STATIC_DEFINE', '-I', $inc, '-L', $lib,
  "-ffile-prefix-map=$PSScriptRoot=.", "-ffile-prefix-map=$ToolchainRoot=toolchain",
  '-Wl,-Bstatic', '-ljxl', '-ljxl_cms', '-lbrotlidec', '-lbrotlicommon', '-lhwy', '-Wl,--whole-archive', (Join-Path $lib 'libwinpthread.a'), '-Wl,--no-whole-archive', '-lstdc++', '-lgcc',
  '-Wl,-Bdynamic', '-lcrypt32', '-ladvapi32', '-luser32', '-lkernel32')
& $gxx '-shared' '-s' '-static-libgcc' '-static-libstdc++' '-Wl,--kill-at,--exclude-all-symbols' (Join-Path $PSScriptRoot 'hook.cpp') (Join-Path $PSScriptRoot 'hook.def') '-o' (Join-Path $OutDir 'LoaderJxl.dll') @common
if ($LASTEXITCODE -ne 0) { throw 'Hook DLL build failed' }
& $gxx '-municode' '-s' '-static-libgcc' '-static-libstdc++' (Join-Path $PSScriptRoot 'hook_selftest.cpp') '-o' (Join-Path $OutDir 'hook_selftest.exe') @common
if ($LASTEXITCODE -ne 0) { throw 'Selftest build failed' }
Get-Item (Join-Path $OutDir 'LoaderJxl.dll'), (Join-Path $OutDir 'hook_selftest.exe') | Select-Object Name, Length
