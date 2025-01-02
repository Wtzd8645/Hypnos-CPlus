#pragma once

#include "GameTimeDefinition.hpp"
#include "GameTimeManager.hpp"
#include <Hypnos-Core/Type.hpp>

namespace Blanketmen {
namespace Hypnos {

class GameTimerBase
{
public:
    friend class GameTimeManager;

    virtual ~GameTimerBase() { }

    inline bool IsStop() const noexcept {
        return isStop;
    }
    inline float32 LeftTime() const noexcept {
        return leftTime;
    }

    virtual void Reset() = 0;
    virtual void Restart() = 0;

    inline void Start()
    {
        isStop = false;
        GameTimeManager::Instance().AddLast(this);
    }

    inline void Stop()
    {
        isStop = true;
    }

    /// <summary>
    /// Positive is Decelerate; Negative number is acceleration.
    /// For example: 0.8f equals +80% current time and -0.5f equals -50% current time.
    /// </summary>
    /// <param name="additiveIncreaseRatio"></param>
    virtual void Scale(float additiveIncreaseRatio) = 0;

protected:
    bool isStop;
    float32 leftTime;
    float32 interval;

    virtual void Tick(const GameTime& timeInfo) = 0;

private:
    GameTimerBase* nextNode;
};

} // namespace Hypnos
} // namespace Blanketmen