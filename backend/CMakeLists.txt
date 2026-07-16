cmake_minimum_required(VERSION 3.20)

project(FaceScanBackend LANGUAGES CXX)

include(CTest)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Boost REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)

# Orbbec SDK 为厂商闭源预编译包，不进 vcpkg 也不进 git；按平台放在
# 3rdParty/orbbec/<platform>/ 下（见 3rdParty/README.md）；缺失时 mock 仍可用，硬件模式会明确报错。
if(NOT FACESCAN_ORBBEC_SDK_DIR)
    if(APPLE)
        set(FACESCAN_ORBBEC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/orbbec/macos")
    elseif(WIN32)
        set(FACESCAN_ORBBEC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/orbbec/windows")
    else()
        set(FACESCAN_ORBBEC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/orbbec/linux")
    endif()
endif()

set(ORBBEC_SDK_INCLUDE_DIR "${FACESCAN_ORBBEC_SDK_DIR}/include")
if(WIN32)
    file(GLOB ORBBEC_SDK_LIBS "${FACESCAN_ORBBEC_SDK_DIR}/lib/OrbbecSDK*.lib")
else()
    file(GLOB ORBBEC_SDK_LIBS
        "${FACESCAN_ORBBEC_SDK_DIR}/lib/libOrbbecSDK*.dylib"
        "${FACESCAN_ORBBEC_SDK_DIR}/lib/libOrbbecSDK*.so")
endif()
list(SORT ORBBEC_SDK_LIBS)
if(EXISTS "${ORBBEC_SDK_INCLUDE_DIR}/libobsensor/ObSensor.hpp" AND ORBBEC_SDK_LIBS)
    list(GET ORBBEC_SDK_LIBS 0 ORBBEC_SDK_LIB)
    message(STATUS "Orbbec SDK found: ${ORBBEC_SDK_LIB}")
else()
    set(ORBBEC_SDK_LIB "")
    message(STATUS "Orbbec SDK not found under ${FACESCAN_ORBBEC_SDK_DIR}; hardware camera mode will be unavailable")
endif()

add_library(facescan_core STATIC
    src/algorithm/color_point_cloud_sdk.cpp
    src/api/app.cpp
    src/camera/camera_manager.cpp
    src/camera/multi_camera_device.cpp
    src/camera/vendor_camera.cpp
    src/common/bmp_utils.cpp
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

if(WIN32)
    # Boost.Asio 在 Windows 上需要明确目标系统版本（0x0601 = Windows 7+）
    target_compile_definitions(facescan_core PUBLIC _WIN32_WINNT=0x0601 NOMINMAX)
endif()

if(ORBBEC_SDK_LIB)
    target_sources(facescan_core PRIVATE src/camera/orbbec_multi_camera.cpp)
    target_compile_definitions(facescan_core PUBLIC FACESCAN_WITH_ORBBEC)
    target_include_directories(facescan_core PRIVATE "${ORBBEC_SDK_INCLUDE_DIR}")
    target_link_libraries(facescan_core PUBLIC "${ORBBEC_SDK_LIB}")
    if(NOT WIN32)
        set_target_properties(facescan_core PROPERTIES
            BUILD_RPATH "${FACESCAN_ORBBEC_SDK_DIR}/lib"
        )
    endif()
endif()

if(WIN32)
    if(NOT FACESCAN_HIKVISION_SDK_DIR)
        set(FACESCAN_HIKVISION_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/hikvision/windows")
    endif()
    set(HIKVISION_INCLUDE "${FACESCAN_HIKVISION_SDK_DIR}/include")
    set(HIKVISION_LIBRARY "${FACESCAN_HIKVISION_SDK_DIR}/lib/MvCameraControl.lib")
    if(EXISTS "${HIKVISION_INCLUDE}/MvCameraControl.h" AND EXISTS "${HIKVISION_LIBRARY}")
        message(STATUS "Hikvision SDK found: ${HIKVISION_LIBRARY}")
        target_sources(facescan_core PRIVATE src/camera/hikvision_camera.cpp)
        target_compile_definitions(facescan_core PUBLIC FACESCAN_WITH_HIKVISION)
        target_include_directories(facescan_core PRIVATE "${HIKVISION_INCLUDE}")
        target_link_libraries(facescan_core PUBLIC "${HIKVISION_LIBRARY}")
        set(HIKVISION_RUNTIME "${FACESCAN_HIKVISION_SDK_DIR}/bin/MvCameraControl.dll")
    else()
        message(STATUS "Hikvision SDK not found under ${FACESCAN_HIKVISION_SDK_DIR}; multi_camera mode will be unavailable")
    endif()

    if(NOT FACESCAN_MINDVISION_SDK_DIR)
        set(FACESCAN_MINDVISION_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/mindvision/windows")
    endif()
    set(MINDVISION_INCLUDE "${FACESCAN_MINDVISION_SDK_DIR}/include")
    set(MINDVISION_LIBRARY "${FACESCAN_MINDVISION_SDK_DIR}/lib/MVCAMSDK_X64.lib")
    if(EXISTS "${MINDVISION_INCLUDE}/CameraApi.h" AND EXISTS "${MINDVISION_LIBRARY}")
        message(STATUS "MindVision SDK found: ${MINDVISION_LIBRARY}")
        target_sources(facescan_core PRIVATE src/camera/mindvision_camera.cpp)
        target_compile_definitions(facescan_core PUBLIC FACESCAN_WITH_MINDVISION)
        target_include_directories(facescan_core PRIVATE "${MINDVISION_INCLUDE}")
        target_link_libraries(facescan_core PUBLIC "${MINDVISION_LIBRARY}")
        set(MINDVISION_RUNTIME "${FACESCAN_MINDVISION_SDK_DIR}/bin/MVCAMSDK_X64.dll")
    else()
        message(STATUS "MindVision SDK not found under ${FACESCAN_MINDVISION_SDK_DIR}; multi_camera mode will be unavailable")
    endif()
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

if(ORBBEC_SDK_LIB AND WIN32)
    # Windows SDK ships runtime files separately from the import library.
    file(GLOB ORBBEC_SDK_RUNTIME
        "${FACESCAN_ORBBEC_SDK_DIR}/bin/*.dll"
        "${FACESCAN_ORBBEC_SDK_DIR}/bin/*.xml")
endif()

if(WIN32)
    set(FACESCAN_VENDOR_RUNTIME)
    if(EXISTS "${HIKVISION_RUNTIME}")
        list(APPEND FACESCAN_VENDOR_RUNTIME "${HIKVISION_RUNTIME}")
    endif()
    if(EXISTS "${MINDVISION_RUNTIME}")
        list(APPEND FACESCAN_VENDOR_RUNTIME "${MINDVISION_RUNTIME}")
    endif()
    if(FACESCAN_VENDOR_RUNTIME)
        add_custom_command(TARGET facescan_backend POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${FACESCAN_VENDOR_RUNTIME} $<TARGET_FILE_DIR:facescan_backend>)
    endif()
endif()

if(BUILD_TESTING)
    # 优先使用 vcpkg/系统提供的 GoogleTest；找不到时回退到 FetchContent 在线下载
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)

        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(facescan_tests
        tests/app_test.cpp
        tests/bmp_utils_test.cpp
        tests/camera_manager_test.cpp
        tests/color_point_cloud_sdk_test.cpp
        tests/database_test.cpp
        tests/json_utils_test.cpp
    )

    target_include_directories(facescan_tests BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    if(NOT GTest_FOUND)
        target_include_directories(facescan_tests BEFORE PRIVATE
            ${googletest_SOURCE_DIR}/googletest/include
        )
    endif()

    target_link_libraries(facescan_tests PRIVATE
        facescan_core
        GTest::gtest_main
    )

    if(ORBBEC_SDK_RUNTIME)
        add_custom_command(TARGET facescan_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${ORBBEC_SDK_RUNTIME} $<TARGET_FILE_DIR:facescan_tests>)
    endif()

    include(GoogleTest)
    gtest_discover_tests(facescan_tests)
endif()

if(ORBBEC_SDK_RUNTIME)
    add_custom_command(TARGET facescan_backend POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ORBBEC_SDK_RUNTIME} $<TARGET_FILE_DIR:facescan_backend>)
endif()
