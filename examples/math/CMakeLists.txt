file(GLOB SRCS PRIVATE
        "*.cpp"
)

file(GLOB API_SRCS PRIVATE
        "../api/mathpb/*.cc"
)

foreach(source ${SRCS})
    get_filename_component(basename ${source} NAME_WE)
    add_executable(${basename} ${source} ${API_SRCS})
    target_link_libraries(${basename} ${DEPEND_LIBS} ${PROTO_SRCS})
endforeach ()