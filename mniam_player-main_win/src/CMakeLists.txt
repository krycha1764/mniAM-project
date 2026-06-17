cmake_minimum_required(VERSION 3.10)
project(mniam_player)

add_subdirectory(amcom)

add_executable(mniam_player main.c)

# Dodana biblioteka ws2_32 na samym końcu!
target_link_libraries(mniam_player PRIVATE am_amcom ws2_32)