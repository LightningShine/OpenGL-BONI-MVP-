#pragma once
#include "ProView.h"
namespace Pro {
    // Per-sector display state: live while the sector runs, completed value of
    // the current lap, or the previous lap's value until the sector restarts.
    struct SectorSnapshot {
        float t[3];        // seconds; -1 = no data
        bool  live[3];     // sector currently running
        float delta[3];    // vs own best for that sector (completed values only)
        bool  hasDelta[3];
    };
    SectorSnapshot GetSectorSnapshot(int32_t vehicleId);

    void RenderTrackMapWindow(const ProContext& ctx, int32_t vehicleId, ImVec2 vpSz, float topH);
}
