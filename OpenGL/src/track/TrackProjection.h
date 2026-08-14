#pragma once

#include "../rendering/Interpolation.h"

#include <cstddef>
#include <vector>

// ============================================================================
// Проекция позиции машины на трек: прогресс круга и близость к трассе.
//
// Вынесено из телеметрии отдельно и БЕЗ глобалов: это чистая геометрия, её
// нужно уметь проверять на столе. Вызывается по разу на каждый принятый пакет,
// то есть до тысячи раз в секунду, поэтому здесь важны два свойства:
//   * никакого копирования трека на вызов — геометрия готовится один раз;
//   * поиск ближайшего сегмента идёт от подсказки, а не по всему треку.
// ============================================================================
namespace track
{
    /// Подготовленная геометрия трека: точки плюс накопленные длины.
    struct Geometry
    {
        std::vector<SplinePoint> points;
        std::vector<float> cumulative_distances;  // длина от старта до точки i
        float total_length = 0.0f;

        /// Пересчитывает накопленные длины под новый набор точек.
        void build(std::vector<SplinePoint> new_points);

        bool valid() const;

        size_t segment_count() const;
    };

    /// Прогресс 0..1 вдоль трека для позиции в кадре трека.
    /// `segment_hint` — сегмент, найденный для этой машины в прошлый раз;
    /// функция его обновляет. На каждую машину нужен свой.
    /// Возвращает 0.0, если геометрия непригодна.
    double progress_at(const Geometry& geometry, const glm::vec2& position, size_t& segment_hint);

    /// Находится ли точка ближе radius_meters к трассе.
    bool is_near_track(const Geometry& geometry, const glm::vec2& position, double radius_meters);

    /// Пересекает ли отрезок движения [move_from, move_to] отрезок линии
    /// [line_a, line_b]. `out_fraction` — доля пути, на которой это произошло:
    /// именно она, а не время кадра, задаёт момент пересечения.
    /// Чистая геометрия, вынесена сюда, чтобы приём пакетов и хронометраж
    /// пользовались одной реализацией.
    bool segment_intersection(const glm::vec2& move_from, const glm::vec2& move_to,
                              const glm::vec2& line_a, const glm::vec2& line_b,
                              float& out_fraction);
}
