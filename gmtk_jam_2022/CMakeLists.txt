cmake_minimum_required(VERSION 3.20)

project(gmtk_jam_2022)

set(TEST_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE TEST_SRC_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false 
    "${TEST_SRC_DIR}/*.h" "${TEST_SRC_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${TEST_SRC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE ${TEST_SRC_DIR})

add_protegon_to(${PROJECT_NAME})

IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/")
    create_resource_symlink(${PROJECT_NAME} "resources")
ENDIF()
