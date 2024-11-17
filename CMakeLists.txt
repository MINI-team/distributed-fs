cmake_minimum_required(VERSION 3.10)
project(DistributedFS)

find_package(PkgConfig REQUIRED)

pkg_check_modules(PROTOBUF_C REQUIRED libprotobuf-c)

pkg_check_modules(GLIB REQUIRED glib-2.0)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/shared 
    ${PROTOBUF_C_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

add_subdirectory(shared)
link_libraries(shared ${PROTOBUF_C_LIBRARIES} ${GLIB_LIBRARIES})

add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(replica)