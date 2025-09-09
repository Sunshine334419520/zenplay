#!/bin/bash

# 批量替换SDL renderer中的日志调用

# 替换std::cerr
sed -i 's/std::cerr << \(.*\) << std::endl;/MODULE_ERROR(LOG_MODULE_RENDERER, \1);/g' src/player/video/render/impl/sdl_renderer.cpp

# 替换std::cout
sed -i 's/std::cout << \(.*\) << std::endl;/MODULE_INFO(LOG_MODULE_RENDERER, \1);/g' src/player/video/render/impl/sdl_renderer.cpp

echo "SDL renderer日志替换完成"
