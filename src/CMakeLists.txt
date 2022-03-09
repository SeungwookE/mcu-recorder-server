cmake_minimum_required(VERSION 3.14)

# set the project name
project(MCU_REC C CXX)
set(CMAKE_CXX_STANDARD 14)

# add library
add_subdirectory(lib/FFmpeg)

list(APPEND SOURCES
    src/main.cpp
    src/mjr_processor.h
)
# add the executable
add_executable(MCU_REC MACOSX_BUNDLE WIN32 ${SOURCES})


target_link_libraries(MCU_REC FFmpeg ${EXTRA_LIBS})
