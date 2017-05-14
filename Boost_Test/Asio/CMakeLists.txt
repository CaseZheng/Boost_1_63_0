#1.指定cmake版本
cmake_minimum_required(VERSION 2.8)

#2.PROJECT命令表明产生的工作空间的名称
PROJECT(Aiso_Client)

MESSAGE(STATUS "Project Name: ${PROJECT_NAME}")               # 打印相关消息消息 
MESSAGE(STATUS "Project Directory: ${PROJECT_SOURCE_DIR}")

SET(CMAKE_BUILE_TYPE DEBUG)                     # 指定编译类型，debug 或者为 release
                                                # debug 版会生成相关调试信息，可以使用 GDB 进行 
                                                # release不会生成调试信息。当无法进行调试时查看此处是否设置为 debug
SET(CMAKE_CXX_FLAGS_DEBUG "-g -Wall")           # 指定编译器 
                                                # CMAKE_C_FLAGS_DEBUG            ----  C 编译器
                                                # CMAKE_CXX_FLAGS_DEBUG        ----  C++ 编译器
                                                # -g：只是编译器，在编译的时候，产生调试信息。
                                                # -Wall：生成所有警告信息。一下是具体的选项，可以单独使用

SET(PROJECT_BINARY_DIR ./build)

#3.头文件目录
INCLUDE_DIRECTORIES(.)

#4.源文件目录
AUX_SOURCE_DIRECTORY(. DIR_SOURCE)

#5.设置环境变量等
#将要编译连接的所有文件 赋值给SOURCE_LIST变量
SET(SOURCE_CLIENT_LIST public.h client.cpp)
SET(SOURCE_SERVER_LIST public.h server.cpp)
SET(SOURCE_TIMER_LIST public.h timer.cpp)

#client最后生成的可执行文件名 赋值给BIN_NAME变量
SET(BIN_CLIENT_NAME client)
SET(BIN_SERVER_NAME server)
SET(BIN_TIMER_NAME timer)

#6.添加要编译的可执行文件 
ADD_EXECUTABLE(${BIN_CLIENT_NAME} ${SOURCE_CLIENT_LIST})
ADD_EXECUTABLE(${BIN_SERVER_NAME} ${SOURCE_SERVER_LIST})
ADD_EXECUTABLE(${BIN_TIMER_NAME} ${SOURCE_TIMER_LIST})

#需要链接的动态库
SET(SO_LIST boost_system)

#7.添加可执行文件所需要的库
TARGET_LINK_LIBRARIES(${BIN_CLIENT_NAME} ${SO_LIST})
TARGET_LINK_LIBRARIES(${BIN_SERVER_NAME} ${SO_LIST})
TARGET_LINK_LIBRARIES(${BIN_TIMER_NAME} ${SO_LIST})
