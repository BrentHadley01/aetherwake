#include "game/AbilitySystem.h"

#include <algorithm>

namespace aetherwake {

AbilitySystem::AbilitySystem() {
    spells_.emplace("ember_lance", SpellDefinition{"ember_lance", "Ember Lance", Element::Ember, 18, 32, "brambles_burned"});
    spells_.emplace("tidal_bind", SpellDefinition{"tidal_bind", "Tidal Bind", Element::Tide, 22, 14, "water_conjured"});
    spells_.emplace("stone_lift", SpellDefinition{"stone_lift", "Stone Lift", Element::Stone, 28, 8, "slab_raised"});
    spells_.emplace("veil_sight", SpellDefinition{"veil_sight", "Veil Sight", Element::Veil, 12, 0, "sigil_revealed"});
}

const SpellDefinition* AbilitySystem::find(const std::string& spellId) const {
    const auto found = spells_.find(spellId);
    return found == spells_.end() ? nullptr : &found->second;
}

CastResult AbilitySystem::cast(PlayerState& caster, EnemyState* target, WorldPropState* prop,
                                const std::string& spellId) const {
    const auto* spell = find(spellId);
    if (spell == nullptr) return {false, "Unknown spell.", 0, ""};
    if (!caster.learnedSpells.contains(spellId)) return {false, "Spell not learned.", 0, ""};
    if (caster.mana < spell->manaCost) return {false, "Insufficient mana.", 0, ""};

    caster.mana -= spell->manaCost;
    CastResult result{true, "Cast accepted by host.", spell->baseDamage, spell->worldEffect};
    if (target != nullptr) {
        const int bonus = target->exposed && spell->element != Element::Veil ? 12 : 0;
        result.damage += bonus;
        target->health = std::max(0, target->health - result.damage);
        if (spell->element == Element::Veil) target->exposed = true;
        if (target->health == 0) caster.experience += 50;
    }
    if (prop != nullptr) {
        prop->changed = true;
        prop->changeDescription = spell->worldEffect;
    }
    return result;
}

} // namespace aetherwake
