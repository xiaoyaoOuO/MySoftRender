#pragma once

#include "Object.h"
#include "ObjLoader.h"

/**
 * @brief 通用网格对象：以单对象持有 vertices+indices，避免“每个三角形一个对象”的高开销。
 */
class MeshObject : public Object
{
public:
    MeshObject() = default;

    MeshObject(
        const ObjMeshData& mesh,
        const glm::vec3& position = glm::vec3(0.0f),
        const glm::vec3& rotation = glm::vec3(0.0f),
        const glm::vec3& scale = glm::vec3(1.0f))
        : Object(position, rotation, scale)
        , mesh_(mesh)
    {
    }

    // 作用：设置网格数据。
    // 用法：传入 ObjLoader 输出的 ObjMeshData，渲染器会按 indices 遍历并绘制三角形。
    void setMesh(const ObjMeshData& mesh)
    {
        mesh_ = mesh;
    }

    // 作用：只读访问网格数据。
    // 用法：渲染阶段读取顶点和索引并进行统一光栅化。
    const ObjMeshData& mesh() const
    {
        return mesh_;
    }

    // 作用：判断网格是否可渲染。
    // 用法：返回 true 表示 vertices/indices 均非空。
    bool hasMesh() const
    {
        return !mesh_.empty();
    }

private:
    ObjMeshData mesh_;
};
