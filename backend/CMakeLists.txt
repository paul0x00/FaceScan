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

find_package(Boost REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)

# Orbbec SDK 为厂商闭源预编译包，不进 vcpkg 也不进 git；按平台放在
# 3rdParty/orbbec/<platform>/ 下（见 3rdParty/README.md），缺失时自动降级为 mock 模式。
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
    target_compile_definitions(facescan_core PUBLIC FACESCAN_WITH_ORBBEC)
    target_include_directories(facescan_core PRIVATE "${ORBBEC_SDK_INCLUDE_DIR}")
    target_link_libraries(facescan_core PUBLIC "${ORBBEC_SDK_LIB}")
    if(NOT WIN32)
        set_target_properties(facescan_core PROPERTIES
            BUILD_RPATH "${FACESCAN_ORBBEC_SDK_DIR}/lib"
        )
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
    # Windows 下 SDK 以动态库分发，构建后把 dll 复制到可执行文件目录
    file(GLOB ORBBEC_SDK_RUNTIME
        "${FACESCAN_ORBBEC_SDK_DIR}/bin/*.dll"
        "${FACESCAN_ORBBEC_SDK_DIR}/bin/*.xml")
    if(ORBBEC_SDK_RUNTIME)
        add_custom_command(TARGET facescan_backend POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${ORBBEC_SDK_RUNTIME} $<TARGET_FILE_DIR:facescan_backend>)
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

    include(GoogleTest)
    gtest_discover_tests(facescan_tests)
endif()
