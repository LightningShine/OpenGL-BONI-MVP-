#pragma once
#include "ui/pro/ProView.h"
namespace Pro {
    // Панель LAP INFO переехала в LAPTIME (см. ProLaptime.cpp): позиция, секторы
    // и прошлый круг относятся к текущему кругу, и отдельное окно под них было
    // лишним. Здесь осталась только сводка сессии.
    void RenderSessionInfoWindow(const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH);
}
