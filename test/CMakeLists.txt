add_executable(testbeast testbeast.cpp)
target_link_libraries(testbeast Fcitx5::Core beast)
add_test(NAME testbeast COMMAND testbeast)
