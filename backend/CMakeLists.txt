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

add_executable(facescan_backend
    src/main.cpp
)

target_include_directories(facescan_backend PRIVATE
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(facescan_backend PRIVATE
    Boost::headers
    SQLite3::SQLite3
)

if(UNIX)
    target_link_libraries(facescan_backend PRIVATE pthread)
endif()
