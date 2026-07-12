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
    // maxRing limits how far out chunks are drawn (999 = everything); the
    // shadow depth pass only needs the rings the light frustum covers.
    void drawTerrain(int maxRing = 999) const;
    void drawGrass(int maxRing = 1) const;
    // Draws scattered environment meshes; lists holds one GL display list per detail type.
    void drawDetails(const unsigned int* lists, int listCount, float excludeX = 0.0F, float excludeZ = 0.0F, float excludeRadius = 0.0F) const;
    [[nodiscard]] int loadedChunkCount() const { return static_cast<int>(visible_.size()); }
    static float heightAt(float x, float z);
private:
    struct Chunk { unsigned int list{}; unsigned int grassList{}; int lod{-1}; std::vector<DetailInstance> details; };
    std::unordered_map<std::uint64_t, Chunk> chunks_;
    std::vector<std::uint64_t> visible_;
    int centerX_{}; int centerZ_{};
};
}
