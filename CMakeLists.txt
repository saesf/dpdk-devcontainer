cmake_minimum_required(VERSION 3.0.0)
project(simplefwd VERSION 0.1.0 LANGUAGES C CXX)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Ofast -DNDEBUG -std=c++17 -march=native")
include(CTest)
enable_testing()
include(FetchContent)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0 # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
)
FetchContent_MakeAvailable(yaml-cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDPDK REQUIRED libdpdk)

# add_compile_definitions(PER_FLOW_AGG)
add_executable(simplefwd main.cpp)

target_compile_options(simplefwd PRIVATE ${LIBDPDK_CFLAGS})
target_link_libraries(simplefwd PRIVATE yaml-cpp::yaml-cpp )
target_link_libraries(simplefwd PRIVATE ${LIBDPDK_LDFLAGS})


set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
