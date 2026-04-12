#include "Light.h"
#include <algorithm>
#include <array>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
// 对方向向量做安全归一化，避免零向量导致 NaN。传入可能为零的方向，函数会回退到默认方向再返回单位向量。
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
  // 对阴影贴图尺寸做非负裁剪，避免后续采样阶段出现非法索引。第一 Pass 生成阴影图后更新，第二 Pass 直接读取该尺寸。
  shadowMapWidth_ = std::max(width, 0);
  shadowMapHeight_ = std::max(height, 0);
}

void Light::setPointShadowResolution(int resolution)
{
  // 限制点光阴影分辨率下限，避免出现 0 尺寸深度图。UI 或渲染流程设置分辨率后调用，后续第一 Pass 会按该值分配 6 面深度缓存。
  pointShadowResolution_ = std::max(resolution, 16);
}

const glm::mat4& Light::pointLightSpaceMatrix(std::size_t faceIndex) const
{
  // 越界访问保护，避免非法面索引导致数组越界。外部传入 0~5 的面索引，越界时回退到 0 号面。
  const std::size_t safeFace = (faceIndex < kPointShadowFaceCount) ? faceIndex : 0U;
  return pointLightSpaceMatrices_[safeFace];
}

std::vector<float>& Light::pointLightViewDepths(std::size_t faceIndex)
{
  const std::size_t safeFace = (faceIndex < kPointShadowFaceCount) ? faceIndex : 0U;
  return pointLightViewDepths_[safeFace];
}

const std::vector<float>& Light::pointLightViewDepths(std::size_t faceIndex) const
{
  const std::size_t safeFace = (faceIndex < kPointShadowFaceCount) ? faceIndex : 0U;
  return pointLightViewDepths_[safeFace];
}

void Light::updateShadowMatrices()
{
  const glm::vec3 lightDir = NormalizeDirectionSafe(direction_);

  if (type_ == LightType::Directional) {
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
    return;
  }

  if (type_ == LightType::Point) {
    // 为点光源构建 90 度透视投影，用于立方体 6 面阴影深度。每次光源位置/阴影近平远平面更新后自动刷新。
    lightProjectionMatrix_ = glm::perspective(glm::radians(90.0f), 1.0f, shadowNearPlane_, shadowFarPlane_);

    const std::array<glm::vec3, kPointShadowFaceCount> faceDirections = {
      glm::vec3(1.0f, 0.0f, 0.0f),
      glm::vec3(-1.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 1.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, -1.0f)
    };

    const std::array<glm::vec3, kPointShadowFaceCount> faceUps = {
      glm::vec3(0.0f, -1.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 1.0f),
      glm::vec3(0.0f, 0.0f, -1.0f),
      glm::vec3(0.0f, -1.0f, 0.0f),
      glm::vec3(0.0f, -1.0f, 0.0f)
    };

    for (std::size_t face = 0; face < kPointShadowFaceCount; ++face) {
      const glm::mat4 faceView = glm::lookAt(position_, position_ + faceDirections[face], faceUps[face]);
      pointLightSpaceMatrices_[face] = lightProjectionMatrix_ * faceView;
    }

    // 兼容旧接口：默认返回 +Z 面矩阵，便于历史路径继续工作。
    lightViewMatrix_ = glm::lookAt(position_, position_ + faceDirections[4], faceUps[4]);
    lightSpaceMatrix_ = pointLightSpaceMatrices_[4];
    return;
  }

  // Spot 光源暂用单视角透视矩阵路径，保持接口完整性。当 type 为 Spot 时，沿 direction_ 方向构建单张阴影投影。
  const glm::vec3 lightTarget = position_ + lightDir;
  const glm::vec3 worldUp = (std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f)
    ? glm::vec3(0.0f, 0.0f, 1.0f)
    : glm::vec3(0.0f, 1.0f, 0.0f);
  lightViewMatrix_ = glm::lookAt(position_, lightTarget, worldUp);
  lightProjectionMatrix_ = glm::perspective(glm::radians(90.0f), 1.0f, shadowNearPlane_, shadowFarPlane_);
  lightSpaceMatrix_ = lightProjectionMatrix_ * lightViewMatrix_;
}
