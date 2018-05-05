cmake_minimum_required(VERSION 3.9)
project(vix_disklib_sample)

set(CMAKE_CXX_STANDARD 11)
#设置vddk库include目录，这里的目录得使用cygwin得目录模式，不能使用传统windows目录比如C:\之类的
set(INC_DIR /cygdrive/d/Code/Project-C/vmware-vix-disklib-distrib/include)
#设置vddk库lib目录
set(LINK_DIR /cygdrive/d/Code/Project-C/vmware-vix-disklib-distrib/lib64)

include_directories(${INC_DIR})
link_directories(${LINK_DIR})
link_libraries(libvixDiskLib.so)
add_executable(vix_disklib_sample vixDiskLibSample.cpp)
target_link_libraries(vix_disklib_sample libvixDiskLib.so)