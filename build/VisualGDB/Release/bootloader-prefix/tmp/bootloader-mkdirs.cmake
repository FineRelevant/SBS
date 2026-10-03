# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "E:/esp/v6.1/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "E:/esp/v6.1/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "E:/Projects/!Embedded/ESP32/SBS-Solution/SBS-Project/SBS/build/VisualGDB/Release/bootloader"
  "E:/Projects/!Embedded/ESP32/SBS-Solution/SBS-Project/SBS/build/VisualGDB/Release/bootloader-prefix"
  "E:/Projects/!Embedded/ESP32/SBS-Solution/SBS-Project/SBS/build/VisualGDB/Release/bootloader-prefix/tmp"
  "E:/Projects/!Embedded/ESP32/SBS-Solution/SBS-Project/SBS/build/VisualGDB/Release/bootloader-prefix/src/bootloader-stamp"
  "E:/Projects/!Embedded/ESP32/SBS-Solution/SBS-Project/SBS/build/VisualGDB/Release/bootloader-prefix/src"
  "E:/Projects/!Embedded/ESP32/SBS-Solution/SBS-Project/SBS/build/VisualGDB/Release/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/Projects/!Embedded/ESP32/SBS-Solution/SBS-Project/SBS/build/VisualGDB/Release/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/Projects/!Embedded/ESP32/SBS-Solution/SBS-Project/SBS/build/VisualGDB/Release/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
