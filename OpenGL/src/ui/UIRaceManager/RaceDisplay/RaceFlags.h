#pragma once

#include <cstdint>

enum class FlagColor : uint8_t
{
    Green,
    Yellow,
    Red,
    Blue,
    White,
    Checkered
};

class RaceFlags
{
public:
    RaceFlags() : m_leftFlag(FlagColor::Green), m_rightFlag(FlagColor::Green) {}

    void SetLeftFlag(FlagColor color) { m_leftFlag = color; }
    void SetRightFlag(FlagColor color) { m_rightFlag = color; }

    FlagColor GetLeftFlag() const { return m_leftFlag; }
    FlagColor GetRightFlag() const { return m_rightFlag; }

    // Get RGBA color for a flag
    static void GetFlagRGBA(FlagColor flag, float& r, float& g, float& b, float& a)
    {
        a = 1.0f;
        switch (flag)
        {
        case FlagColor::Green:
            r = 0.0f; g = 1.0f; b = 0.0f;
            break;
        case FlagColor::Yellow:
            r = 1.0f; g = 1.0f; b = 0.0f;
            break;
        case FlagColor::Red:
            r = 1.0f; g = 0.0f; b = 0.0f;
            break;
        case FlagColor::Blue:
            r = 0.0f; g = 0.0f; b = 1.0f;
            break;
        case FlagColor::White:
            r = 1.0f; g = 1.0f; b = 1.0f;
            break;
        case FlagColor::Checkered:
            r = 0.5f; g = 0.5f; b = 0.5f;
            break;
        }
    }

    void ResetFlags()
    {
        m_leftFlag = FlagColor::Green;
        m_rightFlag = FlagColor::Green;
    }

private:
    FlagColor m_leftFlag;
    FlagColor m_rightFlag;
};
