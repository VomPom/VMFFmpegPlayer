cmake_minimum_required(VERSION 3.6.0)
project("effect")

# 特效框架源文件
add_library(effect STATIC
    effect_pipeline.cpp
    speed_effect.cpp
)

# 头文件路径（供外部模块引用）
target_include_directories(effect PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 查找系统库
find_library(log-lib log)

# 链接日志库
target_link_libraries(effect
    ${log-lib}
)
