#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

// OBJ 文件中一个顶点在渲染阶段常用的属性集合。
struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texCoord{0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    // 缓存顶点在投影阶段的 1/w，用于片段属性透视校正插值。由投影函数在每帧写入，光栅化阶段按该值修正 worldPos/UV 等属性插值。
    float invW{1.0f};
};

// OBJ 解析后的网格数据：顶点列表 + 三角形索引。
struct ObjMeshData {
    std::vector<Vertex> vertices;
    std::vector<glm::uvec3> indices;

    // 包围盒，便于后续做缩放、居中或剔除。
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    bool hasTexCoords = false;
    bool hasNormals = false;

    // 清空当前网格数据，并重置所有状态标记与包围盒。
    void clear();

    // 判断网格数据是否为空：顶点或索引任一为空都视为空网格。
    bool empty() const;
};

class ObjLoader
{
public:
    // 从磁盘加载 OBJ 文件。
    // 成功返回 true；失败返回 false，并将错误写入 outError（如果提供）。
    static bool LoadFromFile(const std::string& filePath, ObjMeshData& outMesh, std::string* outError = nullptr);
};
