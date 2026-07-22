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
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# 所有模块共享源码目录作为公共头文件根目录，并统一 Windows 编译环境。
function(facescan_configure_target target)
    target_include_directories(${target} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(WIN32)
        # Boost.Asio 在 Windows 上需要明确目标系统版本（0x0601 = Windows 7+）。
        target_compile_definitions(${target} PUBLIC _WIN32_WINNT=0x0601 NOMINMAX)
    endif()
endfunction()

# 动态模块默认隐藏内部实现，只导出带 FACESCAN_*_API 标记的接口。
function(facescan_configure_shared_target target build_definition)
    facescan_configure_target(${target})
    target_compile_definitions(${target} PRIVATE ${build_definition})
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
endfunction()

# 公共工具无状态且只供各模块内部使用，保留为可嵌入动态库的静态基础层。
add_library(facescan_common STATIC
    src/common/bmp_utils.cpp
    src/common/file_utils.cpp
    src/common/json_utils.cpp
    src/common/time_utils.cpp
)
facescan_configure_target(facescan_common)
set_target_properties(facescan_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

# 配置文件加载与保存模块。
add_library(FaceScanConfig SHARED
    src/FaceScanConfig/app_config.cpp
)
facescan_configure_shared_target(FaceScanConfig FACESCAN_BUILD_CONFIG)
target_link_libraries(FaceScanConfig PRIVATE facescan_common)

# SQLite 数据访问与数据目录同步模块。
add_library(FaceScanDatabase SHARED
    src/FaceScanDatabase/database.cpp
)
facescan_configure_shared_target(FaceScanDatabase FACESCAN_BUILD_DATABASE)
target_include_directories(FaceScanDatabase PUBLIC ${SQLite3_INCLUDE_DIRS})
target_link_libraries(FaceScanDatabase
    PRIVATE facescan_common
    PUBLIC SQLite3::SQLite3
)

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

# 相机控制模块；厂商 SDK 仅作为该 DLL 的私有依赖。
add_library(FaceScanCamera SHARED
    src/FaceScanCamera/camera_manager.cpp
    src/FaceScanCamera/multi_camera_device.cpp
    src/FaceScanCamera/vendor_camera.cpp
)
facescan_configure_shared_target(FaceScanCamera FACESCAN_BUILD_CAMERA)
target_link_libraries(FaceScanCamera PRIVATE
    facescan_common
    Threads::Threads
)

if(ORBBEC_SDK_LIB)
    target_sources(FaceScanCamera PRIVATE src/FaceScanCamera/orbbec_multi_camera.cpp)
    target_compile_definitions(FaceScanCamera PRIVATE FACESCAN_WITH_ORBBEC)
    target_include_directories(FaceScanCamera PRIVATE "${ORBBEC_SDK_INCLUDE_DIR}")
    target_link_libraries(FaceScanCamera PRIVATE "${ORBBEC_SDK_LIB}")
    if(NOT WIN32)
        set_property(TARGET FaceScanCamera APPEND PROPERTY
            BUILD_RPATH "${FACESCAN_ORBBEC_SDK_DIR}/lib")
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
        target_sources(FaceScanCamera PRIVATE src/FaceScanCamera/hikvision_camera.cpp)
        target_compile_definitions(FaceScanCamera PRIVATE FACESCAN_WITH_HIKVISION)
        target_include_directories(FaceScanCamera PRIVATE "${HIKVISION_INCLUDE}")
        target_link_libraries(FaceScanCamera PRIVATE "${HIKVISION_LIBRARY}")
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
        target_sources(FaceScanCamera PRIVATE src/FaceScanCamera/mindvision_camera.cpp)
        target_compile_definitions(FaceScanCamera PRIVATE FACESCAN_WITH_MINDVISION)
        target_include_directories(FaceScanCamera PRIVATE "${MINDVISION_INCLUDE}")
        target_link_libraries(FaceScanCamera PRIVATE "${MINDVISION_LIBRARY}")
        set(MINDVISION_RUNTIME "${FACESCAN_MINDVISION_SDK_DIR}/bin/MVCAMSDK_X64.dll")
    else()
        message(STATUS "MindVision SDK not found under ${FACESCAN_MINDVISION_SDK_DIR}; multi_camera mode will be unavailable")
    endif()
endif()

# 点云重建和点云编辑模块。
add_library(FaceScanReconstruction SHARED
    src/FaceScanReconstruction/color_point_cloud_sdk.cpp
    src/FaceScanReconstruction/point_cloud_editor.cpp
)
facescan_configure_shared_target(FaceScanReconstruction FACESCAN_BUILD_RECONSTRUCTION)
target_link_libraries(FaceScanReconstruction PRIVATE facescan_common)

# HTTP API、监听和业务编排模块。
add_library(FaceScanService SHARED
    src/FaceScanService/app.cpp
    src/FaceScanService/http_server.cpp
)
facescan_configure_shared_target(FaceScanService FACESCAN_BUILD_SERVICE)
target_link_libraries(FaceScanService
    PRIVATE
        FaceScanCamera
        FaceScanDatabase
        FaceScanReconstruction
        facescan_common
        Threads::Threads
        ZLIB::ZLIB
    PUBLIC
        FaceScanConfig
        Boost::headers
)

# 主程序保留进程入口、配置加载、端口解析和服务启动流程。
add_executable(FaceScanBackend
    src/FaceScanBackend/main.cpp
)
facescan_configure_target(FaceScanBackend)
target_link_libraries(FaceScanBackend PRIVATE
    FaceScanConfig
    FaceScanService
)

# Windows 厂商 SDK 的运行时文件与相机 DLL 放在同一输出目录。
if(ORBBEC_SDK_LIB AND WIN32)
    file(GLOB ORBBEC_SDK_RUNTIME
        "${FACESCAN_ORBBEC_SDK_DIR}/bin/*.dll"
        "${FACESCAN_ORBBEC_SDK_DIR}/bin/*.xml")
endif()

if(WIN32)
    set(FACESCAN_CAMERA_RUNTIME ${ORBBEC_SDK_RUNTIME})
    if(EXISTS "${HIKVISION_RUNTIME}")
        list(APPEND FACESCAN_CAMERA_RUNTIME "${HIKVISION_RUNTIME}")
    endif()
    if(EXISTS "${MINDVISION_RUNTIME}")
        list(APPEND FACESCAN_CAMERA_RUNTIME "${MINDVISION_RUNTIME}")
    endif()
    if(FACESCAN_CAMERA_RUNTIME)
        add_custom_command(TARGET FaceScanCamera POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${FACESCAN_CAMERA_RUNTIME} $<TARGET_FILE_DIR:FaceScanCamera>)
    endif()
endif()

if(BUILD_TESTING)
    # 优先使用 vcpkg/系统提供的 GoogleTest；找不到时回退到 FetchContent 在线下载。
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
        tests/point_cloud_editor_test.cpp
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
        FaceScanReconstruction
        FaceScanService
        FaceScanCamera
        facescan_common
        FaceScanConfig
        FaceScanDatabase
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(facescan_tests)
endif()
