# 对话实现记录

用于记录每一次对话中实现的功能与修改文件。

## 2026-04-01 会话 001

### 实现功能

- 新增 Texture2D：支持从文件加载纹理、程序化棋盘纹理生成、双线性采样。
- 光栅器新增纹理三角形路径：支持 UV 插值并进行纹理采样。
- Sphere 网格新增球面 UV 生成，支持球体贴图采样。
- 渲染器新增球体纹理接口：优先尝试加载 assets/earth.jpg，找不到时自动回退到程序化棋盘纹理。
- 默认关闭线框叠加，程序启动后可直接看到球体纹理（按 F1 可切换线框）。

### 修改文件

- CMakeLists.txt
- include/Sphere.h
- src/Sphere.cpp
- include/Texture.h
- src/Texture.cpp
- include/Rasterizer.h
- src/Rasterizer.cpp
- include/software_renderer.h
- src/software_renderer.cpp
- src/main.cpp

## 2026-04-01 会话 002

### 实现功能

- 修复球体纹理路径查找：支持在不同运行目录下自动查找项目根目录的 assets/earth.jpg。
- 启动时输出实际命中的纹理路径，便于确认加载来源。

### 修改文件

- src/main.cpp
