# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\War3ModelBatchViewer_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\War3ModelBatchViewer_autogen.dir\\ParseCache.txt"
  "War3ModelBatchViewer_autogen"
  )
endif()
