cmake_minimum_required (VERSION 3.22)

project ("core")

# 强制采用C++20标准
set(CMAKE_CXX_STANDARD 20)

# 三方库
# find_package(OpenCV REQUIRED)

# file(GLOB_RECURSE OpenCVSharedDirectories ${OpenCV_INCLUDE_DIRS}/../x64/mingw/bin/*)

# foreach(OpenCVSharedDirectory ${OpenCVSharedDirectories})
#     if(${OpenCVSharedDirectory} MATCHES ".dll|so$")
#         file(COPY ${OpenCVSharedDirectory} DESTINATION ${PROJECT_ROOT_NODE}/bin)
#     endif()
    #if (${OpenCVSharedDirectory} MATCHES "*.dll|*.so")

    #endif ()
# endforeach()

# 指定最终的DLL文件的位置
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_ROOT_NODE}/bin)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 将源代码添加到此项目的可执行文件。
file(GLOB_RECURSE SRC_LIST
    "${CMAKE_CURRENT_SOURCE_DIR}/include/**.cpp"
)

add_library(${PROJECT_NAME} SHARED ${SRC_LIST})

file(GLOB_RECURSE sources_glob "${CMAKE_CURRENT_SOURCE_DIR}/include/*")
foreach (source ${sources_glob})
    string(REPLACE "${SOURCE_DIR}/" "" RELATIVE_PATH ${source})
    if (NOT(${RELATIVE_PATH} STREQUAL "core.cpp"))
        list(APPEND sources "${source}")
    endif ()

    unset(RELATIVE_PATH)
endforeach ()
unset(sources_glob)

# target_link_libraries(${PROJECT_NAME} PUBLIC ${OpenCV_LIBS})

target_sources(${PROJECT_NAME} PUBLIC ${sources})

message(PROJECT:${PROJECT_NAME},G:${CMAKE_C_COMPILER_VERSION},GXX:${CMAKE_CXX_COMPILER_VERSION},CXX STANDARD:${CMAKE_CXX_STANDARD})
message(final)