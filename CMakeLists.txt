cmake_minimum_required(VERSION 3.14)
project(gecs)

#Compile GECS
add_library(gecslib STATIC "./src/gecs.c")

target_include_directories(gecslib PUBLIC include)
target_link_libraries(gecslib PUBLIC sparse_set)

#Compile test
add_executable(test EXCLUDE_FROM_ALL "./test/main.c")

target_link_libraries(test PRIVATE gecslib)
