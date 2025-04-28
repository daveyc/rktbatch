cmake_minimum_required(VERSION 3.29)
project(rktbatch)

set(CMAKE_CXX_STANDARD 17)

add_executable(rktbatch main.cpp)

set(ENV{ZOS_HEADERS_PATH} "~/zos_include")

include_directories(./include)

add_definitions(
        -D_EXT
        -D_LP64
        -D__ptr32=
        -D__ptr64=
        -D__LONGNAME__
        -D__MVS__
        -D__IBMC__
        -D__packed=
        -D_XOPEN_SOURCE_EXTENDED
        -D_ALL_SOURCE
        -D__POSIX_SOURCE
        -D_OPEN_MSGQ_EXT
        -D__ibmxl__
        -D__clang__
)

target_include_directories (rktbatch PUBLIC include)
target_include_directories (rktbatch PUBLIC argparse/include)
target_include_directories (rktbatch PUBLIC spdlog/include)

add_subdirectory(argparse)
add_subdirectory(spdlog)

find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIR})
target_link_libraries(rktbatch PRIVATE ${PNG_LIBRARY})
