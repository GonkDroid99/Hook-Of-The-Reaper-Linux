cmake_minimum_required(VERSION 3.19)
project(HookOfTheReaper LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


#qt_standard_project_setup()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)


#find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets SerialPort Network Multimedia Bluetooth)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets SerialPort Network Multimedia)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets SerialPort Network Multimedia)

# Platform-specific source files and dependencies
if(WIN32)
    set(app_icon_resource_windows "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/icons.rc")
    set(PLATFORM_SOURCES
        main.cpp ${app_icon_resource_windows}
        data/icons/icons.rc
        data/icons/hOTRIcon256.ico
        HookerEngine/HookCOMPortWin.h HookerEngine/HookCOMPortWin.cpp
    )
    set(PLATFORM_BUNDLE WIN32 MACOSX_BUNDLE)
else()
    set(PLATFORM_SOURCES
        main.cpp
        HookerEngine/HookCOMPort.cpp HookerEngine/HookCOMPort.h
    )
    set(PLATFORM_BUNDLE "")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIDAPI REQUIRED hidapi-libusb)
endif()

#qt_add_executable(HookOfTheReaper
add_executable(HookOfTheReaper
    ${PLATFORM_BUNDLE}
    ${PLATFORM_SOURCES}
    hookOfTheReaper.cpp
    hookOfTheReaper.h
    hookOfTheReaper.ui
    Global.h

    README.md
    data/iniDefault.hor data/lgDefault.hor data/settings.hor
    data/mx24.hor data/rs3Reaper.hor
    data/jbgun4ir.hor
    data/nonDefaultLG.hor
    data/fusion.hor
    data/blamcon.hor
    data/alienUSB.hor data/openFire.hor data/xGunner.hor
    data/sounds/sounds.qrc
    data/icons/icons.qrc

    COMDeviceList/ComDeviceList.cpp COMDeviceList/ComDeviceList.h
    COMDeviceList/LightBackground.h COMDeviceList/LightBackground.cpp
    COMDeviceList/LightController.h COMDeviceList/LightController.cpp
    COMDeviceList/LightExecution.h COMDeviceList/LightExecution.cpp
    COMDeviceList/LightGun.cpp COMDeviceList/LightGun.h
    COMDeviceList/PacDriveControl.h COMDeviceList/PacDriveControl.cpp

    HookerEngine/GamePlayer.h HookerEngine/GamePlayer.cpp
    HookerEngine/HookerEngine.cpp HookerEngine/HookerEngine.h
    HookerEngine/HookLight.h HookerEngine/HookLight.cpp
    HookerEngine/HookTCPSocket.cpp HookerEngine/HookTCPSocket.h
    HookerEngine/LightCommand.h HookerEngine/LightCommand.cpp

    Windows/aboutWindow.cpp Windows/aboutWindow.h Windows/aboutWindow.ui
    Windows/addLightControllerWindow.h Windows/addLightControllerWindow.cpp Windows/addLightControllerWindow.ui
    Windows/addLightGunWindowV2.h Windows/addLightGunWindowV2.cpp Windows/addLightGunWindowV2.ui
    Windows/editLightControllerWindow.h Windows/editLightControllerWindow.cpp Windows/editLightControllerWindow.ui
    Windows/editLightGunWindowV2.h Windows/editLightGunWindowV2.cpp Windows/editLightGunWindowV2.ui
    Windows/playerAssignWindow.cpp Windows/playerAssignWindow.h Windows/playerAssignWindow.ui
    Windows/settingsWindow.cpp Windows/settingsWindow.h Windows/settingsWindow.ui
    Windows/testComPortWindow.h Windows/testComPortWindow.cpp Windows/testComPortWindow.ui
    Windows/testLightController.h Windows/testLightController.cpp Windows/testLightController.ui

    ini/MAME/digdug.ini ini/default.ini
    defaultLG/hotd4.txt defaultLG/jp.txt defaultLG/rambo.txt
    defaultLG/lgi.txt defaultLG/lgi3d.txt defaultLG/lgj.txt
    defaultLG/alien3.txt defaultLG/confmiss.txt defaultLG/ptblank2.txt
    COMDeviceList/UltimarcLC.h COMDeviceList/UltimarcLC.cpp
    COMDeviceList/ALEDStripLC.h COMDeviceList/ALEDStripLC.cpp
)


if(WIN32)
    target_include_directories(HookOfTheReaper
        PRIVATE
        D:/QtProjects/HookOfTheReaperGit/HIDAPI/include
        D:/QtProjects/HookOfTheReaperGit/UltimarcSDK
    )
    target_link_directories(HookOfTheReaper
        PRIVATE
        D:/QtProjects/HookOfTheReaperGit/HIDAPI/x64
        D:/QtProjects/HookOfTheReaperGit/UltimarcSDK
    )
    target_link_libraries(HookOfTheReaper
        PRIVATE
            Qt::Core
            Qt::Widgets
            Qt::SerialPort
            Qt::Network
            Qt::Multimedia
            hidapi
            PacDrive64
    )
    target_link_options(HookOfTheReaper PRIVATE "-Wl,--start-group")
else()
    target_include_directories(HookOfTheReaper PRIVATE ${HIDAPI_INCLUDE_DIRS})
    target_link_libraries(HookOfTheReaper
        PRIVATE
            Qt::Core
            Qt::Widgets
            Qt::SerialPort
            Qt::Network
            Qt::Multimedia
            ${HIDAPI_LIBRARIES}
    )
    target_compile_options(HookOfTheReaper PRIVATE ${HIDAPI_CFLAGS_OTHER})
endif()


include(GNUInstallDirs)

# ──────────────────────────────────────────────────────────────────────────────
# AppImage target (Linux only)
#   cmake --build build --target appimage
# Produces HookOfTheReaper-x86_64.AppImage in the project root.
# ──────────────────────────────────────────────────────────────────────────────
if(NOT WIN32)
    find_program(QMAKE_BIN NAMES qmake6 qmake REQUIRED)

    set(LINUXDEPLOY      "${CMAKE_SOURCE_DIR}/linuxdeploy-x86_64.AppImage")
    set(LINUXDEPLOY_QT   "${CMAKE_SOURCE_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage")
    set(APPDIR           "${CMAKE_BINARY_DIR}/AppDir")
    set(APPIMAGE_OUT     "${CMAKE_SOURCE_DIR}/HookOfTheReaper-x86_64.AppImage")

    # Script that downloads linuxdeploy tools if missing — runs at build time
    set(DOWNLOAD_SCRIPT "${CMAKE_BINARY_DIR}/download_linuxdeploy.cmake")
    file(WRITE "${DOWNLOAD_SCRIPT}" [=[
set(LD  "${LINUXDEPLOY}")
set(LDQ "${LINUXDEPLOY_QT}")
foreach(URL_VAR IN ITEMS
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage;${LD}"
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage;${LDQ}")
    list(GET URL_VAR 0 URL)
    list(GET URL_VAR 1 DEST)
    if(NOT EXISTS "${DEST}")
        message(STATUS "Downloading ${URL} -> ${DEST}")
        file(DOWNLOAD "${URL}" "${DEST}" SHOW_PROGRESS STATUS DL_STATUS)
        list(GET DL_STATUS 0 DL_ERR)
        if(DL_ERR)
            message(FATAL_ERROR "Download failed: ${DL_STATUS}")
        endif()
        file(CHMOD "${DEST}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
             GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
endforeach()
]=])

    # Write AppRun at configure time into the build dir — cmake brackets prevent $ expansion
    set(APPRUN_STAGING "${CMAKE_BINARY_DIR}/AppRun.in")
    file(WRITE "${APPRUN_STAGING}" [=[#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QML2_IMPORT_PATH="$HERE/usr/qml"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
exec "$HERE/usr/bin/HookOfTheReaper" "$@"
]=])

    add_custom_target(appimage
        DEPENDS HookOfTheReaper

        # 1. Download tools if missing
        COMMAND ${CMAKE_COMMAND}
            -DLINUXDEPLOY=${LINUXDEPLOY}
            -DLINUXDEPLOY_QT=${LINUXDEPLOY_QT}
            -P ${DOWNLOAD_SCRIPT}

        # 2. Assemble AppDir
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${APPDIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${APPDIR}/usr/bin
        COMMAND ${CMAKE_COMMAND} -E make_directory ${APPDIR}/usr/share/applications
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${APPDIR}/usr/share/icons/hicolor/256x256/apps
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:HookOfTheReaper>
            ${APPDIR}/usr/bin/HookOfTheReaper
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/data/icons/hOTRIcon256.png
            ${APPDIR}/usr/share/icons/hicolor/256x256/apps/HookOfTheReaper.png
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/HookOfTheReaper.desktop
            ${APPDIR}/usr/share/applications/HookOfTheReaper.desktop

        # 3. Install AppRun (linuxdeploy won't overwrite an existing one)
        COMMAND ${CMAKE_COMMAND} -E copy ${APPRUN_STAGING} ${APPDIR}/AppRun
        COMMAND chmod 755 ${APPDIR}/AppRun

        # 4. Bundle with linuxdeploy
        COMMAND ${CMAKE_COMMAND} -E env
            QMAKE=${QMAKE_BIN}
            OUTPUT=${APPIMAGE_OUT}
            NO_STRIP=1
            APPIMAGE_EXTRACT_AND_RUN=1
            ${LINUXDEPLOY}
                --appdir ${APPDIR}
                --plugin qt
                --output appimage
                --desktop-file ${CMAKE_SOURCE_DIR}/HookOfTheReaper.desktop
                --icon-file ${CMAKE_SOURCE_DIR}/data/icons/hOTRIcon256.png

        COMMENT "Building AppImage -> HookOfTheReaper-x86_64.AppImage"
        VERBATIM
        USES_TERMINAL
    )
endif()
