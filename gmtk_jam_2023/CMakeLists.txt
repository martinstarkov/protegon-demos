cmake_minimum_required(VERSION 3.20)

project(gmtk_jam_2023)

add_executable(gmtk_jam_2023 src/application.cpp)

add_protegon_to(gmtk_jam_2023)
create_resource_symlink(gmtk_jam_2023 "resources")