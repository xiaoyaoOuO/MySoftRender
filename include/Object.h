#pragma once

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Texture2D;

/**
 * @brief 代表场景中一个物体的基类，包含位置、旋转和缩放属性。
 */
class Object
{
public:
    Object(
        const glm::vec3& pos = glm::vec3(0.0f),
        const glm::vec3& rot = glm::vec3(0.0f),
        const glm::vec3& scl = glm::vec3(1.0f))
        : position(pos), rotation(rot), scale(scl) {}

    virtual ~Object();

    const glm::vec3& getPosition() const { return position; }
    const glm::vec3& getRotation() const { return rotation; }
    const glm::vec3& getScale() const { return scale; }

    virtual void setPosition(const glm::vec3& pos) { position = pos; modelMatrixDirty = true; }
    virtual void setRotation(const glm::vec3& rot) { rotation = rot; modelMatrixDirty = true; }
    virtual void setScale(const glm::vec3& scl) { scale = scl; modelMatrixDirty = true; }

    // 作用：为对象绑定纹理资源。
    // 用法：传入共享纹理指针后，渲染阶段可按对象读取并使用该纹理。
    void setTexture(const std::shared_ptr<Texture2D>& texture) { texture_ = texture; }

    // 作用：清除对象当前绑定的纹理。
    // 用法：调用后对象将退回无纹理渲染路径。
    void clearTexture() { texture_.reset(); }

    // 作用：判断对象是否已绑定纹理。
    // 用法：渲染器据此决定是否启用纹理采样路径。
    bool hasTexture() const { return static_cast<bool>(texture_); }

    // 作用：控制对象是否参与阴影投射。
    // 用法：调试辅助对象（如光源可视化小球）可关闭阴影投射，避免污染场景阴影结果。
    void setCastShadow(bool enabled) { castShadow_ = enabled; }

    // 作用：查询对象是否参与阴影投射。
    // 用法：Shadow Pass 中据此决定是否写入阴影深度。
    bool castShadow() const { return castShadow_; }

    // 作用：获取对象绑定的纹理共享指针。
    // 用法：渲染器通过 get() 拿到 Texture2D* 传给光栅器。
    const std::shared_ptr<Texture2D>& texture() const { return texture_; }

    void translate(const glm::vec3& delta) { position += delta; modelMatrixDirty = true; }
    void rotate(const glm::vec3& deltaEulerDeg) { rotation += deltaEulerDeg; modelMatrixDirty = true; }

    glm::mat4 modelMatrix() const
    {
        if(!modelMatrixDirty){
            return cachedModelMatrix;
        }
        glm::mat4 model(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, scale);
        cachedModelMatrix = model;
        modelMatrixDirty = false;
        return cachedModelMatrix;
    }

protected:
    glm::vec3 position; // 物体在世界空间中的位置
    glm::vec3 rotation; // 物体的旋转（欧拉角）
    glm::vec3 scale;    // 物体的缩放
    std::shared_ptr<Texture2D> texture_; // 对象绑定的纹理资源（可为空）
    bool castShadow_ = true; // 对象是否参与阴影投射

    //缓存model矩阵以及dirty标志位，避免每帧重复计算
    mutable glm::mat4 cachedModelMatrix;
    mutable bool modelMatrixDirty = true;
};