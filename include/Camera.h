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

	void setPosition(const glm::vec3& position) { position_ = position; viewMatrixDirty_ = true; }
	void setTarget(const glm::vec3& target) { target_ = target; viewMatrixDirty_ = true; }
	void setUp(const glm::vec3& up) { up_ = up; viewMatrixDirty_ = true; }

	float fovYDeg() const { return fovYDeg_; }
	float aspectRatio() const { return aspectRatio_; }
	float nearPlane() const { return nearPlane_; }
	float farPlane() const { return farPlane_; }

	void setFovYDeg(float fovYDeg) { fovYDeg_ = fovYDeg; projectionMatrixDirty_ = true; }
	void setAspectRatio(float aspectRatio) { aspectRatio_ = aspectRatio; projectionMatrixDirty_ = true; }
	void setNearPlane(float nearPlane) { nearPlane_ = nearPlane; projectionMatrixDirty_ = true; }
	void setFarPlane(float farPlane) { farPlane_ = farPlane; projectionMatrixDirty_ = true; }

	float speed() const { return speed_; }
	void setSpeed(float speed) { speed_ = speed; }

	glm::mat4 viewMatrix() const
	{
		if (viewMatrixDirty_) {
			viewMatrixCache_ = glm::lookAt(position_, target_, up_);
			viewMatrixDirty_ = false;
		}
		return viewMatrixCache_;
	}

	glm::mat4 projectionMatrix() const
	{
		if (projectionMatrixDirty_) {
			projectionMatrixCache_ = glm::perspective(glm::radians(fovYDeg_), aspectRatio_, nearPlane_, farPlane_);
			projectionMatrixDirty_ = false;
		}
		return projectionMatrixCache_;
	}

	void move(const glm::vec3& delta)
	{
		position_ += delta;
		target_ += delta;
		viewMatrixDirty_ = true;
	}

	void rotate(float yawDeg, float pitchDeg)
	{
		glm::vec3 forward = glm::normalize(target_ - position_);
		glm::vec3 right = glm::normalize(glm::cross(forward, up_));

		// Yaw rotation around the up vector
		if (yawDeg != 0.0f) {
			float yawRad = glm::radians(yawDeg);
			glm::mat4 yawRotation = glm::rotate(glm::mat4(1.0f), yawRad, up_);
			forward = glm::vec3(yawRotation * glm::vec4(forward, 0.0f));
		}

		// Pitch rotation around the right vector
		if (pitchDeg != 0.0f) {
			float pitchRad = glm::radians(pitchDeg);
			glm::mat4 pitchRotation = glm::rotate(glm::mat4(1.0f), pitchRad, right);
			forward = glm::vec3(pitchRotation * glm::vec4(forward, 0.0f));
			up_ = glm::normalize(glm::cross(right, forward)); // Recalculate up vector
		}

		target_ = position_ + forward;
		viewMatrixDirty_ = true;
	}

private:
	glm::vec3 position_;
	glm::vec3 target_;
	glm::vec3 up_;

	float fovYDeg_;
	float aspectRatio_;
	float nearPlane_;
	float farPlane_;

	float speed_ = 2.0f; // units per second

	mutable glm::mat4 viewMatrixCache_;
	mutable glm::mat4 projectionMatrixCache_;
	mutable bool viewMatrixDirty_ = true;
	mutable bool projectionMatrixDirty_ = true;
};

