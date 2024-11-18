add_executable(client client.c)

# add_custom_target(create_symlink_client ALL
#     COMMAND ${CMAKE_COMMAND} -E create_symlink_client
#     ${CMAKE_SOURCE_DIR}/data_client
#     ${CMAKE_BINARY_DIR}/client/data_client
# )

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/client/data_client
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_SOURCE_DIR}/data_client
    ${CMAKE_BINARY_DIR}/client/data_client
    DEPENDS ${CMAKE_SOURCE_DIR}/data_client
    COMMENT "Creating symlink for client data"
)

add_custom_target(create_symlink_client ALL
    DEPENDS ${CMAKE_BINARY_DIR}/client/data_client
)

add_dependencies(client create_symlink_client)
