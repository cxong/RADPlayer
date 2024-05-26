cmake_minimum_required(VERSION 3.15)

project(RADPlayer)

if(UNIX AND NOT APPLE)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fsanitize=leak -fsanitize=undefined")
endif()

# TODO: convert to SDL2

if(MSVC)
  set(COMPILE_OPTIONS -MP -W4 -WX -wd\"4090\" -wd\"4996\" -wd\"4204\" /D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING /Zc:__cplusplus -lwinmm)
else()
  set(COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Weffc++)
endif()

add_executable(radplayer
	example.cpp)
target_link_options(radplayer PRIVATE -dynamicbase winmm.lib)
