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
    src/GimbalLink.cpp src/GimbalLink.h
    qml.qrc
)

target_link_libraries(agri_gcs_visual PRIVATE
    Qt5::Core Qt5::Gui Qt5::Qml Qt5::Quick Qt5::QuickControls2 Qt5::Positioning Qt5::Concurrent Qt5::Network
)

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
    add_executable(ui_smoke tests/tst_ui.cpp src/AsyncTileMapItem.cpp src/AsyncTileMapItem.h src/OfflineMapSource.cpp src/OfflineMapSource.h src/NestPosition.cpp src/NestPosition.h src/AircraftProfile.cpp src/AircraftProfile.h src/GroundLink.cpp src/GroundLink.h qml.qrc)
    target_link_libraries(ui_smoke PRIVATE Qt5::Core Qt5::Gui Qt5::Qml
        Qt5::Quick Qt5::QuickControls2 Qt5::Test Qt5::Positioning Qt5::Concurrent Qt5::Network)
    add_test(NAME ui_smoke COMMAND ui_smoke)
    set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AGRI_GCS_TEST_TILES=${CMAKE_CURRENT_SOURCE_DIR}/maps/maxar-auckland;AGRI_GCS_TEST_SATELLITE=${CMAKE_CURRENT_SOURCE_DIR}/maps/maxar-auckland")
    add_executable(ground_link_test tests/tst_ground_link.cpp src/GroundLink.cpp src/GroundLink.h)
    target_link_libraries(ground_link_test PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    add_test(NAME ground_link_test COMMAND ground_link_test)
endif()
