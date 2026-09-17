#pragma once

#include <string>

struct ResolutionLevel {
    ResolutionLevel() = default;
    ResolutionLevel(int xLevel, int yLevel) : x(xLevel), y(yLevel) {}

    int x = 0;
    int y = 0;

    bool operator==(ResolutionLevel other) const { return x == other.x && y == other.y; }
    bool operator!=(ResolutionLevel other) const { return !(*this == other); }
    bool operator<(ResolutionLevel other) const
    {
        return x < other.x || (x == other.x && y < other.y);
    }
    std::string toString() const
    {
        return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
};
