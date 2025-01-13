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

# if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
#     add_compile_options(
#         -Wall                # Enable all common warnings
#         -Wextra              # Enable extra warnings
#         -Wpedantic           # Enforce ISO C++ standard compliance
#         -Werror              # Treat all warnings as errors
#         -Werror=unused-variable
#         -Werror=unused-parameter
#         -Werror=unused-function
#         -Wshadow             # Warn about variable shadowing
#         -Wnon-virtual-dtor   # Warn if a class with virtual functions lacks a virtual destructor
#         -Wcast-align         # Warn about potential performance issues with cast alignment
#         -Wpointer-arith      # Warn about arithmetic on void* or function pointers
#         -Wformat=2           # Enable stricter format checks
#         -Wconversion         # Warn on implicit type conversions
#         -Wsign-conversion    # Warn on implicit conversions between signed and unsigned types
#         -Wnull-dereference   # Warn about null pointer dereferences
#         -Woverloaded-virtual # Warn if a base class function is hidden by a derived class
#         -Wvla                # Warn about usage of variable-length arrays
#     )
# endif()


add_subdirectory(shared)
link_libraries(shared ${PROTOBUF_C_LIBRARIES} ${GLIB_LIBRARIES} pthread)

add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(replica)


# cmake_minimum_required(VERSION 3.10)
# project(DistributedFS)

# # Apply aggressive warning flags and treat warnings as errors, depending on compiler
# if(MSVC)
#     # MSVC: /W4 -> enable most warnings, /WX -> treat warnings as errors
#     add_compile_options(/W4 /WX)
# else()
#     # GCC/Clang: -Wall -> most warnings, -Wextra -> extra warnings,
#     #            -Wpedantic -> pedantic warnings, -Werror -> treat warnings as errors
#     add_compile_options(-Wall -Wextra -Wpedantic -Werror)
# endif()

# find_package(PkgConfig REQUIRED)
# pkg_check_modules(PROTOBUF_C REQUIRED libprotobuf-c)
# pkg_check_modules(GLIB REQUIRED glib-2.0)

# include_directories(
#     ${CMAKE_SOURCE_DIR}/include
#     ${CMAKE_SOURCE_DIR}/shared
#     ${PROTOBUF_C_INCLUDE_DIRS}
#     ${GLIB_INCLUDE_DIRS}
# )

# add_subdirectory(shared)
# link_libraries(shared ${PROTOBUF_C_LIBRARIES} ${GLIB_LIBRARIES} pthread)

# add_subdirectory(server)
# add_subdirectory(client)
# add_subdirectory(replica)
