cmake_minimum_required(VERSION 3.9)
project(vix_disklib_sample)

set(CMAKE_CXX_STANDARD 11)
set(INC_DIR /cygdrive/d/Code/Project-C/vmware-vix-disklib-distrib/include)
set(LINK_DIR /cygdrive/d/Code/Project-C/vmware-vix-disklib-distrib/lib64)

include_directories(${INC_DIR})
link_directories(${LINK_DIR})
link_libraries(libvixDiskLib.so)
add_executable(vix_disklib_sample vixDiskLibSample.cpp)
target_link_libraries(vix_disklib_sample libvixDiskLib.so)