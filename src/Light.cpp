#include "Light.h"
#include <algorithm>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
// 作用：对方向向量做安全归一化，避免零向量导致 NaN。
// 用法：传入可能为零的方向，函数会回退到默认方向再返回单位向量。
glm::vec3 NormalizeDirectionSafe(const glm::vec3& direction)
{
  const float len2 = glm::dot(direction, direction);
  if (len2 <= 1e-12f) {
    return glm::vec3(0.0f, -1.0f, 0.0f);
  }
  return glm::normalize(direction);
}
}

Light::Light()
    : position_(0.0f, 0.0f, 0.0f),
      color_(1.0f, 1.0f, 1.0f),
      direction_(0.0f, -1.0f, 0.0f),
      intensity_(1.0f),
      type_(LightType::Point)
{
  updateShadowMatrices();
}

Light::Light(const glm::vec3& position, const glm::vec3& color, const glm::vec3& direction, float intensity, LightType type)
    : position_(position),
      color_(color),
      direction_(direction),
      intensity_(intensity),
      type_(type)
{
  updateShadowMatrices();
}

void Light::setPosition(const glm::vec3& position)
{
  position_ = position;
  updateShadowMatrices();
}

void Light::setDirection(const glm::vec3& direction)
{
  direction_ = direction;
  updateShadowMatrices();
}

void Light::setType(LightType type)
{
  type_ = type;
  updateShadowMatrices();
}

void Light::setShadowOrthoHalfSize(float halfSize)
{
  shadowOrthoHalfSize_ = std::max(halfSize, 0.1f);
  updateShadowMatrices();
}

void Light::setShadowNearPlane(float nearPlane)
{
  shadowNearPlane_ = std::max(nearPlane, 0.01f);
  if (shadowFarPlane_ <= shadowNearPlane_ + 0.01f) {
    shadowFarPlane_ = shadowNearPlane_ + 0.01f;
  }
  updateShadowMatrices();
}

void Light::setShadowFarPlane(float farPlane)
{
  shadowFarPlane_ = std::max(farPlane, shadowNearPlane_ + 0.01f);
  updateShadowMatrices();
}

void Light::setShadowMapSize(int width, int height)
{
  // 作用：对阴影贴图尺寸做非负裁剪，避免后续采样阶段出现非法索引。
  // 用法：第一 Pass 生成阴影图后更新，第二 Pass 直接读取该尺寸。
  shadowMapWidth_ = std::max(width, 0);
  shadowMapHeight_ = std::max(height, 0);
}

void Light::updateShadowMatrices()
{
  const glm::vec3 lightDir = NormalizeDirectionSafe(direction_);
  const glm::vec3 lightTarget = position_ + lightDir;
  const glm::vec3 worldUp = (std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f)
    ? glm::vec3(0.0f, 0.0f, 1.0f)
    : glm::vec3(0.0f, 1.0f, 0.0f);

  lightViewMatrix_ = glm::lookAt(position_, lightTarget, worldUp);

  lightProjectionMatrix_ = glm::ortho(
    -shadowOrthoHalfSize_,
    shadowOrthoHalfSize_,
    -shadowOrthoHalfSize_,
    shadowOrthoHalfSize_,
    shadowNearPlane_,
    shadowFarPlane_);
  

  lightSpaceMatrix_ = lightProjectionMatrix_ * lightViewMatrix_;
}
