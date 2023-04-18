#ifndef GAME_STATE_BASE_HPP_
#define GAME_STATE_BASE_HPP_

namespace Blanketmen {
namespace Hypnos {

class GameStateBase
{
public:
    virtual ~GameStateBase() { }

    virtual void Enter() = 0;
    virtual void Exit() = 0;
    virtual void Update() = 0;
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // GAME_STATE_BASE_HPP_