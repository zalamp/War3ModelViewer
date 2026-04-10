# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-src")
  file(MAKE_DIRECTORY "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-build"
  "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-subbuild/stormlib-populate-prefix"
  "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-subbuild/stormlib-populate-prefix/tmp"
  "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-subbuild/stormlib-populate-prefix/src/stormlib-populate-stamp"
  "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-subbuild/stormlib-populate-prefix/src"
  "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-subbuild/stormlib-populate-prefix/src/stormlib-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-subbuild/stormlib-populate-prefix/src/stormlib-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/IOS/Downloads/War3ModelViewer/build/Desktop_Qt_6_5_0_MinGW_64_bit-Debug/_deps/stormlib-subbuild/stormlib-populate-prefix/src/stormlib-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
