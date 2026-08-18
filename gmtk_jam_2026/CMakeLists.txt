cmake_minimum_required(VERSION 3.28)

project(gmtk_jam_2026 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(
  SRC_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set(
  ASSETS_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
)

set(
  PROJECT_FILE
  "${CMAKE_CURRENT_SOURCE_DIR}/projectname.ptgnproj"
)

set(
  PROTEGON_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../protegon"
)

file(
  GLOB_RECURSE
  SRC_FILES
  CONFIGURE_DEPENDS
  LIST_DIRECTORIES false
  "${SRC_DIR}/*.h"
  "${SRC_DIR}/*.cpp"
)

add_executable(
  ${PROJECT_NAME}
  ${SRC_FILES}
)

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE
    "${SRC_DIR}"
)

# External applications do not build Protegon's examples.
set(
  PTGN_EXAMPLES
  ""
  CACHE STRING
  "Protegon examples to build"
  FORCE
)

add_subdirectory(
  "${PROTEGON_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/protegon"
)

add_protegon_to(
  ${PROJECT_NAME}
  ASSETS_DIR "${ASSETS_DIR}"
)

# add_protegon_to() preloads Assets for Emscripten, but the .ptgnproj
# lives one directory above Assets and therefore must also be mounted.
#
# Distribution builds handle their project mount separately.
if(
  EMSCRIPTEN
  AND NOT PTGN_DISTRIBUTION_BUILD
)
  target_link_options(
    ${PROJECT_NAME}
    PRIVATE
      "--preload-file=${PROJECT_FILE}@/projectname.ptgnproj"
  )
endif()