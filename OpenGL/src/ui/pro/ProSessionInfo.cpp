#include "ui/pro/ProSessionInfo.h"
#include "core/WorldSnapshot.h"
#include "input/Input.h"          // loaded_track_name
#include "network/ReplayPlayer.h"
#include <imgui.h>
#include <cstdio>
#include <ctime>
#include <string>

namespace Pro {

void RenderSessionInfoWindow(const ProContext& ctx, int32_t vehicleId,
                              ImVec2 vpSz, float topH) {
    const float ui = ui_scale::get();
    ImGui::SetNextWindowPos ({0.f,   topH + 710.f * ui},      ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210.f * ui, 140.f * ui},        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({140.f * ui, 80.f * ui}, {vpSz.x, vpSz.y});

    if (!ImGui::Begin("##SessionInfo", nullptr,
        PanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End(); return;
    }

    float z = PanelZoom("SessionInfo");
    DrawPanelHeader(ctx, "SESSION INFO", false, "SessionInfo");
    ImGui::SetWindowFontScale(z);

    std::string driverName = "---";
    {
        const std::shared_ptr<const world::Snapshot> snapshot = world::current();
        if (const world::VehicleView* v = world::find(*snapshot, vehicleId))
            driverName = v->name;
    }

    // Трасса: в повторе — та, на которой сделана запись, иначе загруженная.
    // Панель описывает ПОКАЗАННЫЙ заезд, а не состояние приложения.
    const telemetry::ReplayStatus replay = telemetry::replay_status();
    std::string circuit = replay.active ? replay.track_name : loaded_track_name();
    if (circuit.empty())
        circuit = "---";

    // Дата заезда, а не дата запуска приложения: запись хранит свой день в
    // заголовке (utc_date). Живой заезд идёт сегодня — там сегодняшняя дата и
    // есть правильный ответ.
    char dateBuf[32] = "---";
    if (replay.active && replay.date_yyyymmdd != 0) {
        const uint32_t date = replay.date_yyyymmdd;
        snprintf(dateBuf, sizeof(dateBuf), "%02u/%02u/%04u",
                 date % 100, (date / 100) % 100, date / 10000);
    } else {
        std::time_t t = std::time(nullptr);
        std::tm tm_info{};
        if (localtime_s(&tm_info, &t) == 0)
            strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &tm_info);
    }

    // Часы сессии — общие с остальным PRO (SessionTimeSeconds): в повторе это
    // положение В ЗАПИСИ, поэтому они встают на паузе и отматываются назад
    // вместе с заездом. Своё измерение времени здесь разошлось бы с таймером в
    // навбаре и с журналом событий.
    char elapsed[32] = "--:--";
    {
        const float e = SessionTimeSeconds();
        if (e > 0.f) {
            const int m = (int)(e / 60.f), s = (int)(e) % 60;
            snprintf(elapsed, sizeof(elapsed), "%02d:%02d", m, s);
        }
    }

    LabelValue(ctx, "Driver",   driverName.c_str());
    LabelValue(ctx, "Circuit",  circuit.c_str());
    LabelValue(ctx, "Vehicle",  std::to_string(vehicleId).c_str());
    LabelValue(ctx, "Session",  elapsed);
    LabelValue(ctx, "Date",     dateBuf);

    ImGui::End();
}

} // namespace Pro
