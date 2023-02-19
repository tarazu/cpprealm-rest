cmake_minimum_required(VERSION 3.15)

project (rest-demo)

set(CMAKE_CXX_STANDARD 17)

find_package(cpprestsdk)
add_subdirectory(../realm-cpp/ cmake-build-debug)

# project that pushes data from rest calls
add_executable(rest-demo src/rest-demo.cpp)
target_link_libraries(rest-demo cpprealm cpprestsdk::cpprest)