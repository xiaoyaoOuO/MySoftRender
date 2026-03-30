#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
public:
	Camera(
		const glm::vec3& position = glm::vec3(0.0f, 0.0f, 3.0f),
		const glm::vec3& target = glm::vec3(0.0f, 0.0f, 0.0f),
		const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f),
		float fovYDeg = 60.0f,
		float aspectRatio = 16.0f / 9.0f,
		float nearPlane = 0.1f,
		float farPlane = 100.0f)
		: position_(position),
		  target_(target),
		  up_(up),
		  fovYDeg_(fovYDeg),
		  aspectRatio_(aspectRatio),
		  nearPlane_(nearPlane),
		  farPlane_(farPlane)
	{
	}

	const glm::vec3& position() const { return position_; }
	const glm::vec3& target() const { return target_; }
	const glm::vec3& up() const { return up_; }

	void setPosition(const glm::vec3& position) { position_ = position; }
	void setTarget(const glm::vec3& target) { target_ = target; }
	void setUp(const glm::vec3& up) { up_ = up; }

	float fovYDeg() const { return fovYDeg_; }
	float aspectRatio() const { return aspectRatio_; }
	float nearPlane() const { return nearPlane_; }
	float farPlane() const { return farPlane_; }

	void setFovYDeg(float fovYDeg) { fovYDeg_ = fovYDeg; }
	void setAspectRatio(float aspectRatio) { aspectRatio_ = aspectRatio; }
	void setNearPlane(float nearPlane) { nearPlane_ = nearPlane; }
	void setFarPlane(float farPlane) { farPlane_ = farPlane; }

	glm::mat4 viewMatrix() const
	{
		return glm::lookAt(position_, target_, up_);
	}

	glm::mat4 projectionMatrix() const
	{
		return glm::perspective(glm::radians(fovYDeg_), aspectRatio_, nearPlane_, farPlane_);
	}

private:
	glm::vec3 position_;
	glm::vec3 target_;
	glm::vec3 up_;
	float fovYDeg_;
	float aspectRatio_;
	float nearPlane_;
	float farPlane_;
};

