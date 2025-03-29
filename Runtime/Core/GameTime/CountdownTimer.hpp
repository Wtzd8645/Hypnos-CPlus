#pragma once

#include "GameTimerBase.hpp"
#include <Hypnos-Kernel/Core/Mediation.hpp>

namespace Blanketmen {
namespace Hypnos {
namespace GameTime {

class CountdownTimer: public GameTimerBase
{
public:
    MulticastDelegate<CountdownTimer&> OnTimeUp;

    void Set(float32 time);
    void Reset() override;
    void Restart() override;
    void Add(float32 deltaTime);
    void Scale(float32 additiveIncreaseRatio) override;

protected:
    void Tick(const GameTime& timeInfo) override;
};

} // namespace GameTime
} // namespace Hypnos
} // namespace Blanketmen