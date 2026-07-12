#include "world/WorldStreamer.h"

#include <SDL3/SDL_opengl.h>

#include <cmath>

namespace aetherwake::world {
namespace { constexpr float chunkSize = 32.0F; constexpr int resolution = 16; constexpr int radius = 3; }

float WorldStreamer::heightAt(float x, float z) {
    const float broad = std::sin(x * 0.018F) * 2.8F + std::cos(z * 0.022F) * 2.1F;
    const float ridges = std::sin((x + z) * 0.051F) * 0.75F + std::cos((x - z) * 0.043F) * 0.55F;
    return broad + ridges - 3.0F;
}

void WorldStreamer::update(float playerX, float playerZ) {
    const int centerX = static_cast<int>(std::floor(playerX / chunkSize));
    const int centerZ = static_cast<int>(std::floor(playerZ / chunkSize));
    loaded_.clear(); loaded_.reserve((radius * 2 + 1) * (radius * 2 + 1));
    for (int z = centerZ - radius; z <= centerZ + radius; ++z)
        for (int x = centerX - radius; x <= centerX + radius; ++x) loaded_.push_back({x, z});
}

void WorldStreamer::draw() const {
    glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    for (const auto& chunk : loaded_) {
        const float originX = chunk.x * chunkSize, originZ = chunk.z * chunkSize;
        const float step = chunkSize / resolution;
        glBegin(GL_TRIANGLES);
        for (int z = 0; z < resolution; ++z) for (int x = 0; x < resolution; ++x) {
            const float x0 = originX + x * step, x1 = x0 + step, z0 = originZ + z * step, z1 = z0 + step;
            const float y00 = heightAt(x0, z0), y10 = heightAt(x1, z0), y01 = heightAt(x0, z1), y11 = heightAt(x1, z1);
            const float nx = heightAt(x0 - 0.5F, z0) - heightAt(x0 + 0.5F, z0); const float nz = heightAt(x0, z0 - 0.5F) - heightAt(x0, z0 + 0.5F); const float length = std::sqrt(nx * nx + nz * nz + 1.0F);
            glNormal3f(nx / length, 1.0F / length, nz / length);
            const float tint = 0.04F * std::sin((x0 + z0) * 0.08F); glColor3f(0.09F + tint, 0.19F + tint, 0.105F + tint);
            glVertex3f(x0, y00, z0); glVertex3f(x1, y10, z0); glVertex3f(x1, y11, z1);
            glVertex3f(x0, y00, z0); glVertex3f(x1, y11, z1); glVertex3f(x0, y01, z1);
        }
        glEnd();
    }
    glDisable(GL_COLOR_MATERIAL);
}
}
