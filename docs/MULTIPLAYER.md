# Multiplayer architecture

## Authority

The session host owns the simulation. Clients submit timestamped intent (`MoveIntent`, `CastIntent`, `InteractIntent`); the host validates cooldown, mana, range, line of sight, and target state, then broadcasts resulting snapshots. This prevents a client from declaring damage or unlocks directly.

## Replicated state

- Player transform, velocity, health, mana, equipped and learned spells.
- Enemy transforms, health, behavior state, and encounter seed.
- World-prop state only when changed: burned brambles, lifted slabs, revealed sigils, encounter completion.

## Transport boundary

`src/net/Replication.h` intentionally models messages without selecting a vendor. The production integration should use Steamworks networking sockets and Steam lobbies, with Epic Online Services only evaluated if cross-store identity becomes a release requirement.

## Hard rules

- Never replicate visual effects as authority; replicate an event and let each client render it.
- Never trust a client for XP, inventory, spell unlocks, or damage.
- Use fixed-timestep simulation and deterministic random seeds for encounter decisions.
- Add soak tests before inviting external players.
