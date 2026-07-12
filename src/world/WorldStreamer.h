#pragma once

#include <vector>

namespace aetherwake::world {
struct ChunkCoordinate { int x{}; int z{}; };

class WorldStreamer {
public:
    void update(float playerX, float playerZ);
    void draw() const;
    [[nodiscard]] int loadedChunkCount() const { return static_cast<int>(loaded_.size()); }
    static float heightAt(float x, float z);
private:
    std::vector<ChunkCoordinate> loaded_;
};
}
