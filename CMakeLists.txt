set(CMAKE_C_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing")

cmake_minimum_required(VERSION 3.14)
project(gecs)

include(FetchContent)

FetchContent_Declare(
    gdslib
    GIT_REPOSITORY https://github.com/gabrunken/gds.git
    GIT_TAG main
)

FetchContent_MakeAvailable(gdslib)

#Compile GECS
add_library(gecslib STATIC "./src/gecs.c")

target_include_directories(gecslib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gecslib PUBLIC gdslib)

#Compile test
add_executable(test EXCLUDE_FROM_ALL "./test/main.c")

target_link_libraries(test PRIVATE gecslib)
