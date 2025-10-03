cmake_minimum_required(VERSION 3.22)

include(cmake/AppleBuild.cmake)

project(
  ImGraph
  DESCRIPTION "A graphing calculator written in C++ using a user interface built with Dear ImGui"
  VERSION 1.0.0
  LANGUAGES CXX)

set(PROJECT_COMPANY_NAME "OPCODE")
set(PROJECT_COMPANY_NAMESPACE "in.ac.iiitbh.gymkhana.opcode")  # Reverse domain name notation

include(cmake/StandardProjectSettings.cmake)
include(GNUInstallDirs)

# Link this "library" to use the warnings specified in CompilerWarnings.cmake.
add_library(project_warnings INTERFACE)
include(cmake/CompilerWarnings.cmake)
set_project_warnings(project_warnings)

enable_testing()

add_subdirectory(packaging)
add_subdirectory(vendor)
add_subdirectory(src)
