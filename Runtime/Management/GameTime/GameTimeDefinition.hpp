#ifndef GAME_TIME_DEFINITION_HPP_
#define GAME_TIME_DEFINITION_HPP_

#include <Type.hpp>

namespace Blanketmen {
namespace Hypnos {

struct GameTime
{
    float64 unscaledDeltaTime;
    float64 deltaTime;
};

} // namespace Hypnos
} // namespace Blanketmen

#endif // GAME_TIME_DEFINITION_HPP_