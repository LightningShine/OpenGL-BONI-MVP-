#pragma once

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
    Practice,
    Race,
    Finishing,
    Finished
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
        case RacePhase::Race:
            return "Race";
        case RacePhase::Finishing:
            return "Finishing";
        case RacePhase::Finished:
            return "Finished";
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
