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
    void drawGrass(int maxRing = 1, float eyeX = 0.0F, float eyeZ = 0.0F,
                   float viewForwardX = 0.0F, float viewForwardZ = 0.0F) const;
    // Draws scattered environment meshes; lists holds one GL display list per detail type.
    // Detail meshes are high-poly authored assets. `maxDistance` and the
    // optional view vector cull only geometry that cannot affect the frame.
    void drawDetails(const unsigned int* lists, int listCount, float excludeX = 0.0F, float excludeZ = 0.0F, float excludeRadius = 0.0F,
                     float maxDistance = 0.0F, float viewForwardX = 0.0F, float viewForwardZ = 0.0F) const;
    [[nodiscard]] int loadedChunkCount() const { return static_cast<int>(visible_.size()); }
    // Pushes the position out of tree trunks and boulders (circle colliders).
    void resolveCollision(float& x, float& z, float radius) const;
    // Applies a persistent smooth heightfield brush and rebuilds intersecting
    // streamed chunks. Positive strength raises terrain; negative carves it.
    void deformTerrain(float x, float z, float radius, float strength);
    static float heightAt(float x, float z);
private:
    struct Chunk { unsigned int list{}; unsigned int grassList{}; int lod{-1}; bool terrainDirty{}; std::vector<DetailInstance> details; };
    std::unordered_map<std::uint64_t, Chunk> chunks_;
    std::vector<std::uint64_t> visible_;
    int centerX_{}; int centerZ_{};
};
}
