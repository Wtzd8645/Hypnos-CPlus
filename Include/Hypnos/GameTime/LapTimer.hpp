#pragma once

#include "GameTimerBase.hpp"

namespace Blanketmen {
namespace Hypnos {

class LapTimer: public GameTimerBase
{
public:
    Delegate<LapTimer&> OnLapTimeUp;
    Delegate<LapTimer&> OnTimeUp;

    void Set(float32 time,int32 laps);
    void Reset() override;
    void Restart() override;
    void Add(int32 laps);
    void Scale(float32 additiveIncreaseRatio) override;

protected:
    void Tick(const GameTime& timeInfo) override;

private:
    float32 scaledInterval;
    float32 lapLeftTime;
    int32 totalLap;
    int32 leftLap;
};

} // namespace Hypnos
} // namespace Blanketmen