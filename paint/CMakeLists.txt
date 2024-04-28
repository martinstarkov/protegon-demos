cmake_minimum_required(VERSION 3.20)

project(paint)

add_executable(paint src/application.cpp)

add_protegon_to(${PROJECT_NAME})

IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/")
    create_resource_symlink(${PROJECT_NAME} "resources")
ENDIF()
