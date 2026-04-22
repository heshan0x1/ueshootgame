# Instructions for updating the CMake build files

- Remove setting the output directories in the root CMakeLists.txt
	> set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) <br/>
	> set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) <br/>
	> set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

- Suppress the deprecation warnings in non-MSVC compilers in the root CMakeLists.txt
	> set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wnon-virtual-dtor **-Wno-deprecated-declarations**")

- Add in the new custom target that only builds the libraries (not zello_world) to the root CMakeLists.txt
	> add_custom_target(libraries_only) <br/>
	> add_dependencies(libraries_only ze_loader ze_null ze_tracing_layer ze_validation_layer)

- Remove any and all instances of setting the target properties in **all** CMakeLists.txt, so we have .so files without the version appended,
	e.g. 
	> set_target_properties(${TARGET_LOADER_NAME} PROPERTIES <br/>
	>    VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}" <br/>
	>    SOVERSION "${PROJECT_VERSION_MAJOR}"
	>)