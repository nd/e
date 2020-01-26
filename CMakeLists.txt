cmake_minimum_required(VERSION 3.14)
project(e C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")

find_package(SDL2 REQUIRED)

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

include_directories(${SDL2_INCLUDE_DIRS} ${SDL_IMAGE_INCLUDE_DIRS}, ${SDL_TTF_INCLUDE_DIRS})
include_directories("/home/nd/p/practice-c/lib")

add_executable(e main.c)
target_link_libraries(e ${SDL2_LIBRARIES} ${SDL_TTF_LIBRARIES} ${FREETYPE_LIBRARIES} m)
