#include "Hypnos/GameState/GameStateManager.hpp"

namespace Blanketmen {
namespace Hypnos {

void GameStateManager::Initialize(GameStateBase* startState)
{
    currentState = startState;
}

void GameStateManager::Release()
{
}

void GameStateManager::Start()
{
    if (currentState == nullptr)
    {
        return;
    }

    currentState->Enter();
}

void GameStateManager::Update()
{
    currentState->Update();
}

void GameStateManager::Switch()
{
    currentState->Exit();
    currentState = nullptr;
    currentState->Enter();
}

} // namespace Hypnos
} // namespace Blanketmen