#pragma once
// ============================================================================
// START STOP SYSTEM HEADER FILE
// ============================================================================

enum class SessionState {
    Idle,       // Waiting (before start). No results written, free practice mode.
    Active,     // Session ongoing.
    Finishing,  // "Stop session" clicked. Racers finish their CURRENT lap.
    Ended       // Everyone finished.
};
