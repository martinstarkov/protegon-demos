cmake_minimum_required(VERSION 3.28)

project(jerboa_jam_2026)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false "${SRC_DIR}/*.h" "${SRC_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE "${SRC_DIR}")

set(PTGN_EXAMPLES OFF CACHE BOOL "")

add_subdirectory(../../protegon binary_dir)

add_protegon_to(${PROJECT_NAME} ASSETS_DIR "${ASSETS_DIR}")