cmake_minimum_required(VERSION 3.16)
project(e384c LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Useful for bindgen / clangd: emits compile_commands.json in the build dir
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# ---- Locate the C++ library --------------------------------------------
cmake_path(SET E384COMMLIB_PATH_CMAKE "$ENV{E384COMMLIB_PATH}")
list(APPEND CMAKE_PREFIX_PATH "${E384COMMLIB_PATH_CMAKE}")
find_package(e384commlib REQUIRED CONFIG)

# ---- The C shim library --------------------------------------------------
add_library(e384c SHARED
    src/e384c_shim.cpp
)

# E384C_BUILD switches E384C_API to dllexport on Windows
target_compile_definitions(e384c PRIVATE E384C_BUILD)

target_include_directories(e384c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# The C++ lib is an implementation detail: PRIVATE keeps it out of our ABI
target_link_libraries(e384c PRIVATE e384commlib::e384commlib)

# Hide every symbol except the ones we explicitly export (non-Windows)
set_target_properties(e384c PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ---- Install -------------------------------------------------------------
install(TARGETS e384c
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES include/e384c.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
