# Run after build_dependencies.ps1 has acquired the dependency sources.
# Builds a separate /MT static dependency prefix for the native Shell DLL.
$ErrorActionPreference = 'Stop'
$shellPrefix = Join-Path $PSScriptRoot 'build/depends/shell'
$shellSources = Join-Path $PSScriptRoot 'build/depends/src'
$shellBuild = Join-Path $PSScriptRoot 'build/depends/shell-build'

function Invoke-ShellCMake {
    & cmake @args
    if ($LASTEXITCODE -ne 0) { throw "Shell dependency CMake command failed with exit code $LASTEXITCODE." }
}

foreach ($dependency in @('zlib', 'Imath', 'openexr')) {
    if (!(Test-Path -LiteralPath (Join-Path $shellSources "$dependency/CMakeLists.txt"))) {
        throw "Missing $dependency sources. Run build_dependencies.ps1 first."
    }
}

$common = @('-A', 'x64', "-DCMAKE_INSTALL_PREFIX=$shellPrefix",
    '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
    '-DBUILD_SHARED_LIBS=OFF', '-DBUILD_TESTING=OFF', '-DCMAKE_INSTALL_LIBDIR=lib')
Invoke-ShellCMake -S (Join-Path $shellSources 'zlib') -B (Join-Path $shellBuild 'zlib') @common `
    -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=OFF -DZLIB_BUILD_EXAMPLES=OFF
Invoke-ShellCMake --build (Join-Path $shellBuild 'zlib') --config Release --target install

$staticZlib = @('zlibstatic.lib', 'zs.lib', 'z.lib', 'zlib.lib') |
    ForEach-Object { Join-Path $shellPrefix "lib/$_" } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (!$staticZlib) { throw 'Static zlib library was not installed.' }

Invoke-ShellCMake -S (Join-Path $shellSources 'Imath') -B (Join-Path $shellBuild 'Imath') @common `
    -DIMATH_BUILD_PYTHON=OFF -DPYILMBASE_ENABLE=OFF
Invoke-ShellCMake --build (Join-Path $shellBuild 'Imath') --config Release --target install

Invoke-ShellCMake -S (Join-Path $shellSources 'openexr') -B (Join-Path $shellBuild 'openexr') @common `
    "-DCMAKE_PREFIX_PATH=$shellPrefix" "-DImath_DIR=$shellPrefix/lib/cmake/Imath" `
    "-DZLIB_ROOT=$shellPrefix" "-DZLIB_LIBRARY=$staticZlib" "-DZLIB_LIBRARY_RELEASE=$staticZlib" `
    "-DZLIB_INCLUDE_DIR=$shellPrefix/include" -DOPENEXR_BUILD_TOOLS=OFF -DOPENEXR_BUILD_EXAMPLES=OFF
Invoke-ShellCMake --build (Join-Path $shellBuild 'openexr') --config Release --target install

$licenseDir = Join-Path $shellPrefix 'licenses'
New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
Copy-Item -LiteralPath (Join-Path $shellSources 'Imath/LICENSE.md') -Destination (Join-Path $licenseDir 'Imath-LICENSE')
Copy-Item -LiteralPath (Join-Path $shellSources 'openexr/LICENSE.md') -Destination (Join-Path $licenseDir 'OpenEXR-LICENSE')
# zlib's README includes its license, including on older source revisions.
Copy-Item -LiteralPath (Join-Path $shellSources 'zlib/README') -Destination (Join-Path $licenseDir 'zlib-LICENSE')
