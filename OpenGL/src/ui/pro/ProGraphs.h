#pragma once
#include "ProView.h"
namespace Pro {
    // Многоканальный график ТЕКУЩЕГО круга (скорость, перегрузки, ускорение,
    // торможение) с ползунком перемотки повтора. Реализация — ProGraphs.cpp.
    void RenderGraphsWindow(const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH);
}
