cmake_minimum_required(VERSION 3.20)

project(gmtk_jam_2023)

add_executable(gmtk_jam_2023 src/application.cpp)

target_link_libraries(gmtk_jam_2023 PRIVATE protegon)

create_protegon_symlink(gmtk_jam_2023 "resources")