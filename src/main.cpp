#include "game/AbilitySystem.h"
#include "net/Replication.h"

#include <iostream>

int main() {
    using namespace aetherwake;
    AbilitySystem magic;
    PlayerState wayfinder{1, "Aster", 100, 100, 0, {"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}};
    EnemyState warden{101, "Thorn Warden", 80, false};
    WorldPropState brambleGate{"hollowmere.bramble_gate", false, ""};

    std::cout << "Aetherwake — The Veiled Reach simulation\n\n";
    const auto reveal = magic.cast(wayfinder, &warden, nullptr, "veil_sight");
    const auto burn = magic.cast(wayfinder, &warden, &brambleGate, "ember_lance");

    const aetherwake::net::SpellResolved replication{
        wayfinder.id, warden.id, "ember_lance", burn.damage, burn.worldEvent, 2};
    std::cout << reveal.reason << " Thorn Warden is exposed.\n";
    std::cout << burn.reason << " Ember Lance dealt " << replication.damage << " damage.\n";
    std::cout << "World state: " << brambleGate.id << " -> " << brambleGate.changeDescription << "\n";
    std::cout << "Warden HP: " << warden.health << " | Wayfinder mana: " << wayfinder.mana << "\n";
    std::cout << "Replication event queued for host tick " << replication.hostTick << ".\n";
    return 0;
}
