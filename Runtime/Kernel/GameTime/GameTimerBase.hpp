#ifndef GAME_TIMER_BASE_HPP_
#define GAME_TIMER_BASE_HPP_

#include <memory>
#include <Core/Runtime/Type.hpp>
#include <Core/Runtime/Delegate.hpp>
#include "GameTimeDefinition.hpp"
#include "GameTimeManager.hpp"

namespace Blanketmen {
namespace Hypnos {

class GameTimerBase
{
public:
    friend class GameTimeManager;
    virtual ~GameTimerBase() { }

    inline bool IsStop() const noexcept { return isStop; }
    inline float32 LeftTime() const noexcept { return leftTime; }

    virtual void Reset() = 0;
    virtual void Restart() = 0;

    inline void Start()
    {
        isStop = false;
        GameTimeManager::Instance()->AddLast(this);
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

class CountdownTimer : public GameTimerBase
{
public:
    Delegate<CountdownTimer&> OnTimeUp;

    void Set(float32 time);
    void Reset() override;
    void Restart() override;
    void Add(float32 deltaTime);
    void Scale(float32 additiveIncreaseRatio) override;

protected:
    void Tick(const GameTime& timeInfo) override;
};

class LapTimer : public GameTimerBase
{
public:
    Delegate<LapTimer&> OnLapTimeUp;
    Delegate<LapTimer&> OnTimeUp;

    void Set(float32 time, int32 laps);
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

#endif // GAME_TIMER_BASE_HPP_