#pragma once

namespace aetherwake::engine {

struct EngineFeatures {
    static constexpr bool rendering = true;
    static constexpr bool input = true;
    static constexpr bool ecs = true;
    static constexpr bool physics = true;
    static constexpr bool audio = true;
    static constexpr bool networking = true;
    static constexpr bool tooling = true;
    static constexpr bool serialization = true;
};

} // namespace aetherwake::engine
