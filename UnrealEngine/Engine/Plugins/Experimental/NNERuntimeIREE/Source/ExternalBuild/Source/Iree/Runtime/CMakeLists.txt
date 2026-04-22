cmake_minimum_required(VERSION 3.21...3.24)
project(IREE_RUNTIME_WINDOWS)

# Prevent an issue of not correctly included modules
include(CheckCSourceCompiles)

# Include main module
include(iree_runtime)