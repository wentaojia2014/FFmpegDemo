file(GLOB_RECURSE allHeaders  *.h *.hpp)
file(GLOB_RECURSE allSrc *.c *.cpp)
set(sharedHeaders ${allHeaders} CACHE STRING "sharedHeaders")
set(sharedFiles ${allSrc} CACHE STRING "sharedFiles")
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
message("sharedFiles ${sharedFiles}")