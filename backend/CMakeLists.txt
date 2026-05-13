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
