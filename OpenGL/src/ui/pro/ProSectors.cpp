#include "ui/pro/ProSectors.h"
#include "core/WorldSnapshot.h"
#include "network/ReplayPlayer.h"
#include "rendering/Interpolation.h"
#include "vehicle/Vehicle.h"
#include <imgui.h>
#include <array>
#include <mutex>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

extern std::vector<SplinePoint> g_smooth_track_points;
extern std::map<int32_t, Vehicle> g_vehicles;

namespace Pro {

// ── Mini-sector delta palette ───────────────────────────────────────────────
// Track is split into SEC_ZONES mini-sectors; each is colored by how the driver
// performed in it versus their personal best lap (and the overall session best).
//
// Число зон КРАТНО SECTOR_COUNT намеренно: зоны ложатся ровно по SEC_PER_SECTOR
// штук в каждый зачётный сектор, и время каждой группы приводится к измеренному
// времени своего сектора (см. zoneTimes). Некратное число размазало бы зону
// через границу сектора, и приводить было бы не к чему.
static constexpr int   SEC_ZONES  = 48;
static_assert(SEC_ZONES % SECTOR_COUNT == 0,
              "mini-sector zones must divide evenly into timing sectors");
static constexpr int   SEC_PER_SECTOR = SEC_ZONES / SECTOR_COUNT;
static constexpr ImU32 SEC_PURPLE = IM_COL32(177, 156, 224, 255); // fastest of all
static constexpr ImU32 SEC_GREEN  = IM_COL32( 62, 142,  71, 255); // beats own best
static constexpr ImU32 SEC_YELLOW = IM_COL32(218, 165,  64, 255); // < 1s off best
static constexpr ImU32 SEC_RED    = IM_COL32(193,  60,  53, 255); // > 1s off best
static constexpr ImU32 SEC_NONE   = IM_COL32( 70,  70,  70, 255); // no comparison data

static constexpr float SEC_EPS    = 0.005f; // tie tolerance (s)

// ФОРМА круга по зонам: сколько времени машина провела в каждой мини-зоне,
// как это видно по логу замеров.
//   out[k]   = time spent in mini-sector k (seconds)
//   valid[k] = true only when BOTH boundaries of zone k were actually crossed
//
// История круга упорядочена ПО ПРОГРЕССУ: хронометраж вставляет замер по месту
// (см. RaceManager, upper_bound по progress), и на этом инварианте стоят и
// двоичный поиск в visibleSampleCount, и расчёт здесь. Running-max ниже —
// страховка на случай, если инвариант когда-нибудь ослабят: время границы зоны
// фиксируется в момент, когда она достигнута ВПЕРВЫЕ, и назад уже не уезжает.
// Считается по ПЕРВЫМ `n` сэмплам круга, а не по всему вектору: на повторе в
// истории лежит и то, что на текущей точке ещё не произошло (см.
// visibleSampleCount в Vehicle.h).
//
// ЭТО ТОЛЬКО ФОРМА, НЕ ВЕЛИЧИНА. Плотность лога зависит от частоты кадров, а
// границы зон между замерами приходится интерполировать — поэтому абсолютные
// числа отсюда брать нельзя (см. LapTypes.h, раздел про точки хронометража).
// Масштаб зонам задаёт zoneTimes ниже, приводя каждую группу к ИЗМЕРЕННОМУ
// времени своего сектора.
static void zoneShape(const LapInfo* s, size_t n,
                      float out[SEC_ZONES], bool valid[SEC_ZONES]) {
    for (int k = 0; k < SEC_ZONES; ++k) { out[k] = 0.f; valid[k] = false; }
    if (n < 2) return;

    float bt[SEC_ZONES + 1];
    for (int i = 0; i <= SEC_ZONES; ++i) bt[i] = -1.f;

    double prevMaxP = s[0].progress;
    float  prevT    = s[0].timefromstart;
    int    nb       = 0;
    // Boundaries already behind the first sample are reached at lap start.
    while (nb <= SEC_ZONES && (double)nb / SEC_ZONES <= prevMaxP) bt[nb++] = prevT;

    for (size_t i = 1; i < n; ++i) {
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

// Времена мини-зон одного круга, ПРИВЯЗАННЫЕ К ЗАЧЁТНЫМ СЕКТОРАМ.
//
// Зачем привязка. Время сектора в этом проекте — разность меток пересечений из
// пакетов (LapTypes.h): оно не зависит ни от частоты кадров, ни от скорости
// повтора, ни от того, каким путём оператор пришёл в эту точку записи. Разбор
// лога замеров таких свойств не даёт и объявлен там же негодным способом
// считать секторы — но лог единственный, кто знает, КАК время распределено
// внутри сектора, а мини-зоны без этого не нарисовать.
//
// Поэтому обязанности разделены: лог даёт форму, метки пересечений — величину.
// Группа из SEC_PER_SECTOR зон масштабируется так, чтобы её сумма в точности
// равнялась измеренному времени своего сектора. Следствия:
//   * сумма всех зон равна кругу ПО ПОСТРОЕНИЮ — как и сумма секторов;
//   * систематическая ошибка плотности лога уходит: она общий множитель, и
//     масштабирование её снимает;
//   * законченный сектор неизменяем, поэтому его зоны не меняются от того,
//     сколько раз оператор проехал по этому месту записи.
//
// Сектор без измеренного времени (машина заехала в середине круга, сектор ещё
// не закончен) остаётся на сырой форме: другого источника для него нет, а
// показать текущий сектор всё равно надо — на нём и стоит машина.
static void zoneTimes(const LapInfo* samples, size_t n,
                      const std::array<float, SECTOR_COUNT>& sectors,
                      float out[SEC_ZONES], bool valid[SEC_ZONES]) {
    zoneShape(samples, n, out, valid);

    for (int sec = 0; sec < SECTOR_COUNT; ++sec) {
        const float measured = sectors[sec];
        if (measured <= 0.f) continue;          // SECTOR_TIME_NONE или не измерен

        const int first = sec * SEC_PER_SECTOR;
        const int last  = first + SEC_PER_SECTOR;

        float shape = 0.f;
        bool  whole = true;
        for (int k = first; k < last; ++k) {
            if (!valid[k]) { whole = false; break; }
            shape += out[k];
        }
        // Приводим только ЦЕЛУЮ группу: по части зон судить о распределении
        // внутри сектора нельзя, и масштаб вышел бы завышенным.
        if (!whole || shape <= 1e-6f) continue;

        const float scale = measured / shape;
        for (int k = first; k < last; ++k) out[k] *= scale;
    }
}

/// Времена зон для круга `lapNumber` машины `id`/`v`. Пусто (все valid=false),
/// если такого круга нет. Зовётся под мьютексом машин.
///
/// НА ПОВТОРЕ ЗАМЕРЫ БЕРЁМ ИЗ ЖУРНАЛА ЗАПИСИ, как это делают GRAPHS, LAP LIST
/// и TRACK REPORT. История самой машины для разбора не годится по двум
/// причинам сразу:
///   * она дописывается по кадрам, поэтому её плотность зависит от частоты
///     кадров и от скорости, с которой оператор проехал этот участок;
///   * промотанный вперёд круг остаётся в ней НЕПОЛНЫМ, и граница зоны в нём
///     не находится вовсе — зона молча остаётся серой.
/// Журнал снят одним ровным прогоном при открытии файла и не меняется, поэтому
/// одна и та же точка записи всегда даёт одну и ту же картинку. Живой заезд
/// читает историю машины: другого источника там не существует.
static void lapZoneTimes(int32_t id, const Vehicle& v, int lapNumber,
                         float out[SEC_ZONES], bool valid[SEC_ZONES]) {
    for (int k = 0; k < SEC_ZONES; ++k) { out[k] = 0.f; valid[k] = false; }

    const std::shared_ptr<const telemetry::VehicleJournal> journal =
        telemetry::replay_journal(id);

    const std::vector<LapInfo>* samples = nullptr;
    if (journal != nullptr) {
        const auto lap = journal->lap_samples.find(lapNumber);
        if (lap != journal->lap_samples.end()) samples = &lap->second;
    } else {
        const auto lap = v.laps.find(lapNumber);
        if (lap != v.laps.end()) samples = &lap->second.samples;
    }
    if (samples == nullptr || samples->empty()) return;

    // Законченный круг знает свои секторы сам; у текущего они копятся в машине.
    // Разные места хранения — одна и та же величина: разность меток пересечений.
    std::array<float, SECTOR_COUNT> sectors;
    sectors.fill(SECTOR_TIME_NONE);
    if (lapNumber == v.m_current_lap_number) {
        sectors = v.m_current_lap_sectors;
    } else if (journal != nullptr) {
        const auto done = journal->lap_times.find(lapNumber);
        if (done != journal->lap_times.end()) sectors = done->second.sectors;
    } else {
        const auto done = v.m_laps.find(lapNumber);
        if (done != v.m_laps.end()) sectors = done->second.sectors;
    }

    // Отрезаем непроигранную часть ровно так же, как раньше: круг, на котором
    // стоит запись, раскрывается по мере проезда — в этом и смысл живой карты.
    zoneTimes(samples->data(), visibleSampleCount(v, lapNumber, *samples),
              sectors, out, valid);
}

// ── Кэш зон сессионного рекорда ─────────────────────────────────────────────
//
// Ответ меняется, только когда какая-то машина поставила новый лучший круг или
// её лучший круг дописался новыми замерами. Всё это видно по дешёвому ключу,
// который и считается каждый кадр вместо самого разбора.
struct SessionBestZones {
    float time[SEC_ZONES];
    bool  valid[SEC_ZONES];
};

/// Зовётся ПОД МЬЮТЕКСОМ МАШИН. Ссылка живёт до следующего вызова.
static const SessionBestZones& sessionBestZones() {
    static SessionBestZones s_zones{};
    static uint64_t         s_key = 0;
    static bool             s_seeded = false;

    uint64_t key = 1469598103934665603ull;
    const auto mix = [&key](uint64_t value) {
        key = (key ^ value) * 1099511628211ull;
    };
    for (const auto& [id, v] : g_vehicles) {
        mix(static_cast<uint64_t>(static_cast<uint32_t>(id)));
        mix(static_cast<uint64_t>(static_cast<uint32_t>(v.bestlapID)));
        // Время лучшего круга кладём в ключ побитово: сравнение float здесь не
        // нужно, нужен признак «то же самое число или уже другое».
        uint32_t bits = 0; std::memcpy(&bits, &v.m_best_lap_time, sizeof(bits));
        mix(bits);
        const auto best = v.laps.find(v.bestlapID);
        mix(best == v.laps.end() ? 0ull : best->second.samples.size());
        // Открылась другая запись — журнал другой, а значит и ответ другой,
        // даже если номера машин и кругов случайно совпали.
        mix(telemetry::replay_session_revision());
        // Лучшим может оказаться и круг, по которому машина едет прямо сейчас:
        // тогда видимая часть круга растёт вместе с её прогрессом, и ответ
        // меняется каждый кадр. Только в этом случае прогресс и попадает в
        // ключ — иначе кэш сбрасывался бы всегда и не значил бы ничего.
        mix(static_cast<uint64_t>(static_cast<uint32_t>(v.m_current_lap_number)));
        if (v.bestlapID == v.m_current_lap_number) {
            uint64_t progress = 0;
            std::memcpy(&progress, &v.m_track_progress, sizeof(progress));
            mix(progress);
        }
    }

    if (s_seeded && key == s_key)
        return s_zones;

    s_key    = key;
    s_seeded = true;
    for (int k = 0; k < SEC_ZONES; ++k) { s_zones.time[k] = 0.f; s_zones.valid[k] = false; }

    for (const auto& [id, v] : g_vehicles) {
        if (v.bestlapID < 0) continue;
        float z[SEC_ZONES]; bool zv[SEC_ZONES];
        lapZoneTimes(id, v, v.bestlapID, z, zv);
        for (int k = 0; k < SEC_ZONES; ++k) {
            if (!zv[k]) continue;
            if (!s_zones.valid[k] || z[k] < s_zones.time[k]) {
                s_zones.time[k]  = z[k];
                s_zones.valid[k] = true;
            }
        }
    }
    return s_zones;
}

void RenderSectorsWindow(const ProContext& ctx, int32_t vehicleId,
                          ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({635.f * ui, topH + 600.f * ui}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({550.f * ui, 225.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({140.f * ui, 100.f * ui}, {vpSz.x, vpSz.y});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(COL_BG_WIDGET));
    if (!ImGui::Begin("##Sectors", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End(); ImGui::PopStyleColor(); return;
    }

    float w = ImGui::GetWindowWidth();
    float h = ImGui::GetWindowHeight();
    float z = PanelZoom("Sectors");
    DrawPanelHeader(ctx, "SECTORS", false, "Sectors");

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      base = ImGui::GetCursorScreenPos();

    // Reserved for the color legend at the bottom
    const float labelH = 18.f * z;
    float mapW = w;
    float mapH = h - header_h() - 2.f - labelH;

    dl->AddRectFilled(base, {base.x + mapW, base.y + mapH}, COL_BG);

    // Какой круг разбираем, спрашиваем ДО захвата мьютекса машин: AnalysisLap
    // ходит в RaceManager, а тот берёт тот же мьютекс.
    //
    // Именно AnalysisLap, а не m_current_lap_number. Круг выбирает ОПЕРАТОР
    // (щелчок в LAP LIST), и GRAPHS с LAP LIST уже идут за его выбором. Пока
    // эта панель шла за точкой просмотра, стоило подвинуть ползунок внутри
    // другого круга — и на экране оказывались два разных круга одновременно,
    // без единого признака, что это разные круги.
    const int analysisLap = AnalysisLap(vehicleId);

    // ── Compute per-zone delta colors under the vehicles lock ──────────────────
    ImU32 zoneCol[SEC_ZONES];
    for (int k = 0; k < SEC_ZONES; ++k) zoneCol[k] = SEC_NONE;
    {
        float bestZ[SEC_ZONES]; bool bestV[SEC_ZONES] = { false };
        float dispZ[SEC_ZONES]; bool zValid[SEC_ZONES] = { false };

        VehiclesLock lk;

        const auto it = g_vehicles.find(vehicleId);
        if (it != g_vehicles.end()) {
            const Vehicle& v = it->second;

            // Personal best lap → reference times (full lap → all zones valid)
            if (v.bestlapID >= 0)
                lapZoneTimes(vehicleId, v, v.bestlapID, bestZ, bestV);

            // F1-style live map: color the analysed lap zone by zone. A zone is
            // colored once both of its boundaries are crossed; unreached zones
            // stay neutral. So a slow lap shows green where pace matched and
            // yellow/red only in the mini-sectors where time was actually lost.
            lapZoneTimes(vehicleId, v, analysisLap, dispZ, zValid);
        }

        // Overall session best per zone (across every car's best lap).
        //
        // ПОД КЭШЕМ. Проход по всем машинам с разбором их лучших кругов стоит
        // тем дороже, чем больше машин и чем длиннее круг, а меняется он только
        // когда кто-то поставил новый рекорд. Считать его на каждом кадре
        // отрисовки — платить полную цену за неизменный ответ.
        const SessionBestZones& sess = sessionBestZones();

        for (int k = 0; k < SEC_ZONES; ++k) {
            if (!zValid[k] || !bestV[k]) continue;          // no data / no reference
            float delta = dispZ[k] - bestZ[k];
            if (sess.valid[k] && dispZ[k] <= sess.time[k] + SEC_EPS) zoneCol[k] = SEC_PURPLE;
            else if (delta <= SEC_EPS)                               zoneCol[k] = SEC_GREEN;
            else if (delta < 1.0f)                                   zoneCol[k] = SEC_YELLOW;
            else                                                     zoneCol[k] = SEC_RED;
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
        // Позиция — из снимка: точка на карте обязана стоять там же, где машина
        // на главном экране, в том числе посреди перемотки повтора.
        double vx = 0, vy = 0; bool found = false;
        {
            const std::shared_ptr<const world::Snapshot> snapshot = world::current();
            if (const world::VehicleView* v = world::find(*snapshot, vehicleId)) {
                const glm::vec2 rOff = v->apply_track_render_offset
                                         ? getTrackRenderOffset()
                                         : glm::vec2(0.0f, 0.0f);
                vx = v->x + rOff.x;
                vy = v->y + rOff.y;
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
