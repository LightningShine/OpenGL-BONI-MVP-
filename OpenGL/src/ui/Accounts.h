#pragma once

#include <imgui/imgui.h>

// ============================================================================
// AccountsPanel — Networking → Accounts (admin only): create timed user
// accounts on the RAJAGP Track Server (1 hour / 1 day / unlimited), see who
// exists and when access expires, revoke access, copy tokens to hand out.
//
// Race control (flags) lives in the Race menu, not here.
// ============================================================================

namespace AccountsPanel {

void Toggle();
// bodyFont / boldFont may be nullptr — falls back to the current ImGui font.
void Render(ImFont* bodyFont = nullptr, ImFont* boldFont = nullptr);

} // namespace AccountsPanel
