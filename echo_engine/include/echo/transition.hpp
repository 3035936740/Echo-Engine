#pragma once
#include "world.hpp"
#include <string>
namespace echo {
void validateTransition(const TransitionConfig&);
TransitionConfig readTransition(const std::string&);
std::string writeTransition(const TransitionConfig&);
float transitionCover(const TransitionState&);
} // namespace echo
