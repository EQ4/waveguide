cmake_minimum_required(VERSION 3.0)

file(GLOB sources "*.cpp")

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    "/usr/local/include/"
)

add_library(common STATIC ${sources})

if(APPLE)
    find_library(opencl_library OpenCL)
    mark_as_advanced(opencl_library)
    set(frameworks ${opencl_library})
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
elseif(UNIX)
    find_library(opencl_library OpenCL PATHS ENV LD_LIBRARY_PATH ENV OpenCL_LIBPATH)
    set(frameworks ${opencl_library})
endif()

find_library(assimp_library assimp)
find_library(fftw_library fftw3f)
set(libraries ${assimp_library} ${fftw_library} z ${frameworks})

target_link_libraries(common ${libraries})
