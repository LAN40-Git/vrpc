cmake_minimum_required(VERSION 3.20)
project(vrpc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(${CMAKE_INSTALL_PREFIX} /usr/local)

add_compile_definitions($<$<CONFIG:Debug>:ENABLE_KOSIO_LOG>)

message(STATUS "Version: " "${CMAKE_PROJECT_VERSION}")
message(STATUS "C++ standard: " "${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: " "${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Build type: " "${CMAKE_BUILD_TYPE}")

# ====== LIBRARY ======
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)
find_package(Protobuf REQUIRED)
find_package(TBB REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(DEPEND_LIBS
        ${LIBURING_LIBRARIES}
        ${PROTOBUF_LIBRARIES}
        tbb
)

# ====== INSTALL ======
include(GNUInstallDirs)

install(DIRECTORY vrpc/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vrpc
)

# ====== UNINSTALL ======
if(NOT TARGET uninstall)
    configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake
            ${CMAKE_BINARY_DIR}/uninstall.cmake
            COPYONLY
    )

    add_custom_target(uninstall
            COMMAND ${CMAKE_COMMAND}
            -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -P ${CMAKE_BINARY_DIR}/uninstall.cmake
            COMMENT "卸载 vrpc 头文件"
    )
endif()

# ====== EXAMPLES ======
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/examples)
# ====== TESTS ======
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)