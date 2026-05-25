cmake_minimum_required(VERSION 3.20)

project(FaceScanBackend LANGUAGES CXX)

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

add_executable(facescan_backend
    src/algorithm/color_point_cloud_sdk.cpp
    src/camera/camera_manager.cpp
    src/common/file_utils.cpp
    src/common/json_utils.cpp
    src/common/time_utils.cpp
    src/config/app_config.cpp
    src/database/database.cpp
    src/main.cpp
)

target_include_directories(facescan_backend PRIVATE
    src
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(facescan_backend PRIVATE
    Boost::headers
    SQLite3::SQLite3
    ZLIB::ZLIB
)

if(UNIX)
    target_link_libraries(facescan_backend PRIVATE pthread)
endif()
