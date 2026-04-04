#include "ObjLoader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>

namespace {

// OBJ 面片中的单个顶点引用（v/vt/vn），先存原始索引，再解析为 0-based。
struct VertexRef {
    int positionIndex = -1;
    int texCoordIndex = -1;
    int normalIndex = -1;

    bool operator==(const VertexRef& rhs) const
    {
        return positionIndex == rhs.positionIndex
            && texCoordIndex == rhs.texCoordIndex
            && normalIndex == rhs.normalIndex;
    }
};

// VertexRef 的哈希函数，用于顶点去重哈希表（position/uv/normal 组合键）。
struct VertexRefHash {
    std::size_t operator()(const VertexRef& ref) const
    {
        std::size_t h = static_cast<std::size_t>(std::hash<int>{}(ref.positionIndex));
        h ^= static_cast<std::size_t>(std::hash<int>{}(ref.texCoordIndex) + 0x9e3779b9U + (h << 6U) + (h >> 2U));
        h ^= static_cast<std::size_t>(std::hash<int>{}(ref.normalIndex) + 0x9e3779b9U + (h << 6U) + (h >> 2U));
        return h;
    }
};

// 统一错误输出入口：仅在调用方提供 outError 时写入。
void SetError(std::string* outError, const std::string& message)
{
    if (outError != nullptr) {
        *outError = message;
    }
}

// 去掉行首尾空白字符，便于后续词法解析。
std::string Trim(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1U);
}

// 严格整数解析：要求整个字符串都可被解析为 int。
bool ParseIntStrict(const std::string& text, int& outValue)
{
    if (text.empty()) {
        return false;
    }

    std::size_t consumed = 0;
    try {
        const int value = std::stoi(text, &consumed, 10);
        if (consumed != text.size()) {
            return false;
        }
        outValue = value;
        return true;
    } catch (...) {
        return false;
    }
}

// OBJ 索引转换：支持正索引和负索引，输出为 0-based 下标。
bool ResolveObjIndex(int rawIndex, std::size_t elementCount, int& outResolved)
{
    if (rawIndex == 0) {
        return false;
    }

    if (rawIndex > 0) {
        outResolved = rawIndex - 1;
    } else {
        outResolved = static_cast<int>(elementCount) + rawIndex;
    }

    return outResolved >= 0 && static_cast<std::size_t>(outResolved) < elementCount;
}

// 解析 f 语句中的一个顶点 token（例如 "3/2/1"、"3//1"、"-1/-1/-1"）。
bool ParseFaceToken(const std::string& token, VertexRef& outRef)
{
    std::array<std::string, 3> fields = {"", "", ""};
    std::size_t fieldIndex = 0;
    std::size_t start = 0;

    while (start <= token.size() && fieldIndex < fields.size()) {
        const std::size_t slashPos = token.find('/', start);
        if (slashPos == std::string::npos) {
            fields[fieldIndex++] = token.substr(start);
            break;
        }

        fields[fieldIndex++] = token.substr(start, slashPos - start);
        start = slashPos + 1U;

        if (start == token.size() && fieldIndex < fields.size()) {
            fields[fieldIndex++] = "";
            break;
        }
    }

    int positionRaw = 0;
    if (!ParseIntStrict(fields[0], positionRaw)) {
        return false;
    }

    outRef.positionIndex = positionRaw;
    outRef.texCoordIndex = -1;
    outRef.normalIndex = -1;

    if (!fields[1].empty()) {
        int texRaw = 0;
        if (!ParseIntStrict(fields[1], texRaw)) {
            return false;
        }
        outRef.texCoordIndex = texRaw;
    }

    if (!fields[2].empty()) {
        int normalRaw = 0;
        if (!ParseIntStrict(fields[2], normalRaw)) {
            return false;
        }
        outRef.normalIndex = normalRaw;
    }

    return true;
}

// 计算网格轴对齐包围盒（AABB）。
void ComputeBounds(ObjMeshData& mesh)
{
    if (mesh.vertices.empty()) {
        mesh.boundsMin = glm::vec3(0.0f);
        mesh.boundsMax = glm::vec3(0.0f);
        return;
    }

    mesh.boundsMin = mesh.vertices[0].position;
    mesh.boundsMax = mesh.vertices[0].position;

    for (const Vertex& vertex : mesh.vertices) {
        mesh.boundsMin.x = std::min(mesh.boundsMin.x, vertex.position.x);
        mesh.boundsMin.y = std::min(mesh.boundsMin.y, vertex.position.y);
        mesh.boundsMin.z = std::min(mesh.boundsMin.z, vertex.position.z);

        mesh.boundsMax.x = std::max(mesh.boundsMax.x, vertex.position.x);
        mesh.boundsMax.y = std::max(mesh.boundsMax.y, vertex.position.y);
        mesh.boundsMax.z = std::max(mesh.boundsMax.z, vertex.position.z);
    }
}

// 当 OBJ 缺少法线时，按三角形面积权重近似生成每顶点法线。
void GenerateMissingNormals(ObjMeshData& mesh)
{
    std::vector<glm::vec3> accum(mesh.vertices.size(), glm::vec3(0.0f));

    for (const glm::uvec3& tri : mesh.indices) {
        const std::size_t i0 = static_cast<std::size_t>(tri.x);
        const std::size_t i1 = static_cast<std::size_t>(tri.y);
        const std::size_t i2 = static_cast<std::size_t>(tri.z);

        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            continue;
        }

        const glm::vec3 p0 = mesh.vertices[i0].position;
        const glm::vec3 p1 = mesh.vertices[i1].position;
        const glm::vec3 p2 = mesh.vertices[i2].position;

        const glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);
        if (glm::dot(faceNormal, faceNormal) <= 1e-12f) {
            continue;
        }

        accum[i0] += faceNormal;
        accum[i1] += faceNormal;
        accum[i2] += faceNormal;
    }

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const float len2 = glm::dot(accum[i], accum[i]);
        if (len2 > 1e-12f) {
            mesh.vertices[i].normal = glm::normalize(accum[i]);
        } else {
            mesh.vertices[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    mesh.hasNormals = true;
}

} // namespace

// 重置容器与状态标记，便于复用同一个 ObjMeshData 对象。
void ObjMeshData::clear()
{
    vertices.clear();
    indices.clear();
    boundsMin = glm::vec3(0.0f);
    boundsMax = glm::vec3(0.0f);
    hasTexCoords = false;
    hasNormals = false;
}

// 便捷空检查：任一关键数组为空都不认为是可渲染网格。
bool ObjMeshData::empty() const
{
    return vertices.empty() || indices.empty();
}

// OBJ 主加载入口：解析几何数据并输出统一三角网格。
bool ObjLoader::LoadFromFile(const std::string& filePath, ObjMeshData& outMesh, std::string* outError)
{
    outMesh.clear();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        SetError(outError, "Failed to open OBJ file: " + filePath);
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texCoords;
    std::vector<glm::vec3> normals;

    std::unordered_map<VertexRef, std::uint32_t, VertexRefHash> vertexLut;

    // 将 position/uv/normal 组合引用映射为唯一顶点索引，避免重复顶点。
    auto getOrCreateVertexIndex = [&](const VertexRef& ref) -> std::uint32_t {
        const auto found = vertexLut.find(ref);
        if (found != vertexLut.end()) {
            return found->second;
        }

        const std::uint32_t newIndex = static_cast<std::uint32_t>(outMesh.vertices.size());
        Vertex vertex;
        vertex.position = positions[static_cast<std::size_t>(ref.positionIndex)];
        if (ref.texCoordIndex >= 0) {
            vertex.texCoord = texCoords[static_cast<std::size_t>(ref.texCoordIndex)];
        }
        if (ref.normalIndex >= 0) {
            vertex.normal = normals[static_cast<std::size_t>(ref.normalIndex)];
        }

        outMesh.vertices.push_back(vertex);
        vertexLut.emplace(ref, newIndex);
        return newIndex;
    };

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;

        const std::size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            glm::vec3 p(0.0f);
            if (!(iss >> p.x >> p.y >> p.z)) {
                SetError(outError, "Invalid vertex position at line " + std::to_string(lineNumber));
                return false;
            }
            positions.push_back(p);
            continue;
        }

        if (prefix == "vt") {
            glm::vec2 uv(0.0f);
            if (!(iss >> uv.x >> uv.y)) {
                SetError(outError, "Invalid texture coordinate at line " + std::to_string(lineNumber));
                return false;
            }
            texCoords.push_back(uv);
            continue;
        }

        if (prefix == "vn") {
            glm::vec3 n(0.0f);
            if (!(iss >> n.x >> n.y >> n.z)) {
                SetError(outError, "Invalid normal at line " + std::to_string(lineNumber));
                return false;
            }
            normals.push_back(n);
            continue;
        }

        if (prefix != "f") {
            // 暂不处理 g/o/s/usemtl/mtllib 等语句，读取器按网格几何数据最小集实现。
            continue;
        }

        std::vector<std::string> faceTokens;
        std::string token;
        while (iss >> token) {
            faceTokens.push_back(token);
        }

        if (faceTokens.size() < 3) {
            SetError(outError, "Face has fewer than 3 vertices at line " + std::to_string(lineNumber));
            return false;
        }

        std::vector<VertexRef> faceRefs;
        faceRefs.reserve(faceTokens.size());

        for (const std::string& faceToken : faceTokens) {
            VertexRef rawRef;
            if (!ParseFaceToken(faceToken, rawRef)) {
                SetError(outError, "Invalid face token '" + faceToken + "' at line " + std::to_string(lineNumber));
                return false;
            }

            VertexRef resolvedRef;
            if (!ResolveObjIndex(rawRef.positionIndex, positions.size(), resolvedRef.positionIndex)) {
                SetError(outError, "Position index out of range at line " + std::to_string(lineNumber));
                return false;
            }

            resolvedRef.texCoordIndex = -1;
            if (rawRef.texCoordIndex != -1) {
                if (!ResolveObjIndex(rawRef.texCoordIndex, texCoords.size(), resolvedRef.texCoordIndex)) {
                    SetError(outError, "Texcoord index out of range at line " + std::to_string(lineNumber));
                    return false;
                }
                outMesh.hasTexCoords = true;
            }

            resolvedRef.normalIndex = -1;
            if (rawRef.normalIndex != -1) {
                if (!ResolveObjIndex(rawRef.normalIndex, normals.size(), resolvedRef.normalIndex)) {
                    SetError(outError, "Normal index out of range at line " + std::to_string(lineNumber));
                    return false;
                }
                outMesh.hasNormals = true;
            }

            faceRefs.push_back(resolvedRef);
        }

        // OBJ 支持多边形面，这里使用扇形三角化。
        for (std::size_t i = 1; i + 1 < faceRefs.size(); ++i) {
            const std::uint32_t i0 = getOrCreateVertexIndex(faceRefs[0]);
            const std::uint32_t i1 = getOrCreateVertexIndex(faceRefs[i]);
            const std::uint32_t i2 = getOrCreateVertexIndex(faceRefs[i + 1]);
            outMesh.indices.emplace_back(i0, i1, i2);
        }
    }

    if (outMesh.indices.empty()) {
        SetError(outError, "No face data found in OBJ file: " + filePath);
        return false;
    }

    if (!outMesh.hasNormals) {
        // 文件缺少法线时自动生成，避免后续渲染阶段出现未定义法线。
        GenerateMissingNormals(outMesh);
    }

    ComputeBounds(outMesh);
    SetError(outError, std::string());
    return true;
}
