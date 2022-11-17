cmake_minimum_required(VERSION 3.23)
project(ngx_ziti_module C)

set(CMAKE_C_STANDARD 99)

include(ExternalProject)
include(FetchContent)
find_package(Git REQUIRED)

add_library(${PROJECT_NAME} SHARED ngx_ziti_module.c)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

add_compile_definitions(IS_CMAKE=1)

add_subdirectory("deps")