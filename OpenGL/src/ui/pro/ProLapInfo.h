#pragma once
#include "ProView.h"
namespace Pro {
    void RenderLapInfoWindow    (const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH);
    void RenderSessionInfoWindow(const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH);
}
