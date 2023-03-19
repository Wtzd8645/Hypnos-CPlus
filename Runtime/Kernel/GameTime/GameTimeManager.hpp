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

    inline static GameTimeManager* const Instance() noexcept { return instance; }

    inline static void CreateInstance() noexcept
    {
        if (instance == nullptr)
        {
            instance = new GameTimeManager();
        }
    }

    inline static void ReleaseInstance() noexcept
    {
        if (instance != nullptr)
        {
            delete instance;
            instance = nullptr;
        }
    }

private:
    static GameTimeManager* instance;

    GameTimeManager();
    ~GameTimeManager();

public:
    inline float64 DeltaTime() const noexcept { return timeInfo.deltaTime; }

    void Update();
    void LateUpdate();

    void Clear();
    void AddLast(GameTimerBase* timer);

private:
    const float64 DeltaTimeTickRatio = 1000000000.0f; // NOTE: second / nanosecond
    uint64 minDeltaTimeTick = 16000000ul; // NOTE: nanoseconds

    time_point frameStart;
    GameTime timeInfo;

    GameTimerBase* headNode;
    GameTimerBase* tailNode;
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // GAME_TIME_MANAGER_HPP_