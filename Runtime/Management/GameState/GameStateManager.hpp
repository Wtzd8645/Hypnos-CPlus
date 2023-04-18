#ifndef GAME_STATE_MANAGER_HPP_
#define GAME_STATE_MANAGER_HPP_

#include "GameStateBase.hpp"

namespace Blanketmen {
namespace Hypnos {

class GameStateManager
{
public:
    inline static GameStateManager& Instance() noexcept
    {
        static GameStateManager instance;
        return instance;
    }

private:
    GameStateManager() { }
    GameStateManager(GameStateManager const&) = delete;
    void operator=(GameStateManager const&) = delete;
    ~GameStateManager() { }

public:
    void Initialize(GameStateBase* startState);
    void Release();

    void Start();
    void Update();

    void Switch();

private:
    GameStateBase* currentState = nullptr;
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // GAME_STATE_MANAGER_HPP_