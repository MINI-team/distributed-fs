add_executable(replica replica.c)

add_custom_target(create_symlink ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_SOURCE_DIR}/data_replica1
    ${CMAKE_BINARY_DIR}/replica/data_replica1
)

add_dependencies(replica create_symlink)