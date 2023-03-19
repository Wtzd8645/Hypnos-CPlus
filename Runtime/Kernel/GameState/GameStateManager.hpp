#ifndef GAME_STATE_MANAGER_HPP_
#define GAME_STATE_MANAGER_HPP_

#include "GameStateBase.hpp"

namespace Blanketmen {
namespace Hypnos {

class GameStateManager
{
public:
    inline static GameStateManager* const Instance() noexcept { return instance; }

    inline static void CreateInstance() noexcept
    {
        if (instance == nullptr)
        {
            instance = new GameStateManager();
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
    static GameStateManager* instance;

    GameStateManager();
    ~GameStateManager();

public:
    void Initialize(GameStateBase* startState);
    void Release();

    void Start();
    void Update();

private:
    GameStateBase* currentState = nullptr; // TODO: Temp
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // GAME_STATE_MANAGER_HPP_