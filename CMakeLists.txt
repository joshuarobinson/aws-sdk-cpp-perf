cmake_minimum_required(VERSION 3.2)
project(s3-examples)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -O3")

# Locate the aws sdk for c++ package.
find_package(AWSSDK REQUIRED COMPONENTS s3)

# Link to shared libraries.
add_definitions(-DUSE_IMPORT_EXPORT)

add_executable(readbucket_sync readbucket_sync.cpp)
add_executable(readbucket_mt readbucket_mt.cpp)

target_link_libraries(readbucket_sync ${AWSSDK_LINK_LIBRARIES})
target_link_libraries(readbucket_mt ${AWSSDK_LINK_LIBRARIES})
