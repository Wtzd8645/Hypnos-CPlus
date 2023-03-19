#include <thread>
#include "../Kernel.hpp"
#include "GameTimeManager.hpp"
#include "GameTimerBase.hpp"

namespace Blanketmen {
namespace Hypnos {

GameTimeManager* GameTimeManager::instance = nullptr;

GameTimeManager::GameTimeManager() { }

GameTimeManager::~GameTimeManager() { }

void GameTimeManager::Clear()
{
    GameTimerBase* currNode = headNode;
    while (currNode != nullptr)
    {
        GameTimerBase* nextNode = currNode->nextNode;
        currNode->Reset();
        currNode->nextNode = nullptr;
        currNode = nextNode;
    }
    headNode = nullptr;
    tailNode = nullptr;
}

void GameTimeManager::AddLast(GameTimerBase* timer)
{
    if (timer->nextNode != nullptr || timer == tailNode)
    {
        return;
    }

    if (headNode == nullptr)
    {
        headNode = timer;
        tailNode = timer;
    }
    else
    {
        tailNode->nextNode = timer;
        tailNode = timer;
    }
}

void GameTimeManager::Update()
{
    frameStart = steady_clock::now();
    GameTimerBase* prevNode = nullptr;
    GameTimerBase* currNode = headNode;
    while (currNode != nullptr)
    {
        if (currNode->isStop)
        {
            // Remove node.
            if (prevNode == nullptr)
            {
                headNode = currNode->nextNode;
            }
            else
            {
                prevNode->nextNode = currNode->nextNode;
            }

            // Set last node.
            if (currNode->nextNode == nullptr)
            {
                tailNode = prevNode;
            }

            // Move to next node.
            GameTimerBase* nextNode = currNode->nextNode;
            currNode->nextNode = nullptr;
            currNode = nextNode;
            continue;
        }

        prevNode = currNode;
        currNode->Tick(timeInfo);
        currNode = currNode->nextNode;
    }
}

void GameTimeManager::LateUpdate()
{
    uint64 deltaTimeTick = (steady_clock::now() - frameStart).count();
    if (deltaTimeTick < minDeltaTimeTick)
    {
        timeInfo.deltaTime = minDeltaTimeTick / DeltaTimeTickRatio;
        std::this_thread::sleep_for(std::chrono::nanoseconds(minDeltaTimeTick - deltaTimeTick));
    }
    else
    {
        timeInfo.deltaTime = deltaTimeTick / DeltaTimeTickRatio;
    }
}

} // namespace Hypnos
} // namespace Blanketmen