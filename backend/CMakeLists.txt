cmake_minimum_required(VERSION 3.20)

project(FaceScanBackend LANGUAGES CXX)

include(CTest)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Boost REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)

set(ORBBEC_SDK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/include")
file(GLOB ORBBEC_SDK_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/lib/libOrbbecSDK*")
list(SORT ORBBEC_SDK_LIBS)
if(EXISTS "${ORBBEC_SDK_INCLUDE_DIR}/libobsensor/ObSensor.hpp" AND ORBBEC_SDK_LIBS)
    list(GET ORBBEC_SDK_LIBS 0 ORBBEC_SDK_LIB)
    message(STATUS "Orbbec SDK found: ${ORBBEC_SDK_LIB}")
else()
    set(ORBBEC_SDK_LIB "")
    message(STATUS "Orbbec SDK not found; hardware camera mode will be unavailable")
endif()

add_library(facescan_core STATIC
    src/algorithm/color_point_cloud_sdk.cpp
    src/api/app.cpp
    src/camera/camera_manager.cpp
    src/common/file_utils.cpp
    src/common/json_utils.cpp
    src/common/time_utils.cpp
    src/config/app_config.cpp
    src/database/database.cpp
)

target_include_directories(facescan_core PUBLIC
    src
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(facescan_core PUBLIC
    Boost::headers
    SQLite3::SQLite3
    ZLIB::ZLIB
)

if(ORBBEC_SDK_LIB)
    target_compile_definitions(facescan_core PUBLIC FACESCAN_WITH_ORBBEC)
    target_include_directories(facescan_core PRIVATE "${ORBBEC_SDK_INCLUDE_DIR}")
    target_link_libraries(facescan_core PUBLIC "${ORBBEC_SDK_LIB}")
    set_target_properties(facescan_core PROPERTIES
        BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/lib"
    )
endif()

add_executable(facescan_backend
    src/main.cpp
    src/server/http_server.cpp
)

target_link_libraries(facescan_backend PRIVATE
    facescan_core
)

if(UNIX)
    target_link_libraries(facescan_backend PRIVATE pthread)
endif()

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(facescan_tests
        tests/app_test.cpp
        tests/camera_manager_test.cpp
        tests/color_point_cloud_sdk_test.cpp
        tests/database_test.cpp
        tests/json_utils_test.cpp
    )

    target_include_directories(facescan_tests BEFORE PRIVATE
        ${googletest_SOURCE_DIR}/googletest/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(facescan_tests PRIVATE
        facescan_core
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(facescan_tests)
endif()
