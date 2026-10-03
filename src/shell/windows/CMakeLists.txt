cmake_minimum_required(VERSION 3.18)
project(openexr-shell LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Shell components require Windows x64 and MSVC.")
endif()
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
get_filename_component(VIEWER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

if(NOT SHELL_DEPENDENCIES)
    message(FATAL_ERROR "Set SHELL_DEPENDENCIES to the static dependency prefix prepared by build_windows.ps1.")
endif()
set(CMAKE_PREFIX_PATH "${SHELL_DEPENDENCIES}")
set(Imath_DIR "${SHELL_DEPENDENCIES}/lib/cmake/Imath")
set(OpenEXR_DIR "${SHELL_DEPENDENCIES}/lib/cmake/OpenEXR")
set(ZLIB_ROOT "${SHELL_DEPENDENCIES}")
find_library(SHELL_ZLIB NAMES zlibstatic zs z zlib PATHS "${SHELL_DEPENDENCIES}/lib" NO_DEFAULT_PATH REQUIRED)
set(ZLIB_LIBRARY "${SHELL_ZLIB}" CACHE FILEPATH "Static zlib" FORCE)
set(ZLIB_LIBRARY_RELEASE "${SHELL_ZLIB}" CACHE FILEPATH "Static zlib" FORCE)
set(ZLIB_INCLUDE_DIR "${SHELL_DEPENDENCIES}/include" CACHE PATH "zlib headers" FORCE)
find_package(Imath CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)
foreach(dependency Imath::Imath OpenEXR::OpenEXR OpenEXR::Iex OpenEXR::IlmThread)
    get_target_property(kind ${dependency} TYPE)
    if(NOT kind STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "${dependency} must be static; do not use the viewer's shared dependencies for the Shell build.")
    endif()
endforeach()

configure_file(Config.rc.in Config.rc @ONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Shell.rc" "1 ICON \"${VIEWER_ROOT}/deploy/icon/openexr-viewer.ico\"\n")
add_library(openexr-shell SHARED ShellHandlers.cpp ExrPreview.cpp Shell.def
    "${CMAKE_CURRENT_BINARY_DIR}/Shell.rc"
    "${VIEWER_ROOT}/src/util/ColorTransform.cpp"
    "${VIEWER_ROOT}/src/model/framebuffer/ToneMapping.cpp")
target_link_libraries(openexr-shell PRIVATE OpenEXR::OpenEXR Imath::Imath ole32 uuid user32 gdi32 msimg32)
target_include_directories(openexr-shell PRIVATE "${VIEWER_ROOT}/src")

add_executable(openexr-shell-config WIN32 Config.cpp Registration.cpp "${CMAKE_CURRENT_BINARY_DIR}/Config.rc")
target_include_directories(openexr-shell-config PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(openexr-shell-config PRIVATE ole32 uuid shell32 shlwapi advapi32 user32)
# The resource supplies the complete asInvoker/DPI manifest.
target_link_options(openexr-shell-config PRIVATE /MANIFEST:NO)
foreach(target openexr-shell openexr-shell-config)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
endforeach()
install(TARGETS openexr-shell openexr-shell-config RUNTIME DESTINATION . LIBRARY DESTINATION .)
install(FILES "${SHELL_DEPENDENCIES}/licenses/Imath-LICENSE"
    "${SHELL_DEPENDENCIES}/licenses/OpenEXR-LICENSE" "${SHELL_DEPENDENCIES}/licenses/zlib-LICENSE"
    DESTINATION shell-licenses)
