#pragma once

#include <glm/glm.hpp>

/**
 * @brief 对象级材质参数，当前用于 IBL 的金属度与粗糙度控制。
 */
struct Material
{
    glm::vec3 albedo{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};