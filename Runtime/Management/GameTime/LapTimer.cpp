#include "GameTimerBase.hpp"

namespace Blanketmen {
namespace Hypnos {

void LapTimer::Set(float32 time, int32 laps)
{
    leftTime = time * laps;
    interval = time;
    scaledInterval = time;
    lapLeftTime = time;
    totalLap = laps;
    leftLap = laps;
}

void LapTimer::Reset()
{
    isStop = true;
    OnLapTimeUp.Clear();
    OnTimeUp.Clear();
}

void LapTimer::Restart()
{
    isStop = false;
    leftTime = interval * totalLap;
    scaledInterval = interval;
    lapLeftTime = interval;
    leftLap = totalLap;
}

void LapTimer::Add(int32 laps)
{
    leftTime += scaledInterval * laps;
    leftLap += laps;
}

void LapTimer::Scale(float32 additiveIncreaseRatio)
{
    if (additiveIncreaseRatio < 0.0f)
    {
        additiveIncreaseRatio = 1.0f - additiveIncreaseRatio;
        scaledInterval /= additiveIncreaseRatio;
        lapLeftTime /= additiveIncreaseRatio;
    }
    else
    {
        additiveIncreaseRatio = 1.0f + additiveIncreaseRatio;
        scaledInterval *= additiveIncreaseRatio;
        lapLeftTime *= additiveIncreaseRatio;
    }

    leftTime = lapLeftTime + scaledInterval * (leftLap - 1);
}

void LapTimer::Tick(const GameTime& timeInfo)
{
    printf("Tick: %f", timeInfo.deltaTime);
    leftTime -= timeInfo.deltaTime;
    lapLeftTime -= timeInfo.deltaTime;
    if (lapLeftTime > 0.0f)
    {
        return;
    }

    lapLeftTime += scaledInterval;
    OnLapTimeUp(*this);

    if (--leftLap < 1)
    {
        isStop = true;
        OnTimeUp(*this);
    }
}

} // namespace Hypnos
} // namespace Blanketmen