add_subdirectory(amcom)

add_executable(mniam_player main.c)
target_link_libraries(mniam_player PRIVATE am_amcom)
target_link_libraries(mniam_player PRIVATE m)
