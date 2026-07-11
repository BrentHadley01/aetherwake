#pragma once

#include <cstdint>
#include <string>

namespace aetherwake::net {

// Client-to-host intent. The host validates this against its own authoritative state.
struct CastIntent {
    std::uint32_t playerId{};
    std::uint32_t targetId{};
    std::string spellId;
    std::uint64_t clientTick{};
};

// Host-to-client event. VFX and sound are derived locally from this event.
struct SpellResolved {
    std::uint32_t playerId{};
    std::uint32_t targetId{};
    std::string spellId;
    int damage{};
    std::string worldEvent;
    std::uint64_t hostTick{};
};

} // namespace aetherwake::net
