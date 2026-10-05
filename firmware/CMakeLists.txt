cmake_minimum_required(VERSION 3.16)
add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5basspiezohat VERSION 0.2.0)
