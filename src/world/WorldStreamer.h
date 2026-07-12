#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace aetherwake::world {
struct DetailInstance { float x{}; float y{}; float z{}; float scale{}; float rotation{}; int type{}; };

class WorldStreamer {
public:
    static constexpr float waterLevel = -8.0F;
    ~WorldStreamer();
    void update(float playerX, float playerZ);
    void drawTerrain() const;
    // Draws scattered environment meshes; lists holds one GL display list per detail type.
    void drawDetails(const unsigned int* lists, int listCount) const;
    [[nodiscard]] int loadedChunkCount() const { return static_cast<int>(visible_.size()); }
    static float heightAt(float x, float z);
private:
    struct Chunk { unsigned int list{}; int lod{-1}; std::vector<DetailInstance> details; };
    std::unordered_map<std::uint64_t, Chunk> chunks_;
    std::vector<std::uint64_t> visible_;
};
}
