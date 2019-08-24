cmake_minimum_required(VERSION 3.5)
project(sdl2_vulkan)

add_definitions(-DNOMINMAX -DSDL_MAIN_HANDLED)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Threads REQUIRED)
find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)

add_spirv_embed_library(spirv_shaders vert.vert frag.frag
	raygen.rgen miss.rmiss hit.rchit)

add_executable(sdl2_vulkan main.cpp)

set_target_properties(sdl2_vulkan PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)

target_include_directories(sdl2_vulkan PUBLIC
	$<BUILD_INTERFACE:${SDL2_INCLUDE_DIR}>)

target_link_libraries(sdl2_vulkan PUBLIC
	spirv_shaders Vulkan::Vulkan ${SDL2_LIBRARY})

