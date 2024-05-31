add_executable(testbeast testbeast.cpp)
target_link_libraries(testbeast $<TARGET_OBJECTS:beast> Fcitx5::Core)
add_test(NAME testbeast COMMAND testbeast)
