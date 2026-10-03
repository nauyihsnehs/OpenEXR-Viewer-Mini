# The MIT License (MIT)
#
# Copyright (c) 2017 Nathan Osman
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

get_target_property(_qmake_executable Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}" REQUIRED)

function(windeployqt target directory)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --verbose 0
            --no-compiler-runtime --no-opengl-sw "$<TARGET_FILE:${target}>"
        VERBATIM)
    # CMake 3.18 supports generator expressions inside install(CODE).
    install(CODE "
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\" --verbose 0
                --no-compiler-runtime --no-opengl-sw
                --dir \"\${CMAKE_INSTALL_PREFIX}/${directory}\"
                \"$<TARGET_FILE:${target}>\"
            RESULT_VARIABLE _deploy_result)
        if(NOT _deploy_result EQUAL 0)
            message(FATAL_ERROR \"Qt deployment failed: \${_deploy_result}\")
        endif()
    ")
    # InstallRequiredSystemLibraries is configured by the root CMakeLists.txt.
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()
mark_as_advanced(WINDEPLOYQT_EXECUTABLE)
