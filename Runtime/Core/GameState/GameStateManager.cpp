#include "GameStateManager.hpp"

namespace Blanketmen {
namespace Hypnos {

void GameStateManager::Initialize(GameStateBase* startState)
{
    currState = startState;
}

void GameStateManager::Release()
{
}

void GameStateManager::Start()
{
    if (currState == nullptr)
    {
        return;
    }

    currState->Enter();
}

void GameStateManager::Update()
{
    currState->Update();
}

void GameStateManager::Switch()
{
    currState->Exit();
    currState = nullptr;
    currState->Enter();
}

} // namespace Hypnos
} // namespace Blanketmen