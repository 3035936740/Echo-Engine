#include "echo/input_bindings.hpp"
#include <sstream>
#include <stdexcept>
namespace echo {
void validateKeys(const KeyBindings& b) {
    for (std::size_t i = 0; i < b.keys.size(); ++i) {
        if (b.keys[i] < 8 || b.keys[i] > 254 || b.keys[i] == 0xF3 || b.keys[i] == 0xF4)
            throw std::runtime_error("Bindings require keyboard virtual keys 8..254");
        for (std::size_t j = 0; j < i; ++j)
            if (b.keys[i] == b.keys[j])
                throw std::runtime_error("Gameplay actions must use different keys");
    }
    if (b.arrowMovement)
        for (std::size_t i = 2; i < b.keys.size(); ++i)
            if (b.keys[i] == 37 || b.keys[i] == 39)
                throw std::runtime_error(
                    "Disable arrow movement before assigning Left/Right to another action");
}
std::string writeKeys(const KeyBindings& b) {
    validateKeys(b);
    std::ostringstream out;
    for (int k : b.keys)
        out << k << ' ';
    out << b.arrowMovement;
    return out.str();
}
KeyBindings readKeys(std::string_view text) {
    KeyBindings b;
    std::istringstream in{std::string(text)};
    for (auto& k : b.keys)
        if (!(in >> k))
            throw std::runtime_error("Invalid keyboard bindings");
    int arrows;
    if (!(in >> arrows) || (arrows != 0 && arrows != 1))
        throw std::runtime_error("Invalid keyboard arrow movement setting");
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Unexpected keyboard binding fields");
    b.arrowMovement = arrows != 0;
    validateKeys(b);
    return b;
}
Input keyboardInput(const KeyBindings& b, const std::array<bool, 256>& held, int focus, int hurt, int dash, int climb) {
    auto down = [&](int k) { return k > 0 && k < 256 && held[k]; };
    Input i;
    i.move = float(int(down(b.keys[1]) || (b.arrowMovement && down(39))) -
                   int(down(b.keys[0]) || (b.arrowMovement && down(37))));
    i.jump = down(b.keys[2]);
    i.down = down(b.keys[3]);
    i.confirm = down(b.keys[4]);
    i.fast = down(b.keys[5]);
    i.trace = down(b.keys[6]);
    i.restart = down(b.keys[7]);
    i.focus = down(focus);
    i.showHurt = down(hurt);
    i.dash = down(dash);
    i.up = down(climb);
    return i;
}
} // namespace echo
