cmake_minimum_required(VERSION 3.10)

# set the project name
project(MCU_REC)

# add library
add_subdirectory(lib/FFmpeg)


# add the executable
add_executable(MCU_REC mjr_convertor.cpp)
target_link_libraries(MCU_REC FFmpeg ${EXTRA_LIBS})
