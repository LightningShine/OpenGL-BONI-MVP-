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
#include <UTMUPS.hpp>
//#include <GeographicLib/UTMUPS.hpp>



void Input(
    std::string cordinate,
    std::mutex& pointsMutex,
    std::atomic<bool>& running);

void InputDataOpenGL(
    std::vector<glm::vec2>& points,
    std::mutex& pointsMutex,
    std::atomic<bool>& running
);

void CordinatesToDecimalFormat(std::string line);

void CordinatesToUTM(float CordinateX, float CordinateY);


