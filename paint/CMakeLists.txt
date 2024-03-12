cmake_minimum_required(VERSION 3.20)

project(paint)

add_executable(paint src/application.cpp)

target_link_libraries(paint PRIVATE protegon)