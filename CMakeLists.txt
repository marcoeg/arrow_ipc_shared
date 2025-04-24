cmake_minimum_required(VERSION 3.14)
project(ArrowMemfdSender)

set(CMAKE_CXX_STANDARD 17)

# Find Arrow manually
find_package(Arrow REQUIRED)

add_executable(sender sender.cpp)

# Use absolute path to the Arrow library
target_link_libraries(sender PRIVATE arrow_shared)
