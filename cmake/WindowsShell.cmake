if(CMAKE_VERSION VERSION_LESS 3.18)
    message(FATAL_ERROR "Windows Shell integration requires CMake 3.18 or later.")
endif()
include(ExternalProject)
set(OPENEXR_SHELL_DEPENDENCIES "${PROJECT_SOURCE_DIR}/build/depends/shell" CACHE PATH "Static x64 Shell dependency prefix")
set(_shell_stage "${CMAKE_BINARY_DIR}/shell-install")
# Separate configuration avoids collisions with the viewer's shared OpenEXR targets.
ExternalProject_Add(windows-shell
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/src/shell/windows"
    BINARY_DIR "${CMAKE_BINARY_DIR}/shell-build"
    CMAKE_ARGS
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
