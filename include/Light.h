#pragma once
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <array>
#include <vector>


class Light
{
public:
    enum class LightType
    {
        Point,          //点光源
        Directional,    //平行光
        Spot            //聚光灯
    };

protected:
    glm::vec3 position_;
    glm::vec3 color_;
    glm::vec3 direction_;
    float intensity_;
    LightType type_;
    bool castShadowEnabled_ = true;

    // 作用：缓存阴影计算所需的光源观察矩阵、投影矩阵和组合矩阵。
    // 用法：外部通过 lightSpaceMatrix() 读取，供 ShadowMap pass 与着色阶段采样坐标变换使用。
    glm::mat4 lightViewMatrix_ = glm::mat4(1.0f);
    glm::mat4 lightProjectionMatrix_ = glm::mat4(1.0f);
    glm::mat4 lightSpaceMatrix_ = glm::mat4(1.0f);

    // 作用：控制方向光阴影正交体与深度范围。
    // 用法：按场景尺度调节半径/近平面/远平面，以平衡覆盖范围与深度精度。
    float shadowOrthoHalfSize_ = 4.0f;
    float shadowNearPlane_ = 0.1f;
    float shadowFarPlane_ = 20.0f;

    std::vector<float> LightViewDepths_; // 用于存储光源视角下的深度值，辅助阴影计算
    int shadowMapWidth_ = 0;
    int shadowMapHeight_ = 0;



public:
    static constexpr std::size_t kPointShadowFaceCount = 6;

    Light();
    Light(const glm::vec3& position, const glm::vec3& color, const glm::vec3& direction, float intensity, LightType type);
    ~Light() = default;

    const glm::vec3& position() const { return position_; }
    const glm::vec3& color() const { return color_; }
    const glm::vec3& direction() const { return direction_; }
    float intensity() const { return intensity_; }
    LightType type() const { return type_; }

    // 作用：更新光源基础属性，并在必要时刷新阴影矩阵缓存。
    // 用法：运行时改光源位置/方向时调用，ShadowMap 会在下一帧使用新矩阵。
    void setPosition(const glm::vec3& position);
    void setColor(const glm::vec3& color) { color_ = color; }
    void setDirection(const glm::vec3& direction);
    void setIntensity(float intensity) { intensity_ = intensity; }
    void setType(LightType type);

    // 作用：控制该光源是否参与阴影贴图生成。
    // 用法：可在调试时快速对比“仅光照”与“光照+阴影”的画面差异。
    void setCastShadowEnabled(bool enabled) { castShadowEnabled_ = enabled; }
    bool castShadowEnabled() const { return castShadowEnabled_; }

    // 作用：配置方向光阴影正交投影范围。
    // 用法：范围越大覆盖越广但阴影细节越粗，建议按场景大小渐进调优。
    void setShadowOrthoHalfSize(float halfSize);
    void setShadowNearPlane(float nearPlane);
    void setShadowFarPlane(float farPlane);

    float shadowOrthoHalfSize() const { return shadowOrthoHalfSize_; }
    float shadowNearPlane() const { return shadowNearPlane_; }
    float shadowFarPlane() const { return shadowFarPlane_; }

    const glm::mat4& lightViewMatrix() const { return lightViewMatrix_; }
    const glm::mat4& lightProjectionMatrix() const { return lightProjectionMatrix_; }
    const glm::mat4& lightSpaceMatrix() const { return lightSpaceMatrix_; }

    // 作用：访问光源视角深度缓存（ShadowMap 深度图）。
    // 用法：第一个 Pass 写入该缓存，第二个 Pass 可按需读取进行阴影判断。
    std::vector<float>& lightViewDepths() { return LightViewDepths_; }
    const std::vector<float>& lightViewDepths() const { return LightViewDepths_; }

    // 作用：记录阴影贴图实际尺寸，保证采样阶段使用正确的宽高做坐标映射与边界检查。
    // 用法：在第一 Pass 分配/重建阴影深度图后调用，第二 Pass 读取该尺寸进行采样。
    void setShadowMapSize(int width, int height);
    int shadowMapWidth() const { return shadowMapWidth_; }
    int shadowMapHeight() const { return shadowMapHeight_; }

    // 作用：根据当前光源参数重建阴影矩阵。
    // 用法：通常由 setPosition/setDirection/setType 或阴影范围参数修改后自动调用。
    void updateShadowMatrices();

    // 作用：设置点光源阴影每个面的分辨率。
    // 用法：传入正整数分辨率，阴影 pass 会按该尺寸分配 6 面深度缓存。
    void setPointShadowResolution(int resolution);
    int pointShadowResolution() const { return pointShadowResolution_; }

    // 作用：访问点光源阴影某个面的光空间矩阵。
    // 用法：Shadow Pass 与采样阶段按面索引（0~5）读取矩阵。
    const glm::mat4& pointLightSpaceMatrix(std::size_t faceIndex) const;

    // 作用：访问点光源阴影某个面的深度缓存。
    // 用法：第一个 Pass 写入，第二个 Pass 按面索引读取。
    std::vector<float>& pointLightViewDepths(std::size_t faceIndex);
    const std::vector<float>& pointLightViewDepths(std::size_t faceIndex) const;

private:
    int pointShadowResolution_ = 512;
    std::array<glm::mat4, kPointShadowFaceCount> pointLightSpaceMatrices_ = {
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f),
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)
    };
    std::array<std::vector<float>, kPointShadowFaceCount> pointLightViewDepths_;
};

