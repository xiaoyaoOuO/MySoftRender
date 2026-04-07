#pragma once
#include <glm/vec3.hpp>


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



public:
    Light();
    Light(const glm::vec3& position, const glm::vec3& color, const glm::vec3& direction, float intensity, LightType type);
    ~Light() = default;

    const glm::vec3& position() const { return position_; }
    const glm::vec3& color() const { return color_; }
    const glm::vec3& direction() const { return direction_; }
    float intensity() const { return intensity_; }
    LightType type() const { return type_; }

};

