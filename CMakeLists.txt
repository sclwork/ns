cmake_minimum_required(VERSION 2.8.12)

project(ns)

LINK_LIBRARIES(m)

file(GLOB SRC_FILE *.c)
add_executable (${PROJECT_NAME} ${SRC_FILE})
