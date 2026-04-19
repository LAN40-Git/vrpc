file(GLOB EXAMPLES_SRCS PRIVATE *.cpp)

file(GLOB PROTO_SRCS
        "pb/*.cc"
)

foreach(source ${EXAMPLES_SRCS})
    get_filename_component(basename ${source} NAME_WE)
    add_executable(${basename} ${source})
    target_link_libraries(${basename} ${DEPEND_LIBS} ${PROTO_SRCS})
endforeach ()