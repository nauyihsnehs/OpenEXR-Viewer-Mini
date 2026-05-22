$start_dir = $PWD
$depends_dir = Join-Path $start_dir "build/depends/lib"
$depends_dir = $depends_dir -replace '\\', '/'
$src_dir = Join-Path $start_dir "build/depends/src"

New-Item -ItemType Directory -Force -Path $depends_dir | Out-Null
New-Item -ItemType Directory -Force -Path $src_dir | Out-Null

cd $src_dir

if (!(Test-Path zlib)) {
  git clone https://github.com/madler/zlib.git
}

if (!(Test-Path Imath)) {
  git clone https://github.com/AcademySoftwareFoundation/Imath.git --branch v3.0.1
}

if (!(Test-Path openexr)) {
  git clone https://github.com/AcademySoftwareFoundation/openexr.git --branch v3.0.1
}


cd zlib
New-Item -ItemType Directory -Force -Path build | Out-Null
cd build
cmake .. -DCMAKE_INSTALL_PREFIX="$depends_dir"
cmake --build . --target install --config Release

cd ../..

cd Imath
New-Item -ItemType Directory -Force -Path build | Out-Null
cd build
cmake .. -DCMAKE_INSTALL_PREFIX="$depends_dir"

cmake --build . --target install --config Release

cd ../..

cd openexr
New-Item -ItemType Directory -Force -Path build | Out-Null
cd build
cmake .. `
  -DCMAKE_INSTALL_PREFIX="$depends_dir" `
  -DZLIB_ROOT="$depends_dir" `
  -DImath_DIR="$depends_dir/lib/cmake/Imath"

cmake --build . --target install --config Release

cd $start_dir
