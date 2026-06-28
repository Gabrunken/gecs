set(CMAKE_C_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing")

cmake_minimum_required(VERSION 3.14)
project(gecs)

#Compile GECS
add_library(gecslib STATIC "./src/gecs.c")

target_include_directories(gecslib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

#Compile test
add_executable(test EXCLUDE_FROM_ALL "./test/main.c")

target_link_libraries(test PRIVATE gecslib)
