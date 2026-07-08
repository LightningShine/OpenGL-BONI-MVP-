#pragma once
#include <glm/glm.hpp>
#include <imgui.h>
#include <string>

// ============================================================================
// VehicleNameRenderer
// Projects vehicle world positions into screen space and draws names via ImGui.
// No texture atlas required.
// ============================================================================

namespace VehicleNameRenderer
{
    // No-op — kept for API compatibility.
    inline bool Initialize() { return true; }
    inline void Shutdown()   {}

    // Render the name string above the vehicle world position.
    // worldX/Y  : normalized world coords of the vehicle centre
    // viewProj  : combined view-projection matrix (NDC output assumed)
    // charScale : unused (kept for API compatibility)
    void DrawName(const std::string& name,
        float worldX, float worldY,
        const glm::mat4& viewProj,
        ImFont* font = nullptr,
        float textScale = 1.0f);
}
