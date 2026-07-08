#include "ProEvents.h"
#include "../../racing/RaceManager.h"
#include "../../vehicle/Vehicle.h"
#include "../../network/TrackServerClient.h"
#include <imgui.h>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <map>
#include <climits>
#include <cstdio>

extern RaceManager* g_race_manager;
extern std::map<int32_t, Vehicle> g_vehicles;
extern std::mutex g_vehicles_mutex;

namespace Pro {

// ── Event colors ─────────────────────────────────────────────────────────────
static constexpr ImU32 EV_OVERALL = IM_COL32(0xBB,0x8E,0xF9,255); // purple — session best
static constexpr ImU32 EV_PBLAP   = IM_COL32(0x00,0xD2,0x6E,255); // green  — personal best lap
static constexpr ImU32 EV_PBSEC   = IM_COL32(0x00,0xBC,0xFF,255); // cyan   — personal best sector
static constexpr ImU32 EV_LEAD    = IM_COL32(0xDA,0xA5,0x40,255); // gold   — leader change
static constexpr ImU32 EV_INFO    = IM_COL32(0xC8,0xC8,0xC8,255); // gray   — race info
static constexpr ImU32 EV_STOP    = IM_COL32(0xFF,0x4B,0x4B,255); // red    — stop

static void fmtSec(float s, char* b, size_t n) {
    if (s <= 0.f) { snprintf(b, n, "--.---"); return; }
    int m = (int)(s / 60.f); float r = s - m * 60.f;
    if (m > 0) snprintf(b, n, "%d:%06.3f", m, r);
    else       snprintf(b, n, "%.3f", r);
}

// Robust per-sector times for a lap (running-max progress, lap time closes S3).
static void crossTimes(const std::vector<LapInfo>& s, float bt[4]) {
    for (int i = 0; i < 4; ++i) bt[i] = -1.f;
    if (s.size() < 2) return;
    double prevMaxP = s.front().progress;
    float  prevT    = s.front().timefromstart;
    int    nb       = 0;
    while (nb <= 3 && (double)nb / 3.0 <= prevMaxP) bt[nb++] = prevT;
    for (size_t i = 1; i < s.size(); ++i) {
        double p = s[i].progress; if (p < prevMaxP) p = prevMaxP;
        float  t = s[i].timefromstart;
        while (nb <= 3 && (double)nb / 3.0 <= p) {
            double bp = (double)nb / 3.0, den = p - prevMaxP;
            double f = den > 1e-9 ? (bp - prevMaxP) / den : 1.0;
            bt[nb++] = prevT + (t - prevT) * (float)f;
        }
        prevMaxP = p; prevT = t;
    }
}
static void secCompleted(const std::vector<LapInfo>& s, float lapTime, float out[3], bool v[3]) {
    float bt[4]; crossTimes(s, bt);
    if (bt[3] < 0.f && lapTime > 0.f && bt[2] >= 0.f) bt[3] = lapTime;
    for (int k = 0; k < 3; ++k) {
        if (bt[k] >= 0.f && bt[k + 1] >= 0.f) { out[k] = bt[k + 1] - bt[k]; v[k] = true; }
        else { out[k] = 0.f; v[k] = false; }
    }
}

// ── Event log + detection state ─────────────────────────────────────────────
struct LogEvent { char time[12]; std::string text; ImU32 col; };
static std::deque<LogEvent> s_log;

struct EvtState { float bestLap = -1.f; float bestSec[3] = { -1.f, -1.f, -1.f }; };
static std::map<int32_t, EvtState> s_prev;
static float        s_sessBestLap    = -1.f;
static float        s_sessBestSec[3] = { -1.f, -1.f, -1.f };
static int32_t      s_leader         = INT_MIN;
static SessionState s_state          = SessionState::Idle;
static bool         s_init           = false;

static void pushEvent(float sessT, std::string text, ImU32 col) {
    if (sessT < 0.f) sessT = 0.f;
    LogEvent e;
    int m = (int)(sessT / 60.f), s = (int)sessT % 60;
    snprintf(e.time, sizeof(e.time), "%02d:%02d", m, s);
    e.text = std::move(text);
    e.col  = col;
    s_log.push_front(std::move(e));
    while (s_log.size() > 120) s_log.pop_back();
}

static void resetTracking() {
    s_prev.clear();
    s_sessBestLap = -1.f;
    for (int k = 0; k < 3; ++k) s_sessBestSec[k] = -1.f;
    s_leader = INT_MIN;
    s_log.clear();
}

// Poll vehicle/race state once per frame and append any new events.
static void detectEvents() {
    if (!g_race_manager) return;
    float        sessT = g_race_manager->GetRaceElapsedTime();
    SessionState st    = g_race_manager->GetSessionState();

    // Race flag changes (Track Server) — race-control events in the log.
    // The first observed flag is adopted silently (connecting is not a change).
    {
        static std::string s_flagPrev;
        const std::string f = TrackServerClient::isConnected()
                                ? TrackServerClient::currentFlag() : std::string();
        if (!f.empty() && f != s_flagPrev) {
            if (!s_flagPrev.empty()) {
                ImU32 col = EV_INFO;
                std::string label = "FLAG: ";
                if      (f == "green")  { col = IM_COL32(0x00,0xD2,0x6E,255); label += "Green"; }
                else if (f == "yellow") { col = IM_COL32(0xF5,0xD9,0x0A,255); label += "Yellow"; }
                else if (f == "red")    { col = EV_STOP;                      label += "Red"; }
                else if (f == "finish") { col = EV_LEAD;                      label += "Finish (checkered)"; }
                else                    { label += f; }
                pushEvent(sessT, label, col);
            }
            s_flagPrev = f;
        }
    }

    struct Snap { int32_t id; std::string name; float bestLap; float sec[3]; bool secV[3]; double prog; bool started; };
    std::vector<Snap> snaps;
    int32_t leader = INT_MIN; double leadProg = -1.0;
    {
        std::lock_guard<std::mutex> lk(g_vehicles_mutex);
        auto lapTimeOf = [](Vehicle& v, int ln) -> float {
            auto m = v.m_laps.find(ln); return (m != v.m_laps.end()) ? m->second.lapTime : -1.f;
        };
        for (auto& [id, v] : g_vehicles) {
            Snap s;
            s.id = id;
            s.name = (v.name.empty() || v.name == "Unknown") ? ("CAR " + std::to_string(v.m_id)) : v.name;
            s.bestLap = v.m_best_lap_time;
            for (int k = 0; k < 3; ++k) { s.sec[k] = -1.f; s.secV[k] = false; }
            for (auto& [ln, sess] : v.laps) {
                float z[3]; bool zv[3]; secCompleted(sess.samples, lapTimeOf(v, ln), z, zv);
                for (int k = 0; k < 3; ++k)
                    if (zv[k] && (!s.secV[k] || z[k] < s.sec[k])) { s.sec[k] = z[k]; s.secV[k] = true; }
            }
            s.prog = v.m_total_progress;
            s.started = v.m_has_started_first_lap;
            if (s.started && s.prog > leadProg) { leadProg = s.prog; leader = id; }
            snaps.push_back(std::move(s));
        }
    }

    // First frame (or after a reset): seed bests silently, do not flood the log.
    if (!s_init) {
        for (auto& s : snaps) {
            EvtState e; e.bestLap = s.bestLap;
            for (int k = 0; k < 3; ++k) {
                e.bestSec[k] = s.secV[k] ? s.sec[k] : -1.f;
                if (s.secV[k] && (s_sessBestSec[k] < 0.f || s.sec[k] < s_sessBestSec[k])) s_sessBestSec[k] = s.sec[k];
            }
            s_prev[s.id] = e;
            if (s.bestLap > 0.f && (s_sessBestLap < 0.f || s.bestLap < s_sessBestLap)) s_sessBestLap = s.bestLap;
        }
        s_leader = leader; s_state = st; s_init = true;
        return;
    }

    // Session state transitions
    if (st != s_state) {
        if (st == SessionState::Idle)            resetTracking();
        else if (st == SessionState::Active)     pushEvent(sessT, "RACE: Session started", EV_INFO);
        else if (st == SessionState::Finishing)  pushEvent(sessT, "RACE: Session stopped", EV_STOP);
        else if (st == SessionState::Ended)      pushEvent(sessT, "RACE: Session ended", EV_INFO);
        s_state = st;
    }

    const float eps = 0.001f;
    for (auto& s : snaps) {
        EvtState& pv = s_prev[s.id];

        // Lap data cleared for this car (reset) → drop its tracked bests.
        if (s.bestLap < 0.f && pv.bestLap > 0.f) { pv = EvtState(); }

        if (s.bestLap > 0.f && (pv.bestLap < 0.f || s.bestLap < pv.bestLap - eps)) {
            char tb[16]; fmtSec(s.bestLap, tb, sizeof(tb));
            bool overall = (s_sessBestLap < 0.f || s.bestLap < s_sessBestLap - eps);
            if (overall) { s_sessBestLap = s.bestLap; pushEvent(sessT, "Fastest lap — " + s.name + "  " + tb, EV_OVERALL); }
            else         pushEvent(sessT, "Personal best lap — " + s.name + "  " + tb, EV_PBLAP);
            pv.bestLap = s.bestLap;
        }

        for (int k = 0; k < 3; ++k) {
            if (s.secV[k] && (pv.bestSec[k] < 0.f || s.sec[k] < pv.bestSec[k] - eps)) {
                char tb[16]; fmtSec(s.sec[k], tb, sizeof(tb));
                bool overall = (s_sessBestSec[k] < 0.f || s.sec[k] < s_sessBestSec[k] - eps);
                char head[24];
                if (overall) { s_sessBestSec[k] = s.sec[k]; snprintf(head, sizeof(head), "Fastest S%d — ", k + 1);
                               pushEvent(sessT, std::string(head) + s.name + "  " + tb, EV_OVERALL); }
                else { snprintf(head, sizeof(head), "Best S%d — ", k + 1);
                       pushEvent(sessT, std::string(head) + s.name + "  " + tb, EV_PBSEC); }
                pv.bestSec[k] = s.sec[k];
            }
        }
    }

    if (leader != INT_MIN && leader != s_leader) {
        std::string nm;
        for (auto& s : snaps) if (s.id == leader) nm = s.name;
        if (!nm.empty()) pushEvent(sessT, nm + " takes the lead", EV_LEAD);
        s_leader = leader;
    }
}

void RenderEventsWindow(const ProContext& ctx, ImVec2 vpSz, float topH) {
    detectEvents();

    ImGui::SetNextWindowPos ({210.f, topH + 600.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({200.f, 225.f},         ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({120.f, 80.f}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##Events", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    float z = PanelZoom("Events");
    DrawPanelHeader(ctx, "EVENTS", false, nullptr, z);

    float scrollH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##evScroll", {w, scrollH}, false);

    float fSz  = (ctx.russo   ? ctx.russo->FontSize   : ImGui::GetFontSize()) * z;
    float fReg = (ctx.regular ? ctx.regular->FontSize : ImGui::GetFontSize()) * z;
    float pad  = PAD * z;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (s_log.empty()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ctx.regular, fReg, {p.x + pad, p.y + 4.f}, COL_DIM, "No events yet");
    }

    int i = 0;
    for (const LogEvent& ev : s_log) {       // newest first
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ctx.russo,   fSz,  {p.x + pad, p.y + 1.f},        IM_COL32(0x51,0x51,0x51,255), ev.time);
        dl->AddText(ctx.regular, fReg, {p.x + pad, p.y + fSz + 2.f},  ev.col, ev.text.c_str());

        ImGui::Dummy(ImVec2(w, fSz + fReg + 6.f * z));
        ImVec2 sp = ImGui::GetCursorScreenPos();
        dl->AddLine({sp.x, sp.y}, {sp.x + w, sp.y}, COL_SEP, 1.f);
        ImGui::Dummy(ImVec2(w, 1.f));
        ++i;
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Pro
