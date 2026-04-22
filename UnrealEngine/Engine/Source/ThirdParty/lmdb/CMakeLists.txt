cmake_minimum_required(VERSION 3.22.1)
project(lmdb)

add_library(lmdb ${CMAKE_SOURCE_DIR}/mdb.c ${CMAKE_SOURCE_DIR}/midl.c)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
	# TODO probably not all of them needed for C library
	set_target_properties(lmdb PROPERTIES COMPILE_FLAGS "-fno-exceptions -fno-rtti -fno-unwind-tables -g -O3")
	set_target_properties(lmdb PROPERTIES LINK_FLAGS_RELEASE "-s")

elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	target_compile_options(lmdb PRIVATE "/O2")
endif()
