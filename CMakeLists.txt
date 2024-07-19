cmake_minimum_required(VERSION 3.0.0)
project(tunnel VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
enable_testing()


find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDPDK REQUIRED libdpdk)

add_executable(tunnel main.cpp)

target_compile_options(tunnel PRIVATE ${LIBDPDK_CFLAGS})
target_link_libraries(tunnel ${LIBDPDK_LDFLAGS})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
