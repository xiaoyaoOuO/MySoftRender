# mySoftRender

这是一个可开窗口、实时渲染的软光栅器起步工程。渲染全部在 CPU 完成，再把颜色缓冲上传到 SDL 纹理进行显示。

当前已集成：
- GLM: 数学库（向量/矩阵）
- stb_image: 贴图加载
- Dear ImGui: 运行时调试面板（SDL2 + SDL_Renderer 后端）

## 推荐技术栈

核心必选：
- SDL2: 窗口、输入事件、显示帧缓冲

已接入增强库：
- GLM: 线代和矩阵运算
- stb_image: 纹理加载
- Dear ImGui: 调试面板和实时参数调节

可继续按需加入：
- tinyobjloader: 读取 OBJ 模型

## Ubuntu 环境安装

```bash
sudo apt update
sudo apt install -y build-essential cmake libsdl2-dev gdb
```

说明：首次执行 CMake 配置时会通过 `FetchContent` 自动下载 `glm/stb/imgui`，需要可用网络。

## 构建与运行

```bash
cmake -S . -B build
cmake --build build -j
./build/mySoftRender
```

Release 构建与运行：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
./build-release/mySoftRender
```

可选：测试 `stb_image` 加载（将图片路径作为参数传入）：

```bash
./build/mySoftRender /path/to/texture.png
```

## VS Code 使用

- 直接运行默认构建任务: `cmake build`
- 调试配置: `Debug mySoftRender`

## 运行时功能

- 按 `ESC` 退出
- ImGui 面板可实时调整背景色，并显示当前 FPS
- 可勾选显示 ImGui Demo 窗口

## 你接下来可以做什么

1. 在 `SoftwareRenderer` 中加入深度缓冲和背面剔除
2. 为三角形增加顶点颜色插值
3. 加入 MVP 变换和透视除法
4. 增加 OBJ 加载并绘制网格
