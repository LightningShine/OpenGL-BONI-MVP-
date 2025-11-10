#pragma once
#include <vector>
#include <mutex>
#include <atomic>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <sstream>
#include <string>
#include <proj/proj.h>

float lat_degX = 0;
float lon_degY = 0;

void InputData(
    std::vector<glm::vec2>& points,
    std::mutex& pointsMutex,
    std::atomic<bool>& running
);

void CordinatesToOpenGLFormat(std::atomic<bool>& running);

void CordinatesToUTM(std::atomic<bool>& running);


