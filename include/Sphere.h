#pragma once

#include "Object.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

class Sphere : public Object
{
public:
    Sphere(
        float radius = 0.5f,
        int subdivisions = 1,
        const glm::vec3& color = glm::vec3(0.85f, 0.85f, 1.0f),
        const glm::vec3& position = glm::vec3(0.0f),
        const glm::vec3& rotation = glm::vec3(0.0f),
        const glm::vec3& scale = glm::vec3(1.0f));

    float radius() const { return radius_; }
    int subdivisions() const { return subdivisions_; }
    const glm::vec3& color() const { return color_; }

    void setRadius(float radius);
    void setSubdivisions(int subdivisions);
    void setColor(const glm::vec3& color);

    const std::vector<glm::vec4>& vertices() const { return vertices_; }
    const std::vector<glm::uvec3>& indices() const { return indices_; }
    const std::vector<glm::vec3>& vertexColors() const { return vertexColors_; }
    const std::vector<glm::vec2>& vertexUVs() const { return vertexUVs_; }

private:
    void rebuildMesh();
    glm::vec2 computeSphericalUV(const glm::vec3& point) const;
    std::uint32_t addMidpoint(
        std::uint32_t a,
        std::uint32_t b,
        std::unordered_map<std::uint64_t, std::uint32_t>& midpointCache);
    glm::vec4 projectToSphere(const glm::vec3& point) const;

    float radius_;
    int subdivisions_;
    glm::vec3 color_;
    std::vector<glm::vec4> vertices_;
    std::vector<glm::uvec3> indices_;
    std::vector<glm::vec3> vertexColors_;
    std::vector<glm::vec2> vertexUVs_;
};
