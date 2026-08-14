#include "TrackProjection.h"

#include "../Config.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace track
{
namespace
{
    // Полуокно поиска вокруг подсказки, в сегментах. Между пакетами машина
    // проезжает считанные метры, так что запас многократный.
    constexpr size_t SEGMENT_SEARCH_WINDOW = 24;

    // Если в окне ничего ближе этого расстояния нет, подсказка считается
    // устаревшей и трек просматривается целиком. 25 м при MAP_SIZE = 100 м.
    constexpr double HINT_MAX_DISTANCE_NORM = 0.25;

    // Вырожденный сегмент (две совпавшие точки): направление из него не
    // восстановить, а деление на его длину даст бесконечность.
    constexpr float MIN_SEGMENT_LENGTH_SQ = 1e-10f;

    struct Projection
    {
        size_t index = 0;
        float  along = 0.0f;                                        // положение внутри сегмента, 0..1
        double distance_sq = std::numeric_limits<double>::infinity();
    };

    Projection project_on_segment(const Geometry& geometry, const glm::vec2& p, size_t index)
    {
        const glm::vec2 a = geometry.points[index].position;
        const glm::vec2 b = geometry.points[index + 1].position;
        const glm::vec2 ab = b - a;
        const float length_sq = glm::dot(ab, ab);

        Projection result;
        result.index = index;
        if (length_sq < MIN_SEGMENT_LENGTH_SQ)
            return result;  // distance_sq остаётся бесконечным — сегмент не выиграет

        result.along = glm::clamp(glm::dot(p - a, ab) / length_sq, 0.0f, 1.0f);
        const glm::vec2 delta = p - (a + ab * result.along);
        result.distance_sq = static_cast<double>(glm::dot(delta, delta));
        return result;
    }

    Projection project_on_all(const Geometry& geometry, const glm::vec2& p)
    {
        Projection best;
        const size_t count = geometry.segment_count();
        for (size_t i = 0; i < count; ++i)
        {
            const Projection candidate = project_on_segment(geometry, p, i);
            if (candidate.distance_sq < best.distance_sq)
                best = candidate;
        }
        return best;
    }

    /// Лучшая проекция в окне вокруг подсказки.
    ///
    /// Окно ЗАВОРАЧИВАЕТСЯ через конец трека, и это не мелочь: трек замкнут, и
    /// машина, только что пересёкшая старт/финиш, физически стоит у сегмента 0,
    /// тогда как подсказка ещё указывает на последний. Без заворота ближайшим
    /// оказывался бы конец трека (проекция упирается в его крайнюю точку),
    /// прогресс держался бы на 1.0 ещё десятки метров, и круг засчитывался бы
    /// с опозданием и с неверным временем.
    /// Идём ОТ подсказки наружу, а не слева направо по окну. Разница проявляется
    /// на границе сегментов, где расстояния равны: побеждает ближайший к
    /// подсказке, то есть тот, где машина была мгновение назад. Иначе прогресс
    /// в точке ровно на стыке скакал бы между 0.0 и 1.0 в зависимости от порядка
    /// обхода — а это как раз линия старт/финиша.
    Projection project_around_hint(const Geometry& geometry, const glm::vec2& p, size_t hint)
    {
        const size_t count = geometry.segment_count();
        const size_t reach = std::min(SEGMENT_SEARCH_WINDOW, count / 2);

        Projection best = project_on_segment(geometry, p, hint % count);
        for (size_t step = 1; step <= reach; ++step)
        {
            const size_t forward = (hint + step) % count;
            const size_t backward = (hint + count - step) % count;

            const Projection ahead = project_on_segment(geometry, p, forward);
            if (ahead.distance_sq < best.distance_sq)
                best = ahead;

            const Projection behind = project_on_segment(geometry, p, backward);
            if (behind.distance_sq < best.distance_sq)
                best = behind;
        }
        return best;
    }
}

void Geometry::build(std::vector<SplinePoint> new_points)
{
    points = std::move(new_points);
    cumulative_distances.clear();
    total_length = 0.0f;

    if (points.size() < 2)
        return;

    cumulative_distances.reserve(points.size());
    cumulative_distances.push_back(0.0f);

    float total = 0.0f;
    for (size_t i = 1; i < points.size(); ++i)
    {
        total += glm::distance(points[i - 1].position, points[i].position);
        cumulative_distances.push_back(total);
    }
    total_length = total;
}

bool Geometry::valid() const
{
    return points.size() >= 2 && total_length > 1e-6f;
}

size_t Geometry::segment_count() const
{
    return points.size() >= 2 ? points.size() - 1 : 0;
}

double progress_at(const Geometry& geometry, const glm::vec2& position, size_t& segment_hint)
{
    if (!geometry.valid())
        return 0.0;

    const size_t count = geometry.segment_count();

    Projection best;
    if (segment_hint < count)
    {
        best = project_around_hint(geometry, position, segment_hint);
        if (best.distance_sq > HINT_MAX_DISTANCE_NORM * HINT_MAX_DISTANCE_NORM)
            best = project_on_all(geometry, position);
    }
    else
    {
        best = project_on_all(geometry, position);
    }

    if (best.distance_sq == std::numeric_limits<double>::infinity())
        return 0.0;  // весь трек вырожденный

    segment_hint = best.index;

    const glm::vec2 a = geometry.points[best.index].position;
    const glm::vec2 b = geometry.points[best.index + 1].position;
    const double distance_along =
        static_cast<double>(geometry.cumulative_distances[best.index]) +
        static_cast<double>(glm::distance(a, b) * best.along);

    return std::clamp(distance_along / static_cast<double>(geometry.total_length), 0.0, 1.0);
}

bool segment_intersection(const glm::vec2& move_from, const glm::vec2& move_to,
                          const glm::vec2& line_a, const glm::vec2& line_b,
                          float& out_fraction)
{
    const glm::vec2 move = move_to - move_from;
    const glm::vec2 line = line_b - line_a;

    const float denominator = (-line.x * move.y) + (move.x * line.y);
    if (std::abs(denominator) < 1e-6f)
        return false;  // параллельны или машина не сдвинулась

    const glm::vec2 delta = move_from - line_a;
    const float t = ((-line.y * delta.x) + (line.x * delta.y)) / denominator;  // вдоль движения
    const float u = ((-move.y * delta.x) + (move.x * delta.y)) / denominator;  // вдоль линии

    if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f)
        return false;

    out_fraction = t;
    return true;
}

bool is_near_track(const Geometry& geometry, const glm::vec2& position, double radius_meters)
{
    if (geometry.points.size() < 2)
        return false;

    const double radius_norm = radius_meters / MapConstants::MAP_SIZE;
    const double radius_sq = radius_norm * radius_norm;

    // Ранний выход: машина на трассе отсеивается первым же сегментом, полный
    // проход остаётся только для действительно далёких точек.
    const size_t count = geometry.segment_count();
    for (size_t i = 0; i < count; ++i)
    {
        if (project_on_segment(geometry, position, i).distance_sq <= radius_sq)
            return true;
    }
    return false;
}

}
