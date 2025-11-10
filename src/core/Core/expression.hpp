#pragma once
#include <array>
#include <string>

namespace App::Core {

struct Expression {
    std::array<char, 1024> expr;  // expression as char array
    std::string color;            // color (hex or name)
    bool visible = true;          // whether to render/display
    float thickness = 1.0f;       // optional, for graphing line width
    int id = -1;                  // optional identifier
};

}  // namespace App::Core