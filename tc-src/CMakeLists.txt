cmake_minimum_required(VERSION 3.0)

project(tuchuang C CXX)

enable_testing()

# if(NOT CMAKE_BUILD_TYPE)
#   set(CMAKE_BUILD_TYPE "Release")
# endif()

set(CXX_FLAGS
#  -g
 # -DVALGRIND
 -DCHECK_PTHREAD_RETURN_VALUE
 -D_FILE_OFFSET_BITS=64
 -Wall
 -Wextra
 -Werror
 -Wconversion
 -Wno-unused-parameter
 -Wold-style-cast
 -Woverloaded-virtual
 -Wpointer-arith
 -Wshadow
 -Wwrite-strings
 -march=native
 # -MMD
 -std=c++17
 -rdynamic
 )

# 根据不同构建类型设置编译选项（这里只是示例，可按需调整）
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("Compile Debug")
    # 针对 Debug 构建类型，添加一些常见的调试相关编译选项，比如开启调试符号等
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
else()
    message("Compile Release")
    # 针对 Release 构建类型，添加优化相关编译选项，例如开启一定级别的优化
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
endif()


option(BUILD_SHAREFILE "Build sharefile" ON)
option(BUILD_CLIENT "Build client" ON)


if(CMAKE_BUILD_BITS EQUAL 32)
  list(APPEND CXX_FLAGS "-m32")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  list(APPEND CXX_FLAGS "-Wno-null-dereference")
  list(APPEND CXX_FLAGS "-Wno-sign-conversion")
  list(APPEND CXX_FLAGS "-Wno-unused-local-typedef")
  list(APPEND CXX_FLAGS "-Wthread-safety")
  list(REMOVE_ITEM CXX_FLAGS "-rdynamic")
endif()
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")
# 允许debug
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin) # 执行程序文件路径
# set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib) #库文件路径
set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/muduo/lib) #库文件路径

add_definitions(-w) # 先关闭警告


include_directories(${PROJECT_SOURCE_DIR})

#string(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE)
message(STATUS "CXX_FLAGS = " ${CMAKE_CXX_FLAGS} " " ${CMAKE_CXX_FLAGS_${BUILD_TYPE}})

add_subdirectory(muduo/base)
add_subdirectory(muduo/net)

if(BUILD_SHAREFILE)
  add_subdirectory(sharefile)
endif()



if(BUILD_CLIENT)
  add_subdirectory(client)
endif()
