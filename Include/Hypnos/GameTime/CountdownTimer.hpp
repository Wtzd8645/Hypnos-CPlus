#pragma once

#include "GameTimerBase.hpp"
#include <Hypnos-Core/Mediation/MultiDelegate.hpp>

namespace Blanketmen {
namespace Hypnos {

class CountdownTimer: public GameTimerBase
{
public:
    MultiDelegate<CountdownTimer&> OnTimeUp;

    void Set(float32 time);
    void Reset() override;
    void Restart() override;
    void Add(float32 deltaTime);
    void Scale(float32 additiveIncreaseRatio) override;

protected:
    void Tick(const GameTime& timeInfo) override;
};

} // namespace Hypnos
} // namespace Blanketmen