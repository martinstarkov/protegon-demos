cmake_minimum_required(VERSION 3.20)

project(pixel_jam_2024)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false 
    "${SRC_DIR}/*.h" "${SRC_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR})

add_protegon_to(${PROJECT_NAME})

IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/")
    create_resource_symlink(${PROJECT_NAME} "resources")
ENDIF()
