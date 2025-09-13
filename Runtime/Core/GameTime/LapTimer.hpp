#pragma once

#include "GameTimerBase.hpp"
#include <Hypnos-Kernel/Mediation/MulticastDelegate.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace GameTime {

class LapTimer: public GameTimerBase
{
public:
    MulticastDelegate<LapTimer&> OnLapTimeUp;
    MulticastDelegate<LapTimer&> OnTimeUp;

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

} // namespace GameTime
} // namespace Hypnos
} // namespace Blanketmen