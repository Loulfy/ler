//
// Created by loulfy on 08/03/2024.
//

#include "camera.hpp"

namespace ler::cam
{
void Camera::updateViewMatrix()
{
    /*
    glm::mat4 currentMatrix = m_view;

    glm::mat4 rotM = glm::mat4(1.0f);
    glm::mat4 transM;

    rotM = glm::rotate(rotM, glm::radians(m_rotation.x * (m_flipY ? -1.0f : 1.0f)), glm::vec3(1.0f, 0.0f, 0.0f));
    rotM = glm::rotate(rotM, glm::radians(m_rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    rotM = glm::rotate(rotM, glm::radians(m_rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

    glm::vec3 translation = m_position;
    if (m_flipY)
    {
        translation.y *= -1.0f;
    }
    transM = glm::translate(glm::mat4(1.0f), translation);

    if (type == CameraType::firstperson)
    {
        m_view = rotM * transM;
    }
    else
    {
        m_view = transM * rotM;
    }

    m_viewPos = glm::vec4(m_position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
    m_proj = glm::perspective(glm::radians(m_fov), m_viewport.x / m_viewport.y, m_zNear, m_zFar);

    if (m_view != currentMatrix)
    {
        m_updated = true;
    }*/

    // calculate the new Front vector
    glm::vec3 tmp;
    tmp.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    tmp.y = sin(glm::radians(pitch));
    tmp.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(tmp);
    // also re-calculate the Right and Up vector
    right = glm::normalize(glm::cross(front, worldUp));  // normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
    up = glm::normalize(glm::cross(right, front));

    if(m_flipY)
        m_view = glm::lookAtRH(m_position, m_position + front, up);
    else
        m_view = glm::lookAtRH(m_position, m_position + front, up);
    if(m_flipY)
        m_proj = glm::perspectiveRH(glm::radians(m_fov), m_viewport.x / m_viewport.y, m_zNear, m_zFar);
    else
    {
        // LH
        m_proj = glm::perspectiveRH(glm::radians(m_fov), m_viewport.x / m_viewport.y, m_zNear, m_zFar);
    }

    if(m_flipY)
        m_proj[1][1] *= -1;
}

void Camera::update(float deltaTime)
{
    //updateViewMatrix();
}

glm::vec3 Camera::rayCast(const glm::vec2 &pos, glm::vec3 &rayOrigin)
{
    float x = (2.0f * pos.x) / m_viewport.x - 1.0f;
    float y = 1.0f - (2.0f * pos.y) / m_viewport.y;
    float z = 1.0f;
    glm::vec3 ray_nds = glm::vec3(x, y, z);
    glm::vec4 ray_clip = glm::vec4(ray_nds.x, ray_nds.y, -1.0f, 1.0f);
    // eye space to clip we would multiply by projection so
    // clip space to eye space is the inverse projection
    glm::vec4 ray_eye = inverse(m_proj) * ray_clip;
    // convert point to forwards
    ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0f, 0.0f);
    // world space to eye space is usually multiply by view so
    // eye space to world space is inverse view
    glm::vec4 inv_ray_wor = (inverse(m_view) * ray_eye);
    glm::vec3 ray_wor = glm::vec3(inv_ray_wor.x, inv_ray_wor.y, inv_ray_wor.z);
    ray_wor = normalize(ray_wor);
    rayOrigin = m_position;
    return ray_wor;
}

void Camera::handleMouseMove(double x, double y)
{
    /*
    int32_t dx = (int32_t)m_mousePos.x - x;
    int32_t dy = (int32_t)m_mousePos.y - y;

    m_mousePos = glm::vec2(x, y);

    rotate(glm::vec3(dy * m_rotationSpeed, -dx * m_rotationSpeed, 0.0f));
    updateViewMatrix();*/

    if(m_lockMouse)
        return;

    if (m_firstMouse)
    {
        m_last = glm::vec2(x,y);
        m_firstMouse = false;
    }

    float xoffset = x - m_last.x;
    float yoffset = m_last.y - y; // reversed since y-coordinates go from bottom to top

    m_last = glm::vec2(x,y);

    xoffset *= m_rotationSpeed;
    yoffset *= m_rotationSpeed;

    yaw   += xoffset;
    pitch += yoffset;

    // make sure that when pitch is out of bounds, screen doesn't get flipped
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    // update Front, Right and Up Vectors using the updated Euler angles
    updateViewMatrix();
}

void Camera::handleKeyboard(int key, int action, float delta)
{
    if (action == 0)
        return;

    /*float moveSpeed = m_movementSpeed * delta;

    glm::vec3 camFront;
    camFront.x = -cos(glm::radians(m_rotation.x)) * sin(glm::radians(m_rotation.y));
    camFront.y = sin(glm::radians(m_rotation.x));
    camFront.z = cos(glm::radians(m_rotation.x)) * cos(glm::radians(m_rotation.y));
    camFront = glm::normalize(camFront);

    glm::vec3 camRight = glm::normalize(glm::cross(camFront, worldUp));

    if (key == 87) // Forward
        m_position += camFront * moveSpeed;
    if (key == 83) // Backward
        m_position -= camFront * moveSpeed;
    if (key == 65) // Left
        m_position -= camRight * moveSpeed;
    if (key == 68) // Right
        m_position += camRight * moveSpeed;
    if (key == 69) // Down
        m_position.y += 1 * moveSpeed;
    if (key == 340) // Up
        m_position.y -= 1 * moveSpeed;*/

    float velocity = m_movementSpeed * delta;
    if (key == 87) // Forward
        m_position +=  front * velocity;
    if (key == 83) // Backward
        m_position -=  front * velocity;
    if (key == 65) // Left
        m_position -=  right * velocity;
    if (key == 68) // Right
        m_position +=  right * velocity;
    if (key == 69) // Down
        m_position.y += 1 * velocity;
    if (key == 340) // Up
        m_position.y -= 1 * velocity;
}

void Camera::rotate(glm::vec3 delta)
{
    m_rotation += delta;
    updateViewMatrix();
}

void Camera::translate(glm::vec3 delta)
{
    m_position += delta;
    updateViewMatrix();
}

float Camera::getNearClip() const
{
    return m_zNear;
}

float Camera::getFarClip() const
{
    return m_zFar;
}

glm::mat4 Camera::getViewMatrix() const
{
    return m_view;
}

glm::mat4 Camera::getProjMatrix() const
{
    return m_proj;
}

void Camera::setFlipY(bool enable)
{
    m_flipY = enable;
}

void Camera::lockMouse(bool enable)
{
    m_lockMouse = enable;
    m_firstMouse = true;
}
} // namespace ler::cam