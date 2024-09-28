include(CMakeDependentOption)
include(GNUInstallDirs)
include(FixInterfaceIncludes)

set(SOURCES_HIGHSCORE
    melonDS-highscore.cpp
    Platform.cpp

    ../glad/glad.c
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(highscore-1 REQUIRED IMPORTED_TARGET highscore-1)
pkg_check_modules(gobject-2.0 REQUIRED IMPORTED_TARGET gobject-2.0)

fix_interface_includes(PkgConfig::highscore-1)

add_library(melonDS-highscore MODULE ${SOURCES_HIGHSCORE})

set_target_properties(melonDS-highscore PROPERTIES PREFIX "")
target_include_directories(melonDS-highscore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(melonDS-highscore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(melonDS-highscore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(melonDS-highscore PRIVATE core)
target_link_libraries(melonDS-highscore PRIVATE PkgConfig::highscore-1 PkgConfig::gobject-2.0)
target_link_libraries(melonDS-highscore PRIVATE ${CMAKE_DL_LIBS})

set(MELONDS_GL_HEADER \"frontend/glad/glad.h\" CACHE STRING "Path to a header that contains OpenGL function and type declarations.")

target_compile_definitions(melonDS-highscore PUBLIC MELONDS_GL_HEADER=${MELONDS_GL_HEADER})
target_compile_definitions(core PUBLIC MELONDS_GL_HEADER=${MELONDS_GL_HEADER})

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/melonDS.highscore DESTINATION ${CMAKE_INSTALL_LIBDIR}/highscore/cores)
install(TARGETS melonDS-highscore LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/highscore/cores)
