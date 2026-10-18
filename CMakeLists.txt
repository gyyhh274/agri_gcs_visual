cmake_minimum_required(VERSION 3.16)
project(AgriGcsVisual VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Positioning Concurrent Network)

add_executable(agri_gcs_visual MACOSX_BUNDLE
    src/main.cpp
    src/AsyncTileMapItem.cpp src/AsyncTileMapItem.h
    src/OfflineMapSource.cpp src/OfflineMapSource.h
    src/NestPosition.cpp src/NestPosition.h
    src/AircraftProfile.cpp src/AircraftProfile.h
    src/GroundLink.cpp src/GroundLink.h
    src/OnboardManager.cpp src/OnboardManager.h
    src/GimbalLink.cpp src/GimbalLink.h
    src/PhotoLibrary.cpp src/PhotoLibrary.h
    src/RelayDiscovery.cpp src/RelayDiscovery.h
    src/NativeVideoItem.cpp src/NativeVideoItem.h
    qml.qrc
)

target_link_libraries(agri_gcs_visual PRIVATE
    Qt5::Core Qt5::Gui Qt5::Qml Qt5::Quick Qt5::QuickControls2 Qt5::Positioning Qt5::Concurrent Qt5::Network
)

# Qt5/AVFoundation 在 macOS 不支持 RTSP。复用 MiniGUI_A8mini 的硬解/GL 管线。
# Linux 继续使用 Qt Multimedia 的 GStreamer 后端。
if(APPLE)
    set(AGRI_GCS_GSTREAMER_ROOT "" CACHE PATH "GStreamer.framework/Versions/1.0 directory")
    if(NOT AGRI_GCS_GSTREAMER_ROOT)
        foreach(candidate "$ENV{HOME}/Documents/A_Workspace/MiniGUI_A8mini/.deps/GStreamer.framework/Versions/1.0"
                          "/Library/Frameworks/GStreamer.framework/Versions/1.0")
            if(EXISTS "${candidate}/lib/pkgconfig/gstreamer-app-1.0.pc")
                set(AGRI_GCS_GSTREAMER_ROOT "${candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(AGRI_GCS_GSTREAMER_ROOT)
        set(ENV{PKG_CONFIG_PATH} "${AGRI_GCS_GSTREAMER_ROOT}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        set(PKG_CONFIG_EXECUTABLE "${AGRI_GCS_GSTREAMER_ROOT}/bin/pkg-config")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-app-1.0 gstreamer-video-1.0)
        set(AGRI_GCS_QMLGL_PLUGIN_PATH "" CACHE PATH "Directory containing the Qt5 libgstqmlgl plugin")
        if(NOT AGRI_GCS_QMLGL_PLUGIN_PATH)
            get_filename_component(AGRI_GCS_QMLGL_PLUGIN_PATH
                "${AGRI_GCS_GSTREAMER_ROOT}/../../../../build/gst-plugins" ABSOLUTE)
        endif()
        target_sources(agri_gcs_visual PRIVATE src/A8VideoPipeline.cpp src/A8VideoPipeline.h)
        target_compile_definitions(agri_gcs_visual PRIVATE AGRI_GCS_NATIVE_GSTREAMER
            AGRI_GCS_GSTREAMER_ROOT="${AGRI_GCS_GSTREAMER_ROOT}"
            AGRI_GCS_QMLGL_PLUGIN_PATH="${AGRI_GCS_QMLGL_PLUGIN_PATH}")
        target_link_libraries(agri_gcs_visual PRIVATE PkgConfig::GST)
        set_property(TARGET agri_gcs_visual APPEND PROPERTY BUILD_RPATH
            "${AGRI_GCS_GSTREAMER_ROOT}" "${AGRI_GCS_GSTREAMER_ROOT}/lib")
    else()
        message(WARNING "GStreamer not found: RTSP playback on macOS will be unavailable")
    endif()
endif()

set_target_properties(agri_gcs_visual PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Agri GCS Visual"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.agri.gcs.visual"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

install(TARGETS agri_gcs_visual RUNTIME DESTINATION bin BUNDLE DESTINATION .)
install(DIRECTORY maps/ DESTINATION share/agri_gcs_visual/maps)

option(AGRI_GCS_BUILD_TESTS "Build Qt UI smoke tests" OFF)
if(AGRI_GCS_BUILD_TESTS)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(ui_smoke tests/tst_ui.cpp src/AsyncTileMapItem.cpp src/AsyncTileMapItem.h src/OfflineMapSource.cpp src/OfflineMapSource.h src/NestPosition.cpp src/NestPosition.h src/AircraftProfile.cpp src/AircraftProfile.h src/GroundLink.cpp src/GroundLink.h src/NativeVideoItem.cpp src/NativeVideoItem.h qml.qrc)
    target_link_libraries(ui_smoke PRIVATE Qt5::Core Qt5::Gui Qt5::Qml
        Qt5::Quick Qt5::QuickControls2 Qt5::Test Qt5::Positioning Qt5::Concurrent Qt5::Network)
    add_test(NAME ui_smoke COMMAND ui_smoke)
    set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AGRI_GCS_TEST_TILES=${CMAKE_CURRENT_SOURCE_DIR}/maps/maxar-auckland;AGRI_GCS_TEST_SATELLITE=${CMAKE_CURRENT_SOURCE_DIR}/maps/maxar-auckland")
    add_executable(onboard_manager_test tests/tst_onboard_manager.cpp src/OnboardManager.cpp src/OnboardManager.h)
    target_link_libraries(onboard_manager_test PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    add_test(NAME onboard_manager_test COMMAND onboard_manager_test)
    add_executable(ground_link_test tests/tst_ground_link.cpp src/GroundLink.cpp src/GroundLink.h)
    target_link_libraries(ground_link_test PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    add_test(NAME ground_link_test COMMAND ground_link_test)
    add_executable(gimbal_link_test tests/tst_gimbal_link.cpp src/GimbalLink.cpp src/GimbalLink.h)
    target_link_libraries(gimbal_link_test PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    add_test(NAME gimbal_link_test COMMAND gimbal_link_test)
    add_executable(task_session_test tests/tst_task_session.cpp
        src/AsyncTileMapItem.cpp src/OfflineMapSource.cpp src/NestPosition.cpp
        src/AircraftProfile.cpp src/GroundLink.cpp src/GimbalLink.cpp
        src/PhotoLibrary.cpp src/RelayDiscovery.cpp src/NativeVideoItem.cpp qml.qrc)
    target_include_directories(task_session_test PRIVATE src)
    target_link_libraries(task_session_test PRIVATE Qt5::Core Qt5::Gui Qt5::Qml
        Qt5::Quick Qt5::QuickControls2 Qt5::Positioning Qt5::Concurrent Qt5::Network Qt5::Test)
    add_test(NAME task_session_test COMMAND task_session_test)
endif()
