#include "Interpolation.h"
#include <cmath>
#include <iostream>

// Структура для хранения результата интерполяции


// Helper: Calculates the time knot (без изменений)
float GetT(float t, const glm::vec2& p0, const glm::vec2& p1, float alpha)
{
    float a = std::pow((p1.x - p0.x), 2.0f) + std::pow((p1.y - p0.y), 2.0f);
    float b = std::pow(a, 0.5f);
    float c = std::pow(b, alpha);
    if (c < 1e-4f) c = 1.0f;
    return (c + t);
}

// Helper: Standard Linear Interpolation (без изменений)
glm::vec2 Lerp(const glm::vec2& p0, const glm::vec2& p1, float t, float t0, float t1)
{
    if (std::abs(t1 - t0) < 1e-5f) return p0;
    return (p0 * (t1 - t) + p1 * (t - t0)) / (t1 - t0);
}

// НОВАЯ ФУНКЦИЯ: Вычисляет производную (скорость) для Lerp
// v0, v1 - скорости (производные) в точках p0 и p1
glm::vec2 LerpDerivative(const glm::vec2& p0, const glm::vec2& p1,
    const glm::vec2& v0, const glm::vec2& v1,
    float t, float t0, float t1)
{
    float dt = t1 - t0;
    if (std::abs(dt) < 1e-5f) return glm::vec2(0.0f);
    // Формула производной для рекурсивного Lerp
    return ((p1 - p0) + v0 * (t1 - t) + v1 * (t - t0)) / dt;
}


// Изменяем возвращаемый тип на std::vector<SplinePoint>
std::vector<SplinePoint> InterpolatePointsWithTangents(const std::vector<glm::vec2>& originalPoints, int pointsPerSegment, float alpha)
{
    std::vector<SplinePoint> resultPath;
    if (originalPoints.size() < 2) return resultPath;

    for (size_t i = 0; i < originalPoints.size() - 1; i++)
    {
        glm::vec2 p0, p1, p2, p3;
        p1 = originalPoints[i];
        p2 = originalPoints[i + 1];

        if (i == 0) p0 = p1 - (p2 - p1); else p0 = originalPoints[i - 1];
        if (i + 2 < originalPoints.size()) p3 = originalPoints[i + 2]; else p3 = p2 + (p2 - p1);

        float t0 = 0.0f;
        float t1 = GetT(t0, p0, p1, alpha);
        float t2 = GetT(t1, p1, p2, alpha);
        float t3 = GetT(t2, p2, p3, alpha);

        for (int j = 0; j < pointsPerSegment; j++)
        {
            float t = t1 + ((t2 - t1) * ((float)j / (float)pointsPerSegment));

            // --- Позиция (как раньше) ---
            glm::vec2 A1 = Lerp(p0, p1, t, t0, t1);
            glm::vec2 A2 = Lerp(p1, p2, t, t1, t2);
            glm::vec2 A3 = Lerp(p2, p3, t, t2, t3);
            glm::vec2 B1 = Lerp(A1, A2, t, t0, t2);
            glm::vec2 B2 = Lerp(A2, A3, t, t1, t3);
            glm::vec2 C = Lerp(B1, B2, t, t1, t2); // Итоговая позиция

            // --- АНАЛИТИЧЕСКАЯ КАСАТЕЛЬНАЯ (Новое) ---
            // Скорости базовых точек равны 0, т.к. они константы
            glm::vec2 zeroV(0.0f);

            // Производные уровня 1 (скорости между контрольными точками)
            glm::vec2 VA1 = LerpDerivative(p0, p1, zeroV, zeroV, t, t0, t1);
            glm::vec2 VA2 = LerpDerivative(p1, p2, zeroV, zeroV, t, t1, t2);
            glm::vec2 VA3 = LerpDerivative(p2, p3, zeroV, zeroV, t, t2, t3);

            // Производные уровня 2
            glm::vec2 VB1 = LerpDerivative(A1, A2, VA1, VA2, t, t0, t2);
            glm::vec2 VB2 = LerpDerivative(A2, A3, VA2, VA3, t, t1, t3);

            // Производная уровня 3 (Итоговая касательная/скорость)
            glm::vec2 Tangent = LerpDerivative(B1, B2, VB1, VB2, t, t1, t2);

            SplinePoint sp;
            sp.position = C;
            // Важно: нормализуем касательную! Если длина 0, берем предыдущую или дефолтную.
            if (glm::length(Tangent) > 1e-6f) {
                sp.tangent = glm::normalize(Tangent);
            }
            else if (!resultPath.empty()) {
                sp.tangent = resultPath.back().tangent;
            }
            else {
                sp.tangent = glm::vec2(1, 0); // На случай первой точки с нулевой скоростью
            }

            resultPath.push_back(sp);
        }
    }

    // Добавляем самую последнюю точку.
    // Ее касательная будет такой же, как у предыдущей вычисленной точки.
    if (!resultPath.empty()) {
        SplinePoint lastSP;
        lastSP.position = originalPoints.back();
        lastSP.tangent = resultPath.back().tangent;
        resultPath.push_back(lastSP);
    }

    return resultPath;
}

std::vector<glm::vec2> GenerateTriangleStripFromLine(const std::vector<SplinePoint>& splinePoints, float width)
{
    std::vector<glm::vec2> triangleStripPoints;

    if (splinePoints.size() < 2)
    {
        return triangleStripPoints;
    }

    float halfWidth = width * 0.5f;

    for (size_t i = 0; i < splinePoints.size(); i++)
    {
        // Нам больше не нужны сложные if/else для start/end/middle точек.
        // Мы просто берем готовую аналитическую касательную.
        glm::vec2 tangent = splinePoints[i].tangent;

        // Calculate perpendicular vector (rotate 90 degrees ccw)
        // Если tangent = (x, y), то нормаль = (-y, x)
        glm::vec2 normal(-tangent.y, tangent.x);

        // Generate left and right edge points
        // Используем halfWidth для симметричного отступа от центра
        glm::vec2 leftPoint = splinePoints[i].position + normal * halfWidth;
        glm::vec2 rightPoint = splinePoints[i].position - normal * halfWidth;

        // Add points in triangle strip order
        triangleStripPoints.push_back(leftPoint);
        triangleStripPoints.push_back(rightPoint);
    }

    return triangleStripPoints;
}