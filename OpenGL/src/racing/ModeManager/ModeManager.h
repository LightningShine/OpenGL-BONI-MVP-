#pragma once

#include <string>

#include "../StopReset/StartStop.h"

enum class RaceMode
{
    CircuitRace,
    TimeAttack,
    Rally,
    Undefined
};

enum class RacePhase
{
    Practice,      // no race in progress — free running
    Qualification, // reserved: qualifying mode (future)
    Race,          // race running
    Finishing,     // checkered flag out — cars classify at the line
    Finished       // race over, results frozen
};

class ModeManager
{
public:
    void SetMode(RaceMode mode) { m_mode = mode; }
    void SetPhase(RacePhase phase) { m_phase = phase; }

    RaceMode GetMode() const { return m_mode; }
    RacePhase GetPhase() const { return m_phase; }

    void SyncWithSessionState(SessionState state)
    {
        switch (state)
        {
        case SessionState::Idle:
            m_phase = RacePhase::Practice;
            break;
        case SessionState::Active:
            m_phase = RacePhase::Race;
            break;
        case SessionState::Finishing:
            m_phase = RacePhase::Finishing;
            break;
        case SessionState::Ended:
            m_phase = RacePhase::Finished;
            break;
        }
    }

    // Track Server race state ("idle"/"running"/"finishing"/"finished") —
    // used instead of the local session when connected to a server.
    void SyncWithServerState(const std::string& state)
    {
        if      (state == "running")   m_phase = RacePhase::Race;
        else if (state == "finishing") m_phase = RacePhase::Finishing;
        else if (state == "finished")  m_phase = RacePhase::Finished;
        else                           m_phase = RacePhase::Practice;
    }

    const char* GetModeLabel() const
    {
        switch (m_mode)
        {
        case RaceMode::CircuitRace:
            return "CircuitRace";
        case RaceMode::TimeAttack:
            return "TimeAttack";
        case RaceMode::Rally:
            return "Rally";
        default:
            return "Undefined";
        }
    }

    const char* GetPhaseLabel() const
    {
        switch (m_phase)
        {
        case RacePhase::Practice:
            return "Practice";
        case RacePhase::Qualification:
            return "Qualification";
        case RacePhase::Race:
            return "Race";
        case RacePhase::Finishing:
            return "Race Finishing";
        case RacePhase::Finished:
            return "Race Finished";
        default:
            return "Unknown";
        }
    }

    bool ShouldShowTimer() const
    {
        return m_phase != RacePhase::Practice;
    }

private:
    RaceMode m_mode = RaceMode::CircuitRace;
    RacePhase m_phase = RacePhase::Practice;
};
