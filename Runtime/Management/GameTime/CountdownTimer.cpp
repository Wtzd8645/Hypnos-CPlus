#include "Hypnos/GameTime/CountdownTimer.hpp"

namespace Blanketmen {
namespace Hypnos {

void CountdownTimer::Set(float32 time)
{
    interval = time;
    leftTime = time;
}

void CountdownTimer::Reset()
{
    isStop = true;
    OnTimeUp.Clear();
}

void CountdownTimer::Restart()
{
    isStop = false;
    leftTime = interval;
}

void CountdownTimer::Add(float32 deltaTime)
{
    leftTime += deltaTime;
}

void CountdownTimer::Scale(float32 additiveIncreaseRatio)
{
    if (additiveIncreaseRatio < 0.0f) // Accelerate
    {
        leftTime /= (1.0f - additiveIncreaseRatio);
    }
    else // Decelerate
    {
        leftTime *= (1.0f + additiveIncreaseRatio);
    }
}

void CountdownTimer::Tick(const GameTime& timeInfo)
{
    leftTime -= timeInfo.deltaTime;
    if (leftTime > 0.0f)
    {
        return;
    }

    isStop = true;
    OnTimeUp(*this);
}

} // namespace Hypnos
} // namespace Blanketmen