#pragma once

struct ImFont;

/// Модалка настроек приложения (Settings / Configuration) в стиле Accounts.
/// Раздел «Connected Devices» — редактирование имён и групп трекеров по ID,
/// данные лежат в DeviceRegistry (SQLite). Панель — только представление.
namespace SettingsPanel {

/// Открыть/закрыть окно (зовётся из пункта меню Settings → Configuration).
void Toggle();
bool IsOpen();

/// Отрисовать модалку. Ничего не делает, пока окно закрыто. Зовётся каждый кадр
/// из UI после основного интерфейса. bodyFont/boldFont — шрифты приложения.
void Render(ImFont* bodyFont, ImFont* boldFont);

} // namespace SettingsPanel
