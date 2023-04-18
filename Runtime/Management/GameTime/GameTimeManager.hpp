#ifndef GAME_TIME_MANAGER_HPP_
#define GAME_TIME_MANAGER_HPP_

#include <chrono>
#include <memory>
#include <Core/Runtime/Type.hpp>
#include "GameTimeDefinition.hpp"

namespace Blanketmen {
namespace Hypnos {

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

} // namespace Hypnos
} // namespace Blanketmen

#endif // GAME_TIME_MANAGER_HPP_