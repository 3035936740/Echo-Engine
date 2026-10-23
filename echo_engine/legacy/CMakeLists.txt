cmake_minimum_required (VERSION 3.22)

project ("EchoLegacy")

# 获取当前源代码目录的绝对路径
get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" ABSOLUTE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 三方库
find_package(SFML COMPONENTS system window graphics network audio REQUIRED)

# SFML_INCLUDE_DIRS为自定义的目录
file(GLOB SFML_DLL "${SFML_INCLUDE_DIRS}/bin/*")
file(COPY ${SFML_DLL} DESTINATION "${PROJECT_ROOT_NODE}/bin")


# 遍历目录并将路径添加到 include_directories 中
foreach(INCLUDE_DIR ${LIBRARY_INCLUDES})
    if(IS_DIRECTORY ${INCLUDE_DIR})
        include_directories(${INCLUDE_DIR}/include)
    endif()
endforeach()

# 以当前目录为基准
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# 指定最终的可执行文件的位置
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_ROOT_NODE}/bin)

# 将源代码添加到此项目的可执行文件。
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src SRC_LIST)

add_executable(${PROJECT_NAME} ${SRC_LIST})

# 添加多个CPP/HPP/H
set(sources)

file(GLOB_RECURSE sources_glob "${CMAKE_CURRENT_SOURCE_DIR}/src/*")

foreach (source ${sources_glob})
    string(REPLACE "${SOURCE_DIR}/" "" RELATIVE_PATH ${source})
    if (NOT(${RELATIVE_PATH} STREQUAL "main.cpp"))
        list(APPEND sources "${source}")
    endif ()

    unset(RELATIVE_PATH)
endforeach ()

target_sources(${PROJECT_NAME} PUBLIC ${sources})

target_link_libraries(${PROJECT_NAME} PUBLIC sfml-window sfml-graphics sfml-audio core)

unset(SOURCE_DIR)
unset(sources_glob)
unset(sources)

message(PROJECT:${PROJECT_NAME},G:${CMAKE_C_COMPILER_VERSION},GXX:${CMAKE_CXX_COMPILER_VERSION},CXX STANDARD:${CMAKE_CXX_STANDARD})
message(final)