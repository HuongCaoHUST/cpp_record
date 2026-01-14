cmake_minimum_required(VERSION 3.10)
project(BeeRecorder)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()
find_package(OpenCV REQUIRED)
message(STATUS "OpenCV version: ${OpenCV_VERSION}")
add_executable(bee_record src/record.cpp)
target_include_directories(bee_record PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bee_record ${OpenCV_LIBS})