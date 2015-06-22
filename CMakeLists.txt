cmake_minimum_required(VERSION 2.6)
set(TARGET gen)
set(CMAKE_CXX_FLAGS "-Wall -pedantic -g -pthread -std=c++0x -O3")

set(SOURCE
  ${PROJECT_SOURCE_DIR}/main.cpp
)

add_executable(${TARGET} ${SOURCE})
target_link_libraries(${TARGET} ${REQUIRED_LIBRARIES})
