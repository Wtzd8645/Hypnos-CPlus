#pragma once

#include "GameTimeDefinition.hpp"
#include <chrono>
#include <memory>

namespace Blanketmen {
namespace Hypnos {
namespace GameTime {

class GameTimerBase;

class GameTimeManager
{
public:
    typedef std::chrono::steady_clock steady_clock;
    typedef std::chrono::steady_clock::time_point time_point;

    inline static GameTimeManager& Instance() noexcept
    {
        static GameTimeManager instance;
        return instance;
    }

private:
    GameTimeManager() { }
    GameTimeManager(GameTimeManager const&) = delete;
    void operator=(GameTimeManager const&) = delete;
    ~GameTimeManager() { }

public:
    inline float64 DeltaTime() const noexcept { return timeInfo.deltaTime; }

    void Update();
    void LateUpdate();

    void Clear();
    void AddLast(GameTimerBase* timer);

private:
    const float64 DeltaTimeTickRatio = 1000000000.0f; // NOTE: second / nanosecond
    const uint64 minDeltaTimeTick = 16000000ul; // NOTE: nanoseconds

    time_point frameStart;
    GameTime timeInfo;

    GameTimerBase* headNode;
    GameTimerBase* tailNode;
};

} // namespace GameTime
} // namespace Hypnos
} // namespace Blanketmen