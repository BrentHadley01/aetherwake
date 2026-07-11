#pragma once

#include "game/Types.h"

#include <string>
#include <unordered_map>

namespace aetherwake {

struct SpellDefinition {
    std::string id;
    std::string displayName;
    Element element;
    int manaCost;
    int baseDamage;
    std::string worldEffect;
};

struct CastResult {
    bool accepted{};
    std::string reason;
    int damage{};
    std::string worldEvent;
};

class AbilitySystem {
public:
    AbilitySystem();
    [[nodiscard]] const SpellDefinition* find(const std::string& spellId) const;
    CastResult cast(PlayerState& caster, EnemyState* target, WorldPropState* prop,
                    const std::string& spellId) const;

private:
    std::unordered_map<std::string, SpellDefinition> spells_;
};

} // namespace aetherwake
