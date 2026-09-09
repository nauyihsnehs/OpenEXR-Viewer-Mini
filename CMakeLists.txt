cmake_minimum_required(VERSION 3.5)

project(openexr-viewer
    VERSION 0.6.1
    DESCRIPTION "Simple Viewer for OpenEXR files with detailed metadata display"
    HOMEPAGE_URL "https://github.com/afichet/openexr-viewer"
    LANGUAGES CXX
    )

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

cmake_policy(SET CMP0074 NEW)

include(GNUInstallDirs)


if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)
    set(CMAKE_SKIP_BUILD_RPATH FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

    set(MACOSX_BUNDLE_BUNDLE_NAME "OpenEXR-Viewer")
    set(MACOSX_BUNDLE_CONTENTS "${CMAKE_CURRENT_BINARY_DIR}/${MACOSX_BUNDLE_BUNDLE_NAME}.app/Contents")
    set(MACOSX_BUNDLE_RESOUCES "${MACOSX_BUNDLE_CONTENTS}/Resources")
    set(MACOSX_BUNDLE_ICON_FILE "openexr-viewer.icns")
    set(MACOSX_BUNDLE_ICON "${PROJECT_SOURCE_DIR}/deploy/icon/${MACOSX_BUNDLE_ICON_FILE}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${MACOSX_BUNDLE_RESOUCES}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MACOSX_BUNDLE_ICON}" "${MACOSX_BUNDLE_RESOUCES}")
endif (APPLE)


# ----------------------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------------------

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets Concurrent REQUIRED)
find_package(
    Qt${QT_VERSION_MAJOR}
    COMPONENTS Widgets Concurrent
    REQUIRED
)

find_package(OpenMP)

# Those are provided by Imath and OpenEXR 3.0
find_package(Imath REQUIRED)
find_package(OpenEXR REQUIRED)

if (WIN32)
    find_file(OPENEXR_VIEWER_ZLIB_RUNTIME
        NAMES
            z.dll
            zlib.dll
            zd.dll
            zlibd.dll
        PATHS
            "${ZLIB_ROOT}/bin"
        NO_DEFAULT_PATH
    )

    if (NOT OPENEXR_VIEWER_ZLIB_RUNTIME)
        message(FATAL_ERROR "Could not find zlib runtime DLL in ${ZLIB_ROOT}/bin. Checked z.dll, zlib.dll, zd.dll, zlibd.dll.")
    endif ()
endif ()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

# ----------------------------------------------------------------------------
# Source
# ----------------------------------------------------------------------------
message(${CMAKE_PROJECT_VERSION})

configure_file(src/config.h.in config.h)

set(PROJECT_SOURCES
    src/main.cpp
    src/view/mainwindow.cpp
    src/view/mainwindow.h
    src/view/mainwindow.ui
    src/view/WorkspaceWidgets.cpp
    src/view/WorkspaceWidgets.h

    src/view/about.cpp
    src/view/about.h
    src/view/about.ui

    src/view/ImageFileWidget.cpp
    src/view/ImageFileWidget.h

    src/view/GraphicsView.cpp
    src/view/GraphicsView.h

    src/view/ComboBoxBehavior.cpp
    src/view/ComboBoxBehavior.h

    src/view/FramebufferInfo.h

    src/io/ImageSave.cpp
    src/io/ImageSave.h
    src/io/ImageSavePlan.cpp
    src/io/ImageSavePlan.h

    src/view/SaveImageDialog.cpp
    src/view/SaveImageDialog.h
    src/view/SaveImageDialog.ui

    src/view/RGBFramebufferWidget.cpp
    src/view/RGBFramebufferWidget.h
    src/view/RGBFramebufferWidget.ui

    src/view/YFramebufferWidget.cpp
    src/view/YFramebufferWidget.h
    src/view/YFramebufferWidget.ui

    src/view/ScaleWidget.cpp
    src/view/ScaleWidget.h
    src/view/RangeSliderWidget.cpp
    src/view/RangeSliderWidget.h

    # ------------------------------------------------------------------------
    # Model
    # ------------------------------------------------------------------------
    src/model/OpenEXRImage.cpp
    src/model/OpenEXRImage.h

    # OpenEXR attributes
    src/model/attribute/HeaderItem.cpp
    src/model/attribute/HeaderItem.h
    src/model/attribute/HeaderModel.cpp
    src/model/attribute/HeaderModelAttributes.cpp
    src/model/attribute/HeaderModel.h

    # OpenEXR layer fields (specific kind of attributes)
    src/model/attribute/LayerItem.cpp
    src/model/attribute/LayerItem.h
    src/model/attribute/LayerModel.cpp
    src/model/attribute/LayerModel.h

    # Framebuffers (image content i.e. layer data)
    src/model/framebuffer/FramebufferModel.cpp
    src/model/framebuffer/FramebufferModel.h
    src/model/framebuffer/FramebufferData.h
    src/model/framebuffer/FramebufferLoader.h
    src/model/framebuffer/FramebufferLoader.cpp
    src/model/framebuffer/YFramebufferModel.cpp
    src/model/framebuffer/YFramebufferModel.h
    src/model/framebuffer/RGBFramebufferModel.cpp
    src/model/framebuffer/RGBFramebufferModel.h
    src/model/framebuffer/ToneMapping.cpp
    src/model/framebuffer/ToneMapping.h

    # Stream
    src/model/StdIStream.cpp
    src/model/StdIStream.h

    # ------------------------------------------------------------------------
    # Utilities
    # ------------------------------------------------------------------------
    src/util/Colormap.cpp
    src/util/Colormap.h
    src/util/TabulatedColormap.cpp
    src/util/TabulatedColormap.h
    src/util/BBGRColormap.cpp
    src/util/BBGRColormap.h
    src/util/YColormap.cpp
    src/util/YColormap.h
    src/util/ColormapModule.h
    src/util/ColormapModule.cpp
    src/util/ColorTransform.h
    src/util/ColorTransform.cpp

    openexr-viewer.rc
    assets/themes/dark_flat.qrc
    assets/icons/icons.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    if (WIN32)
        qt_add_executable(openexr-viewer WIN32
            ${PROJECT_SOURCES}
        )
    elseif(APPLE)
        qt_add_executable(openexr-viewer MACOSX_BUNDLE
            ${PROJECT_SOURCES}
        )
    else()
        qt_add_executable(openexr-viewer
            ${PROJECT_SOURCES}
        )
    endif()
else()
    if(WIN32)
        add_executable(openexr-viewer WIN32
            ${PROJECT_SOURCES}
        )
    elseif(APPLE)
        add_executable(openexr-viewer MACOSX_BUNDLE
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(openexr-viewer
            ${PROJECT_SOURCES}
        )
    endif()
endif()


if (APPLE)
    set_target_properties(openexr-viewer PROPERTIES
        OUTPUT_NAME ${MACOSX_BUNDLE_BUNDLE_NAME}
        MACOSX_BUNDLE TRUE
        MACOSX_RPATH TRUE
        MACOSX_FRAMEWORK_IDENTIFIER com.gitlab.afichet.openexrviewer
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@loader_path/Libraries"
        RESOURCE "${RESOURCE_FILES}"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME TRUE
        XCODE_ATTRIBUTE_EXECUTABLE_NAME "exeName"
)

endif ()

target_include_directories(openexr-viewer PRIVATE src)
target_include_directories(openexr-viewer PRIVATE 3rdparty/data)
target_include_directories(openexr-viewer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(openexr-viewer PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_link_libraries(openexr-viewer PRIVATE Qt${QT_VERSION_MAJOR}::Concurrent)
target_link_libraries(openexr-viewer PRIVATE Imath::Imath OpenEXR::OpenEXR)

if((OpenMP_CXX_FOUND) OR (OpenMP_FOUND))
    target_link_libraries(openexr-viewer PRIVATE OpenMP::OpenMP_CXX)
    # target_link_libraries(openexr-viewer PRIVATE OpenMP::OpenMP_C)
endif()


if (WIN32)
    add_custom_command(
        TARGET openexr-viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Widgets>
        $<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Core>
        $<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Gui>
        "${OPENEXR_VIEWER_ZLIB_RUNTIME}"
        $<TARGET_FILE:Imath::Imath>
        $<TARGET_FILE:OpenEXR::Iex>
        $<TARGET_FILE:OpenEXR::IlmThread>
        $<TARGET_FILE:OpenEXR::OpenEXR>
        $<TARGET_FILE_DIR:openexr-viewer>
        )
endif (WIN32)


# Warnings
if(MSVC)
   target_compile_options(openexr-viewer PUBLIC /W4)
else()
   target_compile_options(openexr-viewer PUBLIC -Wall -Wextra -Wpedantic)
endif()


# ----------------------------------------------------------------------------
# Install Rules
# ----------------------------------------------------------------------------

if (UNIX AND NOT APPLE)
    install(TARGETS openexr-viewer)

    # Application desktop entry
    install(
        FILES
            deploy/linux/openexr-viewer.desktop
        DESTINATION
            share/applications
    )

    # MIME data
    install(
        FILES
            deploy/linux/openexr-viewer.xml
        DESTINATION
            share/mime/packages
    )

    # Application icon
    install(
        FILES
            deploy/icon/openexr-viewer.png
        DESTINATION
            share/pixmaps
    )

    # Metadata
    install(
        FILES
            deploy/linux/openexr-viewer.metainfo.xml
        DESTINATION
            share/metainfo
    )
elseif (APPLE)
    install(TARGETS openexr-viewer DESTINATION "." COMPONENT Libraries)
    include(Macdeployqt)
    macdeployqt(openexr-viewer)
elseif (WIN32)
    install(
        TARGETS
            openexr-viewer
        DESTINATION
            ${CMAKE_INSTALL_BINDIR}
    )

    install(
        FILES
            LICENSE
            README.md
        DESTINATION
            .
    )

    set(CMAKE_INSTALL_OPENMP_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)

    install(
        FILES
            "${OPENEXR_VIEWER_ZLIB_RUNTIME}"   # Zlib
            $<TARGET_FILE:Imath::Imath>         # OpenEXR
            $<TARGET_FILE:OpenEXR::Iex>
            $<TARGET_FILE:OpenEXR::IlmThread>
            $<TARGET_FILE:OpenEXR::OpenEXR>
            ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} # Windows DLLs
        DESTINATION
            ${CMAKE_INSTALL_BINDIR}
    )

    include(Windeployqt)
    windeployqt(openexr-viewer ${CMAKE_INSTALL_BINDIR})
endif ()


# ----------------------------------------------------------------------------
# Packaging
# ----------------------------------------------------------------------------

# set(CPACK_PACKAGE_NAME "OpenEXR Viewer")
# set(CPACK_PACKAGE_VENDOR "")
set(CPACK_PACKAGE_DESCRIPTION "Simple Viewer for OpenEXR files with detailed metadata display")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "OpenEXR Viewer")
# set(CPACK_PACKAGE_ICON "")
# set(CPACK_PACKAGE_CHECKSUM "SHA512")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_LIST_DIR}/LICENSE")
# set(CPACK_RESOURCE_FILE_README "README.md")
set(CPACK_MONOLITHIC_INSTALL True)
set(CPACK_PACKAGE_EXECUTABLES "openexr-viewer;OpenEXR Viewer")

if (WIN32)
    set(CPACK_PACKAGE_NAME "openexr-viewer")
    set(CPACK_PACKAGE_FILE_NAME "openexr-viewer-${PROJECT_VERSION}-windows-x64")
endif ()

# NSIS specific settings
if(CPACK_GENERATOR MATCHES "NSIS")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_LIST_DIR}/deploy/icon/openexr-viewer.ico")
  set(CPACK_NSIS_HELP_LINK "")
  set(CPACK_NSIS_URL_INFO_ABOUT "")
  set(CPACK_CREATE_DESKTOP_LINKS "OpenEXR Viewer")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "openexr-viewer.exe")
  set(CPACK_NSIS_MENU_LINKS "openexr-viewer.exe" "OpenEXR Viewer")
  set(CPACK_NSIS_MODIFY_PATH OFF)
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "openexr-viewer.exe")
  set(CPACK_PACKAGE_VENDOR "Lambda")
endif(CPACK_GENERATOR MATCHES "NSIS")

if("${CPACK_GENERATOR}" STREQUAL "PackageMaker")
  set(CPACK_PACKAGE_DEFAULT_LOCATION "/Applications")
endif("${CPACK_GENERATOR}" STREQUAL "PackageMaker")

if (APPLE)
    set(CPACK_BUNDLE_NAME "${MACOSX_BUNDLE_BUNDLE_NAME}")
    set(CPACK_BUNDLE_PLIST "${MACOSX_BUNDLE_CONTENTS}/Info.plist")
    set(CPACK_PACKAGE_ICON "${MACOSX_BUNDLE_ICON}")
    set(CPACK_BUNDLE_ICON "${MACOSX_BUNDLE_ICON}")
endif (APPLE)

# CPack
include(CPack)

option(BUILD_TESTING "Build viewer regression tests" OFF)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)
    set(TEST_SOURCES ${PROJECT_SOURCES})
    list(REMOVE_ITEM TEST_SOURCES src/main.cpp openexr-viewer.rc)
    add_executable(viewer-tests tests/ViewerTests.cpp ${TEST_SOURCES})
    target_include_directories(viewer-tests PRIVATE src 3rdparty/data "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(viewer-tests PRIVATE Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent
        Imath::Imath OpenEXR::OpenEXR)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(viewer-tests PRIVATE OpenMP::OpenMP_CXX)
    endif()
    if(WIN32)
        add_custom_command(TARGET viewer-tests POST_BUILD COMMAND ${CMAKE_COMMAND}
            -E copy_if_different $<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Test>
            $<TARGET_FILE_DIR:viewer-tests>
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:viewer-tests>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>/../plugins/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:viewer-tests>/platforms")
        add_dependencies(viewer-tests openexr-viewer)
    endif()
    add_test(NAME viewer-regressions COMMAND viewer-tests -o -,txt -o test-results.txt,txt)
    set_tests_properties(viewer-regressions PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(viewer-benchmark EXCLUDE_FROM_ALL tests/RenderBenchmark.cpp ${TEST_SOURCES})
    target_include_directories(viewer-benchmark PRIVATE src 3rdparty/data "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(viewer-benchmark PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Concurrent Imath::Imath OpenEXR::OpenEXR)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(viewer-benchmark PRIVATE OpenMP::OpenMP_CXX)
    endif()
    if(WIN32)
        target_link_libraries(viewer-benchmark PRIVATE psapi)
        add_dependencies(viewer-benchmark openexr-viewer)
    endif()
endif()
