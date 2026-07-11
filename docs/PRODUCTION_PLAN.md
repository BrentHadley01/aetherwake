# Production plan

## Milestone 0 — playable foundation (this commit)

Original IP, vertical-slice brief, spell interaction rules, authoritative simulation contract, generated mood/key art, and a dependency-free executable proving the gameplay model.

## Milestone 1 — single-player feel

Move this core into Unreal Engine 5 for Nanite/Lumen-quality rendering; implement third-person locomotion, camera, animation state machine, audio, one biome, the four spells, and enemy behavior. Target 60 fps on the agreed minimum PC spec.

## Milestone 2 — four-player co-op

Steamworks lobby flow, host migration decision, prediction/reconciliation, dedicated-server feasibility test, replication profiling, reconnects, save versioning, and automated multiplayer soak tests.

## Milestone 3 — Steam demo

Store capsule/trailers, controller and Steam Deck pass, accessibility (subtitles, remapping, color-safe feedback, difficulty assists), crash reporting, privacy policy, legal review of every asset/license, QA matrix, and playtest telemetry.

## Non-negotiable release gates

- Every shipped asset has a documented license and source.
- No P2P game state may let a client award itself progress or damage.
- Stable four-player completion rate and reconnect behavior under realistic latency.
- Save migration, error handling, controller support, settings, and accessibility are complete before content expansion.
