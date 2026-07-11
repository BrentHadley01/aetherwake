#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

namespace aetherwake {

enum class Element { Ember, Tide, Stone, Veil };

struct PlayerState {
    std::uint32_t id{};
    std::string name;
    int health{100};
    int mana{100};
    int experience{};
    std::unordered_set<std::string> learnedSpells;
};

struct EnemyState {
    std::uint32_t id{};
    std::string archetype;
    int health{};
    bool exposed{};
};

struct WorldPropState {
    std::string id;
    bool changed{};
    std::string changeDescription;
};

} // namespace aetherwake
