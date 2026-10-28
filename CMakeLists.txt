cmake_minimum_required(VERSION 3.16)
project(AgriGcsVisual VERSION 1.6.0 LANGUAGES CXX)

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
    src/PhotoLibrary.cpp src/PhotoLibrary.h
    src/RelayDiscovery.cpp src/RelayDiscovery.h
    src/MappingTypes.h src/MappingClient.cpp src/MappingClient.h
    src/MappingStarter.cpp src/MappingStarter.h
    src/RoundTripMission.cpp src/RoundTripMission.h
    src/PointCloudView.cpp src/PointCloudView.h
    src/PointCloudGpuItem.cpp src/PointCloudGpuItem.h
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
    add_executable(ui_smoke tests/tst_ui.cpp src/AsyncTileMapItem.cpp src/AsyncTileMapItem.h src/OfflineMapSource.cpp src/OfflineMapSource.h src/NestPosition.cpp src/NestPosition.h src/AircraftProfile.cpp src/AircraftProfile.h src/GroundLink.cpp src/GroundLink.h src/MappingClient.cpp src/MappingClient.h src/PointCloudView.cpp src/PointCloudView.h qml.qrc)
    target_sources(ui_smoke PRIVATE src/PointCloudGpuItem.cpp src/PointCloudGpuItem.h)
    target_link_libraries(ui_smoke PRIVATE Qt5::Core Qt5::Gui Qt5::Qml
        Qt5::Quick Qt5::QuickControls2 Qt5::Test Qt5::Positioning Qt5::Concurrent Qt5::Network)
    add_test(NAME ui_smoke COMMAND ui_smoke)
    set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AGRI_GCS_TEST_TILES=${CMAKE_CURRENT_SOURCE_DIR}/maps/maxar-auckland;AGRI_GCS_TEST_SATELLITE=${CMAKE_CURRENT_SOURCE_DIR}/maps/maxar-auckland")
    add_executable(ground_link_test tests/tst_ground_link.cpp src/GroundLink.cpp src/GroundLink.h)
    target_link_libraries(ground_link_test PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    add_test(NAME ground_link_test COMMAND ground_link_test)
    add_executable(mapping_client_test tests/tst_mapping_client.cpp src/MappingClient.cpp src/MappingClient.h)
    target_include_directories(mapping_client_test PRIVATE src)
    target_link_libraries(mapping_client_test PRIVATE Qt5::Core Qt5::Gui Qt5::Network Qt5::Concurrent Qt5::Test)
    add_test(NAME mapping_client_test COMMAND mapping_client_test)
    add_executable(mapping_starter_test tests/tst_mapping_starter.cpp src/MappingStarter.cpp src/MappingStarter.h)
    target_include_directories(mapping_starter_test PRIVATE src)
    target_link_libraries(mapping_starter_test PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    add_test(NAME mapping_starter_test COMMAND mapping_starter_test)
    add_executable(roundtrip_test tests/tst_roundtrip.cpp src/RoundTripMission.cpp src/RoundTripMission.h
        src/GroundLink.cpp src/GroundLink.h src/MappingClient.cpp src/MappingClient.h
        src/PointCloudView.cpp src/PointCloudView.h src/PointCloudGpuItem.cpp src/PointCloudGpuItem.h)
    target_include_directories(roundtrip_test PRIVATE src)
    target_link_libraries(roundtrip_test PRIVATE Qt5::Core Qt5::Gui Qt5::Quick Qt5::Network Qt5::Concurrent Qt5::Test)
    add_test(NAME roundtrip_test COMMAND roundtrip_test)
    set_tests_properties(roundtrip_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(mission_execution_test tests/tst_mission_execution.cpp
        src/RoundTripMission.cpp src/RoundTripMission.h src/GroundLink.cpp src/GroundLink.h
        src/MappingClient.cpp src/MappingClient.h)
    target_include_directories(mission_execution_test PRIVATE src)
    target_link_libraries(mission_execution_test PRIVATE Qt5::Core Qt5::Gui Qt5::Network Qt5::Concurrent Qt5::Test)
    add_test(NAME mission_execution_test COMMAND mission_execution_test)
    set_tests_properties(mission_execution_test PROPERTIES TIMEOUT 180)
    add_executable(pointcloud_geometry_test tests/tst_pointcloud_geometry.cpp src/PointCloudGeometry.h)
    target_include_directories(pointcloud_geometry_test PRIVATE src)
    target_link_libraries(pointcloud_geometry_test PRIVATE Qt5::Core Qt5::Gui Qt5::Test)
    add_test(NAME pointcloud_geometry_test COMMAND pointcloud_geometry_test)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME mapping_bridge_test COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_pointcloud_bridge.py)
        set_tests_properties(mapping_bridge_test PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME mapping_tunnel_test COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_mapping_tunnel.py)
        set_tests_properties(mapping_tunnel_test PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
