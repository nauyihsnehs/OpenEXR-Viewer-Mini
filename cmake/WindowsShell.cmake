if(CMAKE_VERSION VERSION_LESS 3.18)
    message(FATAL_ERROR "Windows Shell integration requires CMake 3.18 or later.")
endif()
include(ExternalProject)
set(OPENEXR_SHELL_DEPENDENCIES "${PROJECT_SOURCE_DIR}/build/depends/shell" CACHE PATH "Static x64 Shell dependency prefix")
set(_shell_stage "${CMAKE_BINARY_DIR}/shell-install")
# Preserve a child cache's platform, including an empty value. ExternalProject's
# default configure command would instead pass the parent's explicit -A x64.
set(_shell_cache "${CMAKE_BINARY_DIR}/shell-build/CMakeCache.txt")
foreach(_setting PLATFORM TOOLSET INSTANCE)
    set(_shell_${_setting} "${CMAKE_GENERATOR_${_setting}}")
    if(EXISTS "${_shell_cache}")
        file(STRINGS "${_shell_cache}" _cached_setting REGEX "^CMAKE_GENERATOR_${_setting}:[^=]*=")
        string(REGEX REPLACE "^[^=]*=" "" _shell_${_setting} "${_cached_setting}")
    endif()
endforeach()
set(_shell_configure "${CMAKE_COMMAND}" -S <SOURCE_DIR> -B <BINARY_DIR> -G "${CMAKE_GENERATOR}")
if(_shell_PLATFORM)
    list(APPEND _shell_configure -A "${_shell_PLATFORM}")
endif()
if(_shell_TOOLSET)
    list(APPEND _shell_configure -T "${_shell_TOOLSET}")
endif()
if(_shell_INSTANCE)
    list(APPEND _shell_configure "-DCMAKE_GENERATOR_INSTANCE:INTERNAL=${_shell_INSTANCE}")
endif()
# Separate configuration avoids collisions with the viewer's shared OpenEXR targets.
ExternalProject_Add(windows-shell
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/src/shell/windows"
    BINARY_DIR "${CMAKE_BINARY_DIR}/shell-build"
    CONFIGURE_COMMAND ${_shell_configure}
        "-DSHELL_DEPENDENCIES:PATH=${OPENEXR_SHELL_DEPENDENCIES}"
        "-DCMAKE_INSTALL_PREFIX:PATH=${_shell_stage}"
        "-DCMAKE_BUILD_TYPE:STRING=Release"
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release
    INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release
    BUILD_ALWAYS TRUE)
add_dependencies(openexr-viewer windows-shell)
add_custom_target(windows-shell-deploy ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${_shell_stage}/openexr-shell.dll" "${_shell_stage}/openexr-shell-config.exe"
        "$<TARGET_FILE_DIR:openexr-viewer>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${_shell_stage}/shell-licenses" "$<TARGET_FILE_DIR:openexr-viewer>/shell-licenses"
    DEPENDS windows-shell openexr-viewer)
install(DIRECTORY "${_shell_stage}/" DESTINATION "${CMAKE_INSTALL_BINDIR}")
