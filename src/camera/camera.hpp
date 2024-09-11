//
// Created by loulfy on 08/03/2024.
//

#pragma once

#include "log/log.hpp"

#define GLM_FORCE_RADIANS
//#define GLM_FORCE_LEFT_HANDED
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ler::cam
{
class Camera
{
  public:
    void updateViewMatrix();
    void update(float deltaTime);
    glm::vec3 rayCast(const glm::vec2& pos, glm::vec3& rayOrigin);

    void handleMouseMove(double x, double y);
    void handleKeyboard(int key, int action, float delta);

    void rotate(glm::vec3 delta);
    void translate(glm::vec3 delta);

    [[nodiscard]] float getNearClip() const;
    [[nodiscard]] float getFarClip() const;
    [[nodiscard]] glm::mat4 getViewMatrix() const;
    [[nodiscard]] glm::mat4 getProjMatrix() const;

    enum class CameraType { lookat, firstperson };
    CameraType type = CameraType::lookat;

    void setFlipY(bool enable);
    void lockMouse(bool enable);

  private:
    glm::vec3 m_rotation = glm::vec3();
    glm::vec3 m_position = glm::vec3(0, 0, 5);
    //glm::vec3 m_position = glm::vec3(-5.558739, 2.529600, 0.233079);
    glm::vec2 m_viewport = glm::vec2(1280, 720);
    glm::vec4 m_viewPos = glm::vec4();

    glm::mat4 m_proj = glm::mat4(1.f);
    glm::mat4 m_view = glm::mat4(1.f);

    float m_fov = 55.f;
    float m_zNear = 0.01f;  // 0.1f;
    float m_zFar = 10000.f; // 48.f;

    float m_rotationSpeed = 0.1f;
    float m_movementSpeed = 80.f;

    glm::vec2 m_mousePos = glm::vec2(0.f);

    bool m_updated = true;
    bool m_flipY = false;

    static constexpr glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(1.f);
    //glm::vec3 front = glm::vec3(0.997764, 0.008726, -0.066271);
    //glm::vec3 up = glm::vec3(-0.008707, 0.999962, 0.000578);
    glm::vec3 right = glm::vec3(1.f);

    // euler angles
    float yaw = -90.f;
    float pitch = 0.f;

    bool m_lockMouse = true;
    bool m_firstMouse = true;
    glm::vec2 m_last = glm::vec2(0,0);
};

using CameraPtr = std::shared_ptr<Camera>;

} // namespace ler::cam
