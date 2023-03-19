#include "GameStateManager.hpp"

namespace Blanketmen {
namespace Hypnos {

GameStateManager* GameStateManager::instance = nullptr;

GameStateManager::GameStateManager() { }

GameStateManager::~GameStateManager() { }

void GameStateManager::Initialize(GameStateBase* startState)
{
    currentState = startState;
}

void GameStateManager::Release()
{
}

void GameStateManager::Start()
{
    currentState->Enter();
}

void GameStateManager::Update()
{
    currentState->Update();
}

} // namespace Hypnos
} // namespace Blanketmen