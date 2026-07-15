#include "ProSectors.h"
#include "../../rendering/Interpolation.h"
#include "../../vehicle/Vehicle.h"
#include <imgui.h>
#include <mutex>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>

extern std::vector<SplinePoint> g_smooth_track_points;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

// ── Mini-sector delta palette ───────────────────────────────────────────────
// Track is split into SEC_ZONES mini-sectors; each is colored by how the driver
// performed in it versus their personal best lap (and the overall session best).
static constexpr int   SEC_ZONES  = 48;
static constexpr ImU32 SEC_PURPLE = IM_COL32(177, 156, 224, 255); // fastest of all
static constexpr ImU32 SEC_GREEN  = IM_COL32( 62, 142,  71, 255); // beats own best
static constexpr ImU32 SEC_YELLOW = IM_COL32(218, 165,  64, 255); // < 1s off best
static constexpr ImU32 SEC_RED    = IM_COL32(193,  60,  53, 255); // > 1s off best
static constexpr ImU32 SEC_NONE   = IM_COL32( 70,  70,  70, 255); // no comparison data

static constexpr float SEC_EPS    = 0.005f; // tie tolerance (s)

// Per-zone traversal time for one lap.
//   out[k]   = time spent in mini-sector k (seconds)
//   valid[k] = true only when BOTH boundaries of zone k were actually crossed
//
// Samples are stored in TIME order; their `progress` is NOT guaranteed monotonic
// (position noise / braking / standing still can push it backward). So we walk
// the samples once, tracking the running-MAX progress, and record the time each
// zone boundary was first reached. A completed zone's time is then fixed forever,
// regardless of what the car does later — no flicker on hard braking/acceleration.
static void zoneTimes(const std::vector<LapInfo>& s,
                      float out[SEC_ZONES], bool valid[SEC_ZONES]) {
    for (int k = 0; k < SEC_ZONES; ++k) { out[k] = 0.f; valid[k] = false; }
    if (s.size() < 2) return;

    float bt[SEC_ZONES + 1];
    for (int i = 0; i <= SEC_ZONES; ++i) bt[i] = -1.f;

    double prevMaxP = s.front().progress;
    float  prevT    = s.front().timefromstart;
    int    nb       = 0;
    // Boundaries already behind the first sample are reached at lap start.
    while (nb <= SEC_ZONES && (double)nb / SEC_ZONES <= prevMaxP) bt[nb++] = prevT;

    for (size_t i = 1; i < s.size(); ++i) {
        double p = s[i].progress; if (p < prevMaxP) p = prevMaxP; // running max
        float  t = s[i].timefromstart;
        while (nb <= SEC_ZONES && (double)nb / SEC_ZONES <= p) {
            double bp    = (double)nb / SEC_ZONES;
            double denom = p - prevMaxP;
            double frac  = denom > 1e-9 ? (bp - prevMaxP) / denom : 1.0;
            bt[nb++] = prevT + (t - prevT) * (float)frac;
        }
        prevMaxP = p; prevT = t;
    }

    for (int k = 0; k < SEC_ZONES; ++k)
        if (bt[k] >= 0.f && bt[k + 1] >= 0.f) { out[k] = bt[k + 1] - bt[k]; valid[k] = true; }
}

void RenderSectorsWindow(const ProContext& ctx, int32_t vehicleId,
                          ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({635.f * ui, topH + 600.f * ui}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({550.f * ui, 225.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({140.f * ui, 100.f * ui}, {vpSz.x, vpSz.y});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(COL_BG_WIDGET));
    if (!ImGui::Begin("##Sectors", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); ImGui::PopStyleColor(); return;
    }

    float w = ImGui::GetWindowWidth();
    float h = ImGui::GetWindowHeight();
    float z = PanelZoom("Sectors");
    DrawPanelHeader(ctx, "SECTORS", false, nullptr, z);

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      base = ImGui::GetCursorScreenPos();

    // Reserved for the color legend at the bottom
    const float labelH = 18.f * z;
    float mapW = w;
    float mapH = h - header_h() - 2.f - labelH;

    dl->AddRectFilled(base, {base.x + mapW, base.y + mapH}, COL_BG);

    // ── Compute per-zone delta colors under the vehicles lock ──────────────────
    ImU32 zoneCol[SEC_ZONES];
    for (int k = 0; k < SEC_ZONES; ++k) zoneCol[k] = SEC_NONE;
    {
        float bestZ[SEC_ZONES]; bool bestV[SEC_ZONES] = { false };
        float dispZ[SEC_ZONES]; bool zValid[SEC_ZONES] = { false };
        float sessZ[SEC_ZONES]; bool sessV[SEC_ZONES] = { false };

        std::lock_guard<std::mutex> lk(g_vehicles_mutex);

        auto it = g_vehicles.find(vehicleId);
        if (it != g_vehicles.end()) {
            Vehicle& v = it->second;

            // Personal best lap → reference times (full lap → all zones valid)
            if (v.bestlapID >= 0) {
                auto bit = v.laps.find(v.bestlapID);
                if (bit != v.laps.end()) zoneTimes(bit->second.samples, bestZ, bestV);
            }

            // F1-style live map: color ONLY the current lap, zone by zone as the
            // driver passes through each one. A zone is colored once both of its
            // boundaries are crossed; unreached zones stay neutral and the map
            // resets each lap. So a slow lap shows green where pace matched and
            // yellow/red only in the mini-sectors where time was actually lost.
            auto cit = v.laps.find(v.m_current_lap_number);
            if (cit != v.laps.end())
                zoneTimes(cit->second.samples, dispZ, zValid);
        }

        // Overall session best per zone (across every car's best lap)
        for (auto& [id, v] : g_vehicles) {
            if (v.bestlapID < 0) continue;
            auto bit = v.laps.find(v.bestlapID);
            if (bit == v.laps.end()) continue;
            float z[SEC_ZONES]; bool zv[SEC_ZONES];
            zoneTimes(bit->second.samples, z, zv);
            for (int k = 0; k < SEC_ZONES; ++k) {
                if (!zv[k]) continue;
                if (!sessV[k] || z[k] < sessZ[k]) { sessZ[k] = z[k]; sessV[k] = true; }
            }
        }

        for (int k = 0; k < SEC_ZONES; ++k) {
            if (!zValid[k] || !bestV[k]) continue;          // no data / no reference
            float delta = dispZ[k] - bestZ[k];
            if (sessV[k] && dispZ[k] <= sessZ[k] + SEC_EPS)  zoneCol[k] = SEC_PURPLE;
            else if (delta <= SEC_EPS)                       zoneCol[k] = SEC_GREEN;
            else if (delta < 1.0f)                           zoneCol[k] = SEC_YELLOW;
            else                                             zoneCol[k] = SEC_RED;
        }
    }

    // ── Draw the track, painting each segment by its mini-sector color ─────────
    if (g_smooth_track_points.empty()) {
        const char* msg = "No track";
        ImVec2 tSz = ImGui::CalcTextSize(msg);
        dl->AddText(nullptr, 0.f,
                    {base.x + (mapW - tSz.x)*0.5f, base.y + (mapH - tSz.y)*0.5f},
                    IM_COL32(60,60,60,255), msg);
    } else {
        const size_t n = g_smooth_track_points.size();

        // Bounds
        glm::vec2 lo = g_smooth_track_points[0].position;
        glm::vec2 hi = lo;
        for (auto& sp : g_smooth_track_points) {
            lo.x = lo.x < sp.position.x ? lo.x : sp.position.x;
            lo.y = lo.y < sp.position.y ? lo.y : sp.position.y;
            hi.x = hi.x > sp.position.x ? hi.x : sp.position.x;
            hi.y = hi.y > sp.position.y ? hi.y : sp.position.y;
        }
        float rX = hi.x - lo.x; if (rX < 1e-6f) rX = 1.f;
        float rY = hi.y - lo.y; if (rY < 1e-6f) rY = 1.f;
        float pad   = 20.f;
        float scale = fminf((mapW - pad*2) / rX, (mapH - pad*2) / rY);
        float offX  = base.x + (mapW - rX*scale) * 0.5f;
        float offY  = base.y + (mapH - rY*scale) * 0.5f;

        auto toScreen = [&](glm::vec2 p) -> ImVec2 {
            return {offX + (p.x - lo.x)*scale, offY + (rY - (p.y - lo.y))*scale};
        };

        // Cumulative arc length per point → progress (matches m_track_progress).
        std::vector<float> cum(n, 0.f);
        for (size_t i = 1; i < n; ++i) {
            glm::vec2 d = g_smooth_track_points[i].position - g_smooth_track_points[i-1].position;
            cum[i] = cum[i-1] + sqrtf(d.x*d.x + d.y*d.y);
        }
        glm::vec2 dc = g_smooth_track_points[0].position - g_smooth_track_points[n-1].position;
        float total = cum[n-1] + sqrtf(dc.x*dc.x + dc.y*dc.y);
        if (total < 1e-6f) total = 1.f;

        auto colAtProg = [&](float p) -> ImU32 {
            int z = (int)(p * SEC_ZONES);
            if (z < 0) z = 0; if (z >= SEC_ZONES) z = SEC_ZONES - 1;
            return zoneCol[z];
        };

        const float trackTh = fmaxf(mapH * 0.022f, 4.f);
        for (size_t i = 0; i < n; ++i) {
            size_t j  = (i + 1) % n;
            ImVec2 pa = toScreen(g_smooth_track_points[i].position);
            ImVec2 pb = toScreen(g_smooth_track_points[j].position);
            dl->AddLine(pa, pb, colAtProg(cum[i] / total), trackTh);
        }

        // Vehicle position dot — apply the same centering offset that is baked
        // into g_smooth_track_points (see rebuildTrackCacheFromEdges).
        double vx = 0, vy = 0; bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_vehicles_mutex);
            auto it = g_vehicles.find(vehicleId);
            if (it != g_vehicles.end()) {
                const glm::vec2 rOff = it->second.m_apply_track_render_offset
                                         ? getTrackRenderOffset()
                                         : glm::vec2(0.0f, 0.0f);
                vx = it->second.m_normalized_x + rOff.x;
                vy = it->second.m_normalized_y + rOff.y;
                found = true;
            }
        }
        if (found) {
            ImVec2 dot = toScreen({(float)vx, (float)vy});
            float  dr  = fmaxf(mapH * 0.025f, 4.f);
            dl->AddCircleFilled(dot, dr,     IM_COL32(240, 240, 240, 255));
            dl->AddCircle      (dot, dr + 2, IM_COL32(60, 60, 60, 200), 16, 1.f);
        }
    }

    // ── Color legend ──────────────────────────────────────────────────────────
    {
        struct { ImU32 c; const char* t; } key[] = {
            { SEC_PURPLE, "OVERALL" },
            { SEC_GREEN,  "BEST"    },
            { SEC_YELLOW, "<1s"     },
            { SEC_RED,    ">1s"     },
        };
        float sw    = 11.f * z;
        float fSz   = (ctx.russo ? ctx.russo->FontSize : 11.f) * 0.85f * z;
        float legY  = base.y + mapH + (labelH - sw) * 0.5f;
        float x     = base.x + pad_px() * z;
        float colW  = (mapW - pad_px() * 2.f * z) / 4.f;
        for (auto& k : key) {
            dl->AddRectFilled({x, legY}, {x + sw, legY + sw}, k.c, 2.f);
            dl->AddText(ctx.russo, fSz, {x + sw + 4.f * z, legY + (sw - fSz) * 0.5f}, COL_DIM, k.t);
            x += colW;
        }
    }

    ImGui::SetCursorScreenPos({base.x, base.y + mapH + labelH});
    ImGui::Dummy(ImVec2(mapW, 0.f));

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}

} // namespace Pro
