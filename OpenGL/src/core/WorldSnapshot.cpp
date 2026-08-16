#include "WorldSnapshot.h"

#include <mutex>

namespace world
{
namespace
{
    // Указатель на текущий снимок. Меняется целиком, поэтому читателю
    // достаточно взять копию shared_ptr — данные под ним уже не изменятся.
    // Мьютекс здесь короткий и защищает только сам указатель, а не работу с
    // данными: интерфейс не должен ждать пайплайн ради чтения.
    std::mutex g_mutex;
    std::shared_ptr<const Snapshot> g_current = std::make_shared<const Snapshot>();
}

void publish(std::shared_ptr<const Snapshot> snapshot)
{
    if (!snapshot)
        return;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_current = std::move(snapshot);
}

std::shared_ptr<const Snapshot> current()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_current;
}

const VehicleView* find(const Snapshot& snapshot, int32_t vehicle_id)
{
    const auto it = snapshot.vehicles.find(vehicle_id);
    return (it != snapshot.vehicles.end()) ? &it->second : nullptr;
}

}
