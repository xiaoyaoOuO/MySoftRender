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

## 2026-04-01 会话 003

### 实现功能

- 重写 4x MSAA：从近似覆盖率方案改为每像素 4 个 sample 独立深度/颜色缓存。
- 纯色三角形与纹理三角形统一走同一套 MSAA 光栅化流程。
- 解析阶段按 4 个 sample 平均颜色输出像素，深度取最前 sample，修复边缘深度/颜色不一致。

### 修改文件

- include/Rasterizer.h
- src/Rasterizer.cpp

## 2026-04-01 会话 004

### 实现功能

- 将抗锯齿实现调整为传统 MSAA：sample 级覆盖与深度测试，像素级单次着色。
- 移除逐 sample 着色路径，降低纹理采样与着色开销。
- 对边缘像素使用覆盖 sample 的重心点进行一次着色，再写回通过深度测试的 samples。

### 修改文件

- src/Rasterizer.cpp

## 2026-04-01 会话 005

### 实现功能

- 新增独立 OBJ 加载模块（不接入当前渲染流程）。
- 支持解析 v/vt/vn/f，支持负索引，支持多边形面扇形三角化。
- 输出统一网格数据结构（顶点、三角形索引、包围盒、法线/UV可用标记）。
- 当 OBJ 缺少法线时自动生成顶点法线。

### 修改文件

- include/ObjLoader.h
- src/ObjLoader.cpp
- CMakeLists.txt

## 2026-04-01 会话 006

### 实现功能

- 为 ObjLoader 模块补充函数级注释（头文件声明与实现文件中的主要辅助函数）。
- 注释覆盖索引解析、面片 token 解析、包围盒计算、缺失法线生成与主加载入口。

### 修改文件

- include/ObjLoader.h
- src/ObjLoader.cpp

## 2026-04-04 会话 007

### 实现功能

- 为 MSAA 实现补充中文函数注释，明确每个函数的作用与调用方式。
- 注释覆盖采样索引映射、深度/颜色/法线解析，以及三角形 MSAA 光栅化主流程。
- 未修改渲染算法与接口，仅增强代码可读性与维护性。

### 修改文件

- src/Rasterizer.cpp
