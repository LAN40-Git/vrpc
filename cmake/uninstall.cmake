if(NOT EXISTS "${CMAKE_BINARY_DIR}/install_manifest.txt")
    message(FATAL_ERROR "找不到 install_manifest.txt，无法卸载！")
endif()

file(READ "${CMAKE_BINARY_DIR}/install_manifest.txt" files)
string(REGEX REPLACE "\n" ";" files "${files}")

foreach(file ${files})
    message(STATUS "卸载文件：${file}")
    if(EXISTS "${file}")
        file(REMOVE "${file}")
    else()
        message(STATUS "文件已不存在：${file}")
    endif()
endforeach()

# 删除空目录
execute_process(
        COMMAND find ${CMAKE_INSTALL_PREFIX}/include/vrpc -type d -empty -delete
        OUTPUT_QUIET ERROR_QUIET
)

message(STATUS "卸载完成！")