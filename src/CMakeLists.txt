
if (${ADDON_TYPE} STREQUAL "StaticLibrary")
    add_library(webserver STATIC webserver.cpp)
    target_link_libraries(webserver Fcitx5::Core)
endif()

if (${ADDON_TYPE} STREQUAL "SharedLibrary")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_library(webserver SHARED webserver.cpp config/config.cpp)
    target_link_libraries(webserver Fcitx5::Core nlohmann_json)
    target_compile_definitions(webserver PUBLIC FCITX_BEAST_IS_SHARED)
    install(TARGETS webserver DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
endif()

configure_file(webserver.conf.in.in webserver.conf.in @ONLY)
fcitx5_translate_desktop_file(${CMAKE_CURRENT_BINARY_DIR}/webserver.conf.in webserver.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/webserver.conf" DESTINATION "${CMAKE_INSTALL_PREFIX}/share/fcitx5/addon")
