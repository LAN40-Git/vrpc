cmake_minimum_required(VERSION 3.20)
project(vrpc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(${CMAKE_INSTALL_PREFIX} /usr/local)

message(STATUS "Version: " "${CMAKE_PROJECT_VERSION}")
message(STATUS "C++ standard: " "${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: " "${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Build type: " "${CMAKE_BUILD_TYPE}")

# ====== LIBRARY ======
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(DEPEND_LIBS
        ${LIBURING_LIBRARIES}
)

# ====== EXAMPLES ======
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/examples)
# ====== TESTS ======
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)