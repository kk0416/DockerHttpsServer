# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\DockerRoboshopServer_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\DockerRoboshopServer_autogen.dir\\ParseCache.txt"
  "DockerRoboshopServer_autogen"
  )
endif()
