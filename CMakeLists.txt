# Arthur32 - merlin32 companion tool for >64K data on the Apple IIgs
#
# Cross-platform build (Win64 / Linux / macOS), vanilla C++17, no dependencies.
#
#   cmake -B build
#   cmake --build build
#
cmake_minimum_required(VERSION 3.15)
project(arthur32 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
	add_compile_options(/W3)
else()
	add_compile_options(-Wall -Wextra)
endif()

add_executable(arthur32
	source/arthur32.cpp
	source/fileio.cpp
	source/omfsegment.cpp
	source/omffile.cpp
)
