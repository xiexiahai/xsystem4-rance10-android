cmake_minimum_required(VERSION 3.22)
project(sunset_archive C)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(ENABLE_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
foreach(feature TAR CPIO CAT UNZIP ZIP OPENSSL NETTLE MBEDTLS LIBB2
                LZMA ZSTD LZ4 LZO BZip2 BZIP2 LIBXML2 EXPAT ICONV PCREPOSIX
                ACL XATTR)
    set(ENABLE_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${LIBARCHIVE_SOURCE_DIR}" libarchive EXCLUDE_FROM_ALL)

add_library(sunsetarchive SHARED sunset_archive.c)
target_link_libraries(sunsetarchive PRIVATE archive_static)
target_compile_options(sunsetarchive PRIVATE -Wall -Wextra -fvisibility=hidden)
target_link_options(sunsetarchive PRIVATE -Wl,-z,max-page-size=16384)
