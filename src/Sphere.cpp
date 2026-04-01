#include "Sphere.h"

#include <algorithm>
#include <cmath>

namespace {
std::uint64_t EdgeKey(std::uint32_t a, std::uint32_t b)
{
    if (a > b) {
        std::swap(a, b);
    }
    return (static_cast<std::uint64_t>(a) << 32U) | static_cast<std::uint64_t>(b);
}
constexpr float KPi = 3.14159265358979323846f;
}

Sphere::Sphere(
    float radius,
    int subdivisions,
    const glm::vec3& color,
    const glm::vec3& position,
    const glm::vec3& rotation,
    const glm::vec3& scale)
    : Object(position, rotation, scale)
    , radius_(std::max(radius, 1e-4f))
    , subdivisions_(std::max(subdivisions, 0))
    , color_(glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)))
{
    rebuildMesh();
}

void Sphere::setRadius(float radius)
{
    radius_ = std::max(radius, 1e-4f);
    rebuildMesh();
}

void Sphere::setSubdivisions(int subdivisions)
{
    subdivisions_ = std::max(subdivisions, 0);
    rebuildMesh();
}

void Sphere::setColor(const glm::vec3& color)
{
    color_ = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    vertexColors_.assign(vertices_.size(), color_);
}

glm::vec4 Sphere::projectToSphere(const glm::vec3& point) const
{
    const float len2 = glm::dot(point, point);
    if (len2 <= 1e-12f) {
        return glm::vec4(0.0f, radius_, 0.0f, 1.0f);
    }

    const glm::vec3 normalized = glm::normalize(point) * radius_;
    return glm::vec4(normalized, 1.0f);
}

std::uint32_t Sphere::addMidpoint(
    std::uint32_t a,
    std::uint32_t b,
    std::unordered_map<std::uint64_t, std::uint32_t>& midpointCache)
{
    const std::uint64_t key = EdgeKey(a, b);
    auto it = midpointCache.find(key);
    if (it != midpointCache.end()) {
        return it->second;
    }

    const glm::vec3 pa(vertices_[a]);
    const glm::vec3 pb(vertices_[b]);
    const glm::vec3 midpoint = (pa + pb) * 0.5f;

    const std::uint32_t newIndex = static_cast<std::uint32_t>(vertices_.size());
    vertices_.push_back(projectToSphere(midpoint));
    midpointCache.emplace(key, newIndex);
    return newIndex;
}

void Sphere::rebuildMesh()
{
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;

    const std::vector<glm::vec3> baseVertices = {
        {-1.0f,  t,  0.0f}, { 1.0f,  t,  0.0f}, {-1.0f, -t,  0.0f}, { 1.0f, -t,  0.0f},
        { 0.0f, -1.0f,  t}, { 0.0f,  1.0f,  t}, { 0.0f, -1.0f, -t}, { 0.0f,  1.0f, -t},
        { t,  0.0f, -1.0f}, { t,  0.0f,  1.0f}, {-t,  0.0f, -1.0f}, {-t,  0.0f,  1.0f}
    };

    vertices_.clear();
    vertices_.reserve(baseVertices.size());
    for (const glm::vec3& v : baseVertices) {
        vertices_.push_back(projectToSphere(v));
    }
    //vectexs_索引如下，表示一个正二十面体的20个面，每个面由3个顶点组成。
    indices_ = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    for (int i = 0; i < subdivisions_; ++i) {
        std::unordered_map<std::uint64_t, std::uint32_t> midpointCache;
        std::vector<glm::uvec3> subdividedFaces;
        subdividedFaces.reserve(indices_.size() * 4);

        for (const glm::uvec3& tri : indices_) {
            const std::uint32_t a = tri.x;
            const std::uint32_t b = tri.y;
            const std::uint32_t c = tri.z;

            const std::uint32_t ab = addMidpoint(a, b, midpointCache);
            const std::uint32_t bc = addMidpoint(b, c, midpointCache);
            const std::uint32_t ca = addMidpoint(c, a, midpointCache);

            subdividedFaces.emplace_back(a, ab, ca);
            subdividedFaces.emplace_back(b, bc, ab);
            subdividedFaces.emplace_back(c, ca, bc);
            subdividedFaces.emplace_back(ab, bc, ca);
        }

        indices_.swap(subdividedFaces);
    }

    vertexColors_.assign(vertices_.size(), color_);

    vertexUVs_.clear();
    vertexUVs_.reserve(vertices_.size());
    for (const glm::vec4& v : vertices_) {
        vertexUVs_.push_back(computeSphericalUV(glm::vec3(v)));
    }
}

glm::vec2 Sphere::computeSphericalUV(const glm::vec3 &point) const
{
    const float len = glm::dot(point, point);
    if (len <= 1e-12f) {
        return glm::vec2(0.5f, 0.5f);
    }
    glm::vec3 n = glm::normalize(point);
    const float u = std::clamp(std::atan2(n.z, n.x) / (2.0f * KPi) + 0.5f, 0.0f, 1.0f);
    const float v = std::clamp(std::acos(n.y) / KPi, 0.0f, 1.0f);
    return glm::vec2(u, v);
}
