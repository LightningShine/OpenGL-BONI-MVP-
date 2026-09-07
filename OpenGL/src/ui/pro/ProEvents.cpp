#include "ProEvents.h"
#include "../../core/WorldSnapshot.h"
#include "../../racing/RaceManager.h"
#include "../../vehicle/Vehicle.h"
#include "../../network/ReplayPlayer.h"
#include "../../network/TrackServerClient.h"
#include <imgui.h>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <map>
#include <climits>
#include <cstdio>
#include <algorithm>
#include <array>
#include <memory>

extern RaceManager* g_race_manager;

namespace Pro {

// ── Event colors ─────────────────────────────────────────────────────────────
static constexpr ImU32 EV_OVERALL = IM_COL32(0xBB,0x8E,0xF9,255); // purple - session best
static constexpr ImU32 EV_PBLAP   = IM_COL32(0x00,0xD2,0x6E,255); // green  - personal best lap
static constexpr ImU32 EV_PBSEC   = IM_COL32(0x00,0xBC,0xFF,255); // cyan   - personal best sector
static constexpr ImU32 EV_LEAD    = IM_COL32(0xDA,0xA5,0x40,255); // gold   - leader change
static constexpr ImU32 EV_INFO    = IM_COL32(0xC8,0xC8,0xC8,255); // gray   - race info
static constexpr ImU32 EV_STOP    = IM_COL32(0xFF,0x4B,0x4B,255); // red    - stop

static void fmtSec(float s, char* b, size_t n) {
    if (s <= 0.f) { snprintf(b, n, "--.---"); return; }
    int m = (int)(s / 60.f); float r = s - m * 60.f;
    if (m > 0) snprintf(b, n, "%d:%06.3f", m, r);
    else       snprintf(b, n, "%.3f", r);
}

// ── Event log + detection state ─────────────────────────────────────────────
struct LogEvent { char time[12]; float t; std::string text; ImU32 col; };
static std::deque<LogEvent> s_log;

// Потолок журнала. Живой заезд идёт часами, и без потолка очередь росла бы
// бесконечно; на повторе журнал строится из записи целиком, и потолок обязан
// быть заведомо больше числа событий полноценной гонки — иначе разбор терял бы
// как раз начало заезда.
static constexpr size_t LOG_CAPACITY = 1000;

// ЧИТАЕМ ЗАПИСЬ, А НЕ НАБЛЮДАЕМ ЗА НЕЙ.
//
// Живой заезд можно только наблюдать: будущего в нём не существует. Запись —
// наоборот, известна целиком с момента открытия (см. VehicleJournal), и события
// в ней надо ЧИТАТЬ. Пока журнал строился наблюдением, он зависел от того, как
// оператор ходил по записи: промотал круг вперёд — событий этого круга не будет
// никогда, потому что их никто не застал; вернулся назад — детектор
// пересеивался молча. Разбор по такому журналу невоспроизводим.
//
// Поэтому на повторе журнал строится один раз из записи, весь, а точка
// просмотра только ОТРЕЗАЕТ будущее при показе. Перемотка взад-вперёд по одному
// месту даёт один и тот же список — как и положено.
static bool s_journal_mode  = false;   // журнал построен из записи
static bool s_journal_built = false;

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
    e.t = sessT;
    int m = (int)(sessT / 60.f), s = (int)sessT % 60;
    snprintf(e.time, sizeof(e.time), "%02d:%02d", m, s);
    e.text = std::move(text);
    e.col  = col;
    s_log.push_front(std::move(e));
    while (s_log.size() > LOG_CAPACITY) s_log.pop_back();
}

// Сбрасывает только НАБЛЮДАТЕЛЯ - рекорды, относительно которых решается, что
// событие произошло. Журнал не трогает: это две разные вещи, и путать их
// нельзя. Наблюдатель обязан соответствовать текущей точке заезда, а журнал —
// это то, что на этой точке уже успело произойти.
static void resetDetector() {
    s_prev.clear();
    s_sessBestLap = -1.f;
    for (int k = 0; k < 3; ++k) s_sessBestSec[k] = -1.f;
    s_leader = INT_MIN;
}

static void resetTracking() {
    resetDetector();
    s_log.clear();
}


/// Сколько секунд от начала записи прошло к моменту с меткой `utc`.
/// Разворачивает переход через полночь: метка источника обнуляется в 00:00.
static float elapsedSinceStart(uint32_t base, uint32_t utc) {
    const uint32_t delta = (utc >= base) ? (utc - base)
                                         : (86'400'000u - base + utc);
    return static_cast<float>(delta) / 1000.f;
}

/// Строит журнал событий ЦЕЛИКОМ из записи. Зовётся один раз на открытую
/// запись, сразу как только прогрев отдал журнал.
static void buildFromJournal(const std::vector<int32_t>& ids) {
    resetTracking();

    const uint32_t base = telemetry::replay_status().first_utc_ms;

    // Имя берём ИЗ ЖУРНАЛА, а не из снимка мира. В снимке его на этом кадре
    // нет: прогрев заканчивается сбросом пайплайна, машины появятся заново
    // только по мере проигрывания, и все события звались бы "CAR 3".
    const auto nameOf = [](const telemetry::VehicleJournal& j, int32_t id) -> std::string {
        if (!j.name.empty() && j.name != "Unknown") return j.name;
        return "CAR " + std::to_string(id);
    };

    // Один законченный круг одной машины: всё, о чём вообще может быть событие.
    struct Entry {
        float                            t;          // с от начала записи
        int32_t                          car;
        std::string                      name;
        int                              lap;        // номер круга
        float                            lapTime;
        std::array<float, SECTOR_COUNT>  sec;
    };
    std::vector<Entry> entries;

    for (int32_t id : ids) {
        const std::shared_ptr<const telemetry::VehicleJournal> journal =
            telemetry::replay_journal(id);
        if (journal == nullptr) continue;

        const std::string name = nameOf(*journal, id);

        for (const auto& [lapNumber, lap] : journal->lap_times) {
            Entry e;
            e.car     = id;
            e.name    = name;
            e.lap     = lapNumber;
            e.lapTime = lap.lapTime;
            e.sec     = lap.sectors;

            // Когда круг закончился — по САМОЙ ПОЗДНЕЙ метке его замеров.
            // История упорядочена по прогрессу, а не по времени, поэтому берём
            // максимум, а не последний элемент.
            uint32_t end_utc = 0;
            const auto samples = journal->lap_samples.find(lapNumber);
            if (samples != journal->lap_samples.end())
                for (const LapInfo& sample : samples->second)
                    if (sample.utc_ms != 0 && sample.utc_ms > end_utc) end_utc = sample.utc_ms;

            // Замеров у круга нет (запись без телеметрии этого участка) — на
            // ось времени его поставить нечем, но и терять его незачем: пусть
            // стоит в самом начале, событие о нём всё равно осмысленно.
            e.t = (base != 0 && end_utc != 0) ? elapsedSinceStart(base, end_utc) : 0.f;
            entries.push_back(e);
        }
    }

    // Порядок событий — хронологический, а не по машинам: рекорд считается
    // относительно того, что было ДО него в заезде.
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.t < b.t; });

    // Кто сколько кругов проехал и когда закончил последний — по этому
    // считается лидер (см. ниже).
    struct Standing { int laps = 0; float lastLapAt = 0.f; };
    std::map<int32_t, Standing> standings;

    constexpr float eps = 0.001f;
    for (const Entry& e : entries) {
        const std::string& name = e.name;
        EvtState& pv = s_prev[e.car];

        if (e.lapTime > 0.f && (pv.bestLap < 0.f || e.lapTime < pv.bestLap - eps)) {
            char tb[16]; fmtSec(e.lapTime, tb, sizeof(tb));
            const bool overall = (s_sessBestLap < 0.f || e.lapTime < s_sessBestLap - eps);
            if (overall) {
                s_sessBestLap = e.lapTime;
                pushEvent(e.t, "Fastest lap - " + name + "  " + tb, EV_OVERALL);
            } else {
                pushEvent(e.t, "Personal best lap - " + name + "  " + tb, EV_PBLAP);
            }
            pv.bestLap = e.lapTime;
        }

        for (int k = 0; k < SECTOR_COUNT && k < 3; ++k) {
            const float t = e.sec[k];
            if (t <= 0.f) continue;                      // SECTOR_TIME_NONE
            if (pv.bestSec[k] >= 0.f && t >= pv.bestSec[k] - eps) continue;

            char tb[16]; fmtSec(t, tb, sizeof(tb));
            char head[24];
            const bool overall = (s_sessBestSec[k] < 0.f || t < s_sessBestSec[k] - eps);
            if (overall) {
                s_sessBestSec[k] = t;
                snprintf(head, sizeof(head), "Fastest S%d - ", k + 1);
                pushEvent(e.t, std::string(head) + name + "  " + tb, EV_OVERALL);
            } else {
                snprintf(head, sizeof(head), "Best S%d - ", k + 1);
                pushEvent(e.t, std::string(head) + name + "  " + tb, EV_PBSEC);
            }
            pv.bestSec[k] = t;
        }

        // ЛИДЕР — по кругам и по времени их закрытия, а не по мгновенному
        // прогрессу. Больше всего пройденных кругов; при равенстве — тот, кто
        // закрыл свой последний круг раньше. Ровно так считает лидера любое
        // табло хронометража.
        //
        // Поле LapData::positionAtFinish для этого не годится: хронометраж
        // кладёт туда ноль на каждом круге (см. RaceManager, «Store completed
        // lap»), поэтому «место на финише» в записи попросту не сохранено.
        // Строить событие на нём — значит не выдать его ни разу.
        {
            Standing& own = standings[e.car];
            own.laps      = e.lap;
            own.lastLapAt = e.t;

            int32_t leader = INT_MIN;
            int     bestLaps = -1;
            float   bestAt   = 0.f;
            for (const auto& [car, st] : standings) {
                if (st.laps > bestLaps || (st.laps == bestLaps && st.lastLapAt < bestAt)) {
                    bestLaps = st.laps;
                    bestAt   = st.lastLapAt;
                    leader   = car;
                }
            }

            if (leader != INT_MIN && leader != s_leader) {
                // Первого лидера тоже объявляем: в разборе записи «кто повёл
                // гонку» — такое же событие, как и любая последующая смена.
                s_leader = leader;
                for (const Entry& who : entries)
                    if (who.car == leader) { pushEvent(e.t, who.name + " takes the lead", EV_LEAD); break; }
            }
        }
    }

    s_journal_mode = true;
}

// Poll vehicle/race state once per frame and append any new events.
static void detectEvents() {
    // Открылась (или закрылась) другая запись — журнал предыдущей к ней не
    // относится ни одной строкой.
    {
        static uint64_t s_session_seen = 0;
        if (ReplaySessionChanged(s_session_seen)) {
            resetTracking();
            s_journal_mode  = false;
            s_journal_built = false;
            s_state = SessionState::Idle;
            s_init  = false;
        }
    }

    // На повторе журнал ЧИТАЕТСЯ ИЗ ЗАПИСИ. Строим один раз, как только прогрев
    // отдал журнал; дальше панель только отрезает будущее по точке просмотра.
    if (telemetry::replay_is_active()) {
        if (!s_journal_built) {
            const std::vector<int32_t> ids = telemetry::replay_journal_vehicles();
            if (!ids.empty()) {
                buildFromJournal(ids);
                s_journal_built = true;
            }
        }
        return;
    }

    s_journal_mode = false;

    if (!g_race_manager) return;

    const float  sessT = SessionTimeSeconds();   // часы ЗАЕЗДА, см. ProView.h
    SessionState st    = g_race_manager->GetSessionState();

    // Перемотки здесь больше нет: до сюда доходит только живой заезд, а он
    // назад не мотается. На повторе журнал строится из записи целиком, и точка
    // просмотра лишь отрезает будущее при показе - подрезать очередь по
    // откатам не нужно.

    // Race flag changes (Track Server) - race-control events in the log.
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
        // Рекорды считаем ТОЛЬКО по завершённым секторам с измеренным временем.
        //
        // Раньше здесь разбирался лог телеметрии, и лучший сектор брался в том
        // числе из НЕДОЕХАННОГО круга: пока машина едет, его «время сектора»
        // всё уменьшается, каждый кадр оказывается новым рекордом - отсюда и
        // сыпались одинаковые строки пачками, да ещё и с прочерком вместо
        // времени. Законченный сектор неизменяем, поэтому событие про него
        // может произойти ровно один раз.
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();

        for (const auto& [id, v] : snapshot->vehicles) {
            Snap s;
            s.id = id;
            s.name = (v.name.empty() || v.name == "Unknown") ? ("CAR " + std::to_string(id)) : v.name;
            s.bestLap = v.best_lap_time;
            for (int k = 0; k < 3; ++k) { s.sec[k] = -1.f; s.secV[k] = false; }
            for (const auto& [ln, lap] : v.laps_ref()) {
                for (int k = 0; k < 3; ++k) {
                    const float t = lap.sectors[k];
                    if (t <= 0.f) continue;
                    if (!s.secV[k] || t < s.sec[k]) { s.sec[k] = t; s.secV[k] = true; }
                }
            }
            s.prog = v.total_progress;
            s.started = v.has_started_first_lap;
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
            if (overall) { s_sessBestLap = s.bestLap; pushEvent(sessT, "Fastest lap - " + s.name + "  " + tb, EV_OVERALL); }
            else         pushEvent(sessT, "Personal best lap - " + s.name + "  " + tb, EV_PBLAP);
            pv.bestLap = s.bestLap;
        }

        for (int k = 0; k < 3; ++k) {
            if (s.secV[k] && s.sec[k] > 0.f &&
                (pv.bestSec[k] < 0.f || s.sec[k] < pv.bestSec[k] - eps)) {
                char tb[16]; fmtSec(s.sec[k], tb, sizeof(tb));
                bool overall = (s_sessBestSec[k] < 0.f || s.sec[k] < s_sessBestSec[k] - eps);
                char head[24];
                if (overall) { s_sessBestSec[k] = s.sec[k]; snprintf(head, sizeof(head), "Fastest S%d - ", k + 1);
                               pushEvent(sessT, std::string(head) + s.name + "  " + tb, EV_OVERALL); }
                else { snprintf(head, sizeof(head), "Best S%d - ", k + 1);
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

    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({210.f * ui, topH + 600.f * ui}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({200.f * ui, 225.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({120.f * ui, 80.f * ui}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##Events", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::End(); return;
    }

    float w = ImGui::GetWindowWidth();
    float z = PanelZoom("Events");
    DrawPanelHeader(ctx, "EVENTS", false, "Events");

    float scrollH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##evScroll", {w, scrollH}, false, ImGuiWindowFlags_NoNav);

    float fSz  = (ctx.russo   ? ctx.russo->FontSize   : ImGui::GetFontSize()) * z;
    float fReg = (ctx.regular ? ctx.regular->FontSize : ImGui::GetFontSize()) * z;
    float pad  = pad_px() * z;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // На повторе журнал построен из записи ЦЕЛИКОМ, поэтому показывать его
    // целиком нельзя: оператор увидел бы рекорды из ещё не сыгранной части. Всё
    // после точки просмотра отрезаем - и это единственное, что зависит от того,
    // где стоит запись.
    //
    // Точку просмотра спрашиваем ОДИН РАЗ на кадр: внутри она берёт мьютекс
    // проигрывателя, а строк в журнале до тысячи.
    constexpr float EVENT_TIME_EPS = 0.25f;
    const float playhead = SessionTimeSeconds();
    const auto visible = [&](const LogEvent& ev) {
        return !s_journal_mode || ev.t <= playhead + EVENT_TIME_EPS;
    };

    bool anyVisible = false;
    for (const LogEvent& ev : s_log)
        if (visible(ev)) { anyVisible = true; break; }

    if (!anyVisible) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ctx.regular, fReg, {p.x + pad, p.y + 4.f}, COL_DIM, "No events yet");
    }

    for (const LogEvent& ev : s_log) {       // newest first
        if (!visible(ev)) continue;

        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ctx.russo,   fSz,  {p.x + pad, p.y + 1.f},        IM_COL32(0x51,0x51,0x51,255), ev.time);
        dl->AddText(ctx.regular, fReg, {p.x + pad, p.y + fSz + 2.f},  ev.col, ev.text.c_str());

        ImGui::Dummy(ImVec2(w, fSz + fReg + 6.f * z));
        ImVec2 sp = ImGui::GetCursorScreenPos();
        dl->AddLine({sp.x, sp.y}, {sp.x + w, sp.y}, COL_SEP, 1.f);
        ImGui::Dummy(ImVec2(w, 1.f));
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Pro
