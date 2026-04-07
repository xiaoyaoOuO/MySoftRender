#include "Light.h"

Light::Light()
    : position_(0.0f, 0.0f, 0.0f),
      color_(1.0f, 1.0f, 1.0f),
      direction_(0.0f, -1.0f, 0.0f),
      intensity_(1.0f),
      type_(LightType::Point)
{
}

Light::Light(const glm::vec3& position, const glm::vec3& color, const glm::vec3& direction, float intensity, LightType type)
    : position_(position),
      color_(color),
      direction_(direction),
      intensity_(intensity),
      type_(type)
{
}