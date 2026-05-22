param(
  [string]$Configuration = "RelWithDebInfo",
  [switch]$Package
)

$root_dir = (Get-Location).Path
$dir = $root_dir -replace '\\', '/'
$build_dir = Join-Path $root_dir "build"

if ($Configuration -eq "Debug") {
  Write-Warning "Dependencies are built as Release. Debug can trigger MSVC STL/CRT assertions; use RelWithDebInfo for local debugging."
}

New-Item -ItemType Directory -Force -Path $build_dir | Out-Null

cd $build_dir

cmake .. `
  -DCMAKE_PREFIX_PATH="C:/ProgramData/Qt/6.11.1/msvc2022_64" `
  -DZLIB_ROOT="$dir/build/depends/lib" `
  -DImath_DIR="$dir/build/depends/lib/lib/cmake/Imath" `
  -DOpenEXR_DIR="$dir/build/depends/lib/lib/cmake/OpenEXR" `
  -DCMAKE_INSTALL_PREFIX="$dir/build/install" `
  -DCMAKE_BUILD_TYPE="$Configuration" `
  -DCMAKE_CONFIGURATION_TYPES="$Configuration"

cmake --build . --config $Configuration

if ($Package) {
  if (!(Get-Command makensis -ErrorAction SilentlyContinue)) {
    Write-Error "NSIS is required for packaging. Install NSIS or run without -Package."
    cd $root_dir
    exit 1
  }

  cpack -C $Configuration
}

cd $root_dir
