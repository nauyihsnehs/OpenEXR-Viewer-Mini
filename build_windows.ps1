param(
  [string]$Configuration = "RelWithDebInfo",
  [switch]$Package,
  [switch]$Portable,
  [switch]$WithoutShell,
  [string]$QtPath = $env:QT_ROOT_DIR,
  [string]$Generator
)

function Invoke-Checked {
    param([string]$Program)
    & $Program @args
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}

function Read-BuildCache {
    param([string]$BuildDirectory)
    $cache = @{}
    $path = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (Test-Path -LiteralPath $path) {
        foreach ($line in Get-Content -LiteralPath $path -Encoding UTF8) {
            if ($line -match '^(CMAKE_GENERATOR(?:_PLATFORM|_TOOLSET|_INSTANCE)?):[^=]*=(.*)$') {
                $cache[$Matches[1]] = $Matches[2]
            }
        }
        if (!$cache.CMAKE_GENERATOR) { throw "Incomplete CMake cache: $path. Reconfigure this build directory." }
    }
    return $cache
}

function Get-WindowsBuildToolchain {
    param([string]$Root, [string]$Generator, [switch]$WithoutShell)
    $build = Join-Path $Root 'build'
    $directories = @($build)
    foreach ($name in 'zlib', 'Imath', 'openexr') {
        $directories += Join-Path $build "depends/shared-build/$name"
        if (!$WithoutShell) { $directories += Join-Path $build "depends/shell-build/$name" }
    }
    if (!$WithoutShell) { $directories += Join-Path $build 'shell-build' }
    $cached = @(foreach ($directory in $directories) {
        $cache = Read-BuildCache $directory
        if ($cache.Count) { [pscustomobject]@{ Directory = $directory; Values = $cache } }
    })

    $capabilities = (Invoke-Checked cmake -E capabilities) | ConvertFrom-Json
    $supported = @($capabilities.generators | ForEach-Object { $_.name })
    $instance = ''
    $toolset = ''
    if ($cached.Count) {
        if (!$Generator) { $Generator = $cached[0].Values.CMAKE_GENERATOR }
        $toolset = [string]$cached[0].Values.CMAKE_GENERATOR_TOOLSET
        foreach ($entry in $cached) {
            $values = $entry.Values
            if ($values.CMAKE_GENERATOR -ne $Generator -or
                [string]$values.CMAKE_GENERATOR_TOOLSET -ne $toolset) {
                throw "Toolchain conflict in $($entry.Directory): cached '$($values.CMAKE_GENERATOR)' / '$($values.CMAKE_GENERATOR_TOOLSET)', requested '$Generator' / '$toolset'. Reconfigure the affected viewer and dependency builds together; no files were removed."
            }
            $platform = [string]$values.CMAKE_GENERATOR_PLATFORM
            if ($platform -and $platform -notmatch '^x64(?:,|$)') {
                throw "The cache in $($entry.Directory) targets '$platform'. This script requires MSVC x64; reconfigure that build directory."
            }
            $cachedInstance = ([string]$values.CMAKE_GENERATOR_INSTANCE).Replace('\', '/').TrimEnd('/')
            if ($cachedInstance) {
                if ($instance -and $instance -ne $cachedInstance) {
                    throw "Visual Studio installation conflict in $($entry.Directory). Reconfigure the viewer and dependencies with the same installation."
                }
                $instance = $cachedInstance
            }
        }
    }
    if ($Generator -and $Generator -notin $supported) {
        throw "This CMake does not support '$Generator'. Update CMake or select a supported installed Visual Studio with -Generator."
    }
    if (!$cached.Count) {
        if ($Generator -and $Generator -notmatch '^Visual Studio \d+ ') {
            throw 'New Windows builds require a Visual Studio generator. Existing MSVC builds using other generators can still be reused.'
        }
        # vswhere excludes prerelease/incomplete installations by default and includes Build Tools.
        $command = Get-Command vswhere.exe -ErrorAction SilentlyContinue
        $vswhere = if ($command) { $command.Source } else {
            Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        }
        if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) {
            throw 'vswhere.exe was not found. Install Visual Studio or Build Tools with the C++ x64 tools.'
        }
        $encoding = [Console]::OutputEncoding
        try {
            [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
            $installations = (Invoke-Checked $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8) | ConvertFrom-Json
        } finally { [Console]::OutputEncoding = $encoding }
        foreach ($installation in ($installations | Sort-Object { [version]$_.installationVersion } -Descending)) {
            $major = ([version]$installation.installationVersion).Major
            $match = $supported | Where-Object { $_ -match "^Visual Studio $major " } | Select-Object -First 1
            if ($match -and (!$Generator -or $Generator -eq $match)) {
                $Generator = $match
                $instance = $installation.installationPath
                break
            }
        }
        if (!$instance) {
            throw "No installed C++ Visual Studio matches this CMake and generator '$Generator'. Update CMake for newer Visual Studio, or install the required C++ build tools."
        }
    }
    Write-Host "Windows build: $Generator; target MSVC x64; existing cache settings are preserved."
    return [pscustomobject]@{
        Generator = $Generator
        Instance = $instance
        Toolset = $toolset
        CMakeVersion = $capabilities.version.string
    }
}

function Invoke-WindowsConfigure {
    param([string]$Source, [string]$BuildDirectory, $Toolchain, [string[]]$Options)
    $cache = Read-BuildCache $BuildDirectory
    $arguments = @('-S', $Source, '-B', $BuildDirectory, '-G', $Toolchain.Generator)
    if ($cache.Count) {
        # In particular, an empty cached platform must not become an explicit -A x64.
        if ($cache.CMAKE_GENERATOR_PLATFORM) { $arguments += @('-A', $cache.CMAKE_GENERATOR_PLATFORM) }
        if ($cache.CMAKE_GENERATOR_TOOLSET) { $arguments += @('-T', $cache.CMAKE_GENERATOR_TOOLSET) }
    } elseif ($Toolchain.Generator -match '^Visual Studio \d+ ') {
        $arguments += @('-A', 'x64')
        if ($Toolchain.Toolset) { $arguments += @('-T', $Toolchain.Toolset) }
        if ($Toolchain.Instance) { $arguments += "-DCMAKE_GENERATOR_INSTANCE=$($Toolchain.Instance)" }
    }
    Invoke-Checked cmake @arguments @Options

    # CMake's detected compiler metadata also verifies old caches with an implicit platform.
    $found = $false
    foreach ($language in 'C', 'CXX') {
        $metadata = Join-Path $BuildDirectory "CMakeFiles/$($Toolchain.CMakeVersion)/CMake${language}Compiler.cmake"
        if (Test-Path -LiteralPath $metadata) {
            $found = $true
            $contents = Get-Content -LiteralPath $metadata -Raw -Encoding UTF8
            if ($contents -notmatch ('set\(CMAKE_{0}_COMPILER_ID "MSVC"\)' -f $language) -or
                $contents -notmatch ('set\(CMAKE_{0}_SIZEOF_DATA_PTR "?8"?\)' -f $language)) {
                throw "The compiler detected in $BuildDirectory is not MSVC x64. Select an MSVC x64 environment and reconfigure; the build was not started."
            }
        }
    }
    if (!$found) { throw "CMake compiler metadata is missing in $BuildDirectory; cannot verify MSVC x64." }
}

function Get-DependencySources {
    param([string]$Root, [object[]]$Dependencies)
    $sources = Join-Path $Root 'dependencies'
    New-Item -ItemType Directory -Force -Path $sources | Out-Null
    foreach ($dependency in $Dependencies) {
        $source = Join-Path $sources $dependency.Name
        if (!(Test-Path -LiteralPath $source)) {
            Invoke-Checked git clone --no-checkout $dependency.Url $source
            Invoke-Checked git -C $source checkout --detach $dependency.Ref
        }
        if (!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt') -PathType Leaf)) {
            throw "Incomplete dependency source: $source. Restore this checkout before retrying; existing files were not reset."
        }
    }
}

function Build-Dependencies {
    param([string]$Root, $Toolchain, [object[]]$Dependencies, [switch]$Static)
    $kind = if ($Static) { 'shell' } else { 'shared' }
    $prefix = Join-Path $Root $(if ($Static) { 'build/depends/shell' } else { 'build/depends/lib' })
    $common = @("-DCMAKE_INSTALL_PREFIX=$prefix", '-DCMAKE_INSTALL_LIBDIR=lib',
        '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF')
    if ($Static) {
        $common += @('-DBUILD_SHARED_LIBS=OFF', '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW',
            '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
    } else { $common += '-DBUILD_SHARED_LIBS=ON' }
    foreach ($dependency in $Dependencies) {
        Write-Host "Building $kind dependency: $($dependency.Name)"
        $source = Join-Path $Root "dependencies/$($dependency.Name)"
        $directory = Join-Path $Root "build/depends/$kind-build/$($dependency.Name)"
        $options = $common + @()
        switch ($dependency.Name) {
            'zlib' {
                $shared = if ($Static) { 'OFF' } else { 'ON' }
                $staticLibrary = if ($Static) { 'ON' } else { 'OFF' }
                $options += @("-DZLIB_BUILD_SHARED=$shared", "-DZLIB_BUILD_STATIC=$staticLibrary",
                    '-DZLIB_BUILD_TESTING=OFF', '-DZLIB_BUILD_EXAMPLES=OFF')
            }
            'Imath' {
                if ($Static) { $options += @('-DIMATH_BUILD_PYTHON=OFF', '-DPYILMBASE_ENABLE=OFF') }
            }
            'openexr' {
                $options += @("-DCMAKE_PREFIX_PATH=$prefix", "-DZLIB_ROOT=$prefix", "-DImath_DIR=$prefix/lib/cmake/Imath")
                if ($Static) {
                    $zlib = @('zlibstatic.lib', 'zs.lib', 'z.lib', 'zlib.lib') |
                        ForEach-Object { Join-Path $prefix "lib/$_" } |
                        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
                    if (!$zlib) { throw 'Static zlib library was not installed.' }
                    $options += @("-DZLIB_LIBRARY=$zlib", "-DZLIB_LIBRARY_RELEASE=$zlib",
                        "-DZLIB_INCLUDE_DIR=$prefix/include", '-DOPENEXR_BUILD_TOOLS=OFF', '-DOPENEXR_BUILD_EXAMPLES=OFF')
                }
            }
        }
        Invoke-WindowsConfigure -Source $source -BuildDirectory $directory -Toolchain $Toolchain -Options $options
        Invoke-Checked cmake --build $directory --config Release --target install
    }
    if ($Static) {
        $licenses = Join-Path $prefix 'licenses'
        New-Item -ItemType Directory -Force -Path $licenses | Out-Null
        foreach ($name in 'zlib', 'Imath', 'openexr') {
            $license = if ($name -eq 'zlib') { 'README' } else { 'LICENSE.md' }
            $label = if ($name -eq 'openexr') { 'OpenEXR' } else { $name }
            Copy-Item -LiteralPath (Join-Path $Root "dependencies/$name/$license") -Destination (Join-Path $licenses "$label-LICENSE")
        }
    }
}

$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'This script requires Windows.' }
if ($Package -and $Portable) { throw '-Package and -Portable cannot be used together.' }
$programs = @('git', 'cmake')
if ($Package -or $Portable) { $programs += 'cpack' }
if ($Package) { $programs += 'makensis' }
foreach ($program in $programs) {
    if (!(Get-Command $program -CommandType Application -ErrorAction SilentlyContinue)) {
        throw "Required tool not found: $program. Install it and add it to PATH."
    }
}
if (!$QtPath) { $QtPath = 'C:/ProgramData/Qt/6.11.1/msvc2022_64' }
if (!(Test-Path -LiteralPath $QtPath -PathType Container)) {
    throw "Qt prefix not found: $QtPath. Pass -QtPath or set QT_ROOT_DIR."
}
if ($Configuration -eq 'Debug') {
    Write-Warning 'Dependencies are built as Release. Debug can trigger MSVC STL/CRT assertions; use RelWithDebInfo for local debugging.'
}
$root_dir = $PSScriptRoot
$build_dir = Join-Path $root_dir 'build'
$depends_dir = Join-Path $build_dir 'depends/lib'
$shell_enabled = if ($WithoutShell) { 'OFF' } else { 'ON' }
$toolchain = Get-WindowsBuildToolchain -Root $root_dir -Generator $Generator -WithoutShell:$WithoutShell
$dependencies = @(
    @{ Name = 'zlib'; Url = 'https://github.com/madler/zlib.git'; Ref = 'f9dd6009be3ed32415edf1e89d1bc38380ecb95d' },
    @{ Name = 'Imath'; Url = 'https://github.com/AcademySoftwareFoundation/Imath.git'; Ref = 'v3.0.1' },
    @{ Name = 'openexr'; Url = 'https://github.com/AcademySoftwareFoundation/openexr.git'; Ref = 'v3.0.1' }
)
Get-DependencySources -Root $root_dir -Dependencies $dependencies
Build-Dependencies -Root $root_dir -Toolchain $toolchain -Dependencies $dependencies
if (!$WithoutShell) { Build-Dependencies -Root $root_dir -Toolchain $toolchain -Dependencies $dependencies -Static }

$options = @(
  "-DCMAKE_PREFIX_PATH=$QtPath",
  "-DZLIB_ROOT=$depends_dir",
  "-DImath_DIR=$depends_dir/lib/cmake/Imath",
  "-DOpenEXR_DIR=$depends_dir/lib/cmake/OpenEXR",
  "-DCMAKE_INSTALL_PREFIX=$build_dir/install",
  "-DBUILD_WINDOWS_SHELL=$shell_enabled",
  "-DOPENEXR_SHELL_DEPENDENCIES=$build_dir/depends/shell",
  "-DCMAKE_BUILD_TYPE=$Configuration",
  "-DCMAKE_CONFIGURATION_TYPES=$Configuration"
)
Invoke-WindowsConfigure -Source $root_dir -BuildDirectory $build_dir -Toolchain $toolchain -Options $options
Invoke-Checked cmake --build $build_dir --config $Configuration

if ($Package -or $Portable) {
  $packageGenerator = if ($Portable) { 'ZIP' } else { 'NSIS' }
  Invoke-Checked cpack --config (Join-Path $build_dir 'CPackConfig.cmake') `
    -B $build_dir -G $packageGenerator -C $Configuration
}

$executable = Join-Path $build_dir "$Configuration/openexr-viewer.exe"
if (!(Test-Path -LiteralPath $executable -PathType Leaf)) { $executable = Join-Path $build_dir 'openexr-viewer.exe' }
Write-Host "Executable: $executable"
if ($Package -or $Portable) { Write-Host "Packages: $build_dir" }
