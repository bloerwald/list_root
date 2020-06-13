if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  cmake_minimum_required(VERSION 3.3)

  set (CMAKE_CXX_STANDARD 11)
  set (CMAKE_CXX_STANDARD_REQUIRED YES)

  list (APPEND CMAKE_CXX_FLAGS "-fcolor-diagnostics")

  include (cmake/add_macros.cmake)
  include (cmake/parse_arguments.cmake)
  include (cmake/beautify_find_boost.cmake)

  find_boost (REQUIRED COMPONENTS filesystem system)
endif()

extended_add_library (NAME lookup3 SOURCES "lookup3.cpp" INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR})
set_property (TARGET lookup3 APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR})

extended_add_executable (NAME list_root SOURCES "list_root.cpp" LIBRARIES lookup3 Boost::filesystem Boost::system)

extended_add_executable (NAME jenkins SOURCES "jenkins.cpp" LIBRARIES lookup3)
