#include "world/WorldStreamer.h"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>

namespace aetherwake::world {
namespace {
constexpr float chunkSize = 64.0F;
constexpr int radius = 15;         // 31x31 chunks -> ~2 km of streamed world around the player
constexpr int detailRadius = 4;    // rings that receive scattered trees and boulders
constexpr int buildBudget = 10;    // chunk display lists compiled per update to avoid hitches

std::uint32_t hash2(int x, int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393U + static_cast<std::uint32_t>(z) * 668265263U;
    h = (h ^ (h >> 13)) * 1274126177U;
    return h ^ (h >> 16);
}
float hashToUnit(std::uint32_t h) { return static_cast<float>(h & 0xFFFFFFU) / 16777215.0F; }
float smoothstep01(float t) { return t * t * (3.0F - 2.0F * t); }

float valueNoise(float x, float z) {
    const int ix = static_cast<int>(std::floor(x)), iz = static_cast<int>(std::floor(z));
    const float fx = smoothstep01(x - ix), fz = smoothstep01(z - iz);
    const float a = hashToUnit(hash2(ix, iz)), b = hashToUnit(hash2(ix + 1, iz));
    const float c = hashToUnit(hash2(ix, iz + 1)), d = hashToUnit(hash2(ix + 1, iz + 1));
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz;
}

float fbm(float x, float z, int octaves) {
    float sum = 0.0F, amplitude = 1.0F, total = 0.0F;
    for (int i = 0; i < octaves; ++i) { sum += valueNoise(x, z) * amplitude; total += amplitude; amplitude *= 0.5F; x = x * 2.03F + 19.19F; z = z * 2.03F - 7.31F; }
    return sum / total;
}

std::uint64_t chunkKey(int x, int z) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(z); }
}

float WorldStreamer::heightAt(float x, float z) {
    const float m = fbm(x * 0.0011F + 37.2F, z * 0.0011F - 11.8F, 4);            // broad mountain mass
    const float hills = (fbm(x * 0.004F, z * 0.004F, 5) - 0.5F) * 26.0F;         // rolling relief
    const float detail = (fbm(x * 0.05F + 91.0F, z * 0.05F - 53.0F, 3) - 0.5F) * 2.2F;
    float h = hills * (0.4F + 0.6F * m) + std::pow(m, 2.4F) * 72.0F - 14.0F + detail;
    // Flatten toward the authored Blender landmark so it sits seamlessly at the origin.
    const float distance = std::sqrt(x * x + z * z);
    const float t = smoothstep01(std::clamp((distance - 38.0F) / 72.0F, 0.0F, 1.0F));
    return -0.15F + (h - -0.15F) * t;
}

WorldStreamer::~WorldStreamer() { for (auto& [key, chunk] : chunks_) if (chunk.list) glDeleteLists(chunk.list, 1); }

namespace {
void emitVertex(float x, float z, float step) {
    const float y = WorldStreamer::heightAt(x, z);
    const float nx = WorldStreamer::heightAt(x - step, z) - WorldStreamer::heightAt(x + step, z);
    const float nz = WorldStreamer::heightAt(x, z - step) - WorldStreamer::heightAt(x, z + step);
    const float ny = 2.0F * step;
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    glNormal3f(nx / length, ny / length, nz / length);
    // Moisture-driven ground tint: lush green in wet basins, dry olive on exposed ground.
    const float moisture = fbm(x * 0.013F + 5.0F, z * 0.013F + 5.0F, 3);
    glColor3f(0.38F + 0.22F * (1.0F - moisture), 0.55F + 0.30F * moisture, 0.30F + 0.08F * moisture);
    glVertex3f(x, y, z);
}
}

void WorldStreamer::update(float playerX, float playerZ) {
    const int centerX = static_cast<int>(std::floor(playerX / chunkSize));
    const int centerZ = static_cast<int>(std::floor(playerZ / chunkSize));
    visible_.clear(); visible_.reserve((radius * 2 + 1) * (radius * 2 + 1));
    int builds = 0;
    for (int dz = -radius; dz <= radius; ++dz) for (int dx = -radius; dx <= radius; ++dx) {
        const int cx = centerX + dx, cz = centerZ + dz;
        const int ring = std::max(std::abs(dx), std::abs(dz));
        const int lod = ring <= 1 ? 0 : ring <= 3 ? 1 : ring <= 7 ? 2 : 3;
        const std::uint64_t key = chunkKey(cx, cz);
        Chunk& chunk = chunks_[key];
        if ((chunk.list == 0 || chunk.lod != lod) && builds < buildBudget) {
            ++builds;
            if (chunk.list) glDeleteLists(chunk.list, 1);
            const int resolution = lod == 0 ? 48 : lod == 1 ? 20 : lod == 2 ? 10 : 5;
            chunk.list = glGenLists(1); chunk.lod = lod;
            const float originX = cx * chunkSize, originZ = cz * chunkSize;
            const float step = chunkSize / resolution;
            glNewList(chunk.list, GL_COMPILE);
            glBegin(GL_TRIANGLES);
            for (int z = 0; z < resolution; ++z) for (int x = 0; x < resolution; ++x) {
                const float x0 = originX + x * step, x1 = x0 + step, z0 = originZ + z * step, z1 = z0 + step;
                emitVertex(x0, z0, step); emitVertex(x1, z0, step); emitVertex(x1, z1, step);
                emitVertex(x0, z0, step); emitVertex(x1, z1, step); emitVertex(x0, z1, step);
            }
            glEnd();
            // Downward skirts hide the hairline cracks where neighbouring LOD rings meet.
            glBegin(GL_QUADS);
            const float drop = 4.0F;
            for (int i = 0; i < resolution; ++i) {
                const float a = i * step, b = a + step;
                const auto skirt = [&](float ax, float az, float bx, float bz) {
                    const float ya = heightAt(ax, az), yb = heightAt(bx, bz);
                    glNormal3f(0.0F, 1.0F, 0.0F); glColor3f(0.35F, 0.33F, 0.26F);
                    glVertex3f(ax, ya, az); glVertex3f(bx, yb, bz); glVertex3f(bx, yb - drop, bz); glVertex3f(ax, ya - drop, az);
                };
                skirt(originX + a, originZ, originX + b, originZ);
                skirt(originX + a, originZ + chunkSize, originX + b, originZ + chunkSize);
                skirt(originX, originZ + a, originX, originZ + b);
                skirt(originX + chunkSize, originZ + a, originX + chunkSize, originZ + b);
            }
            glEnd();
            glEndList();
            // Deterministic environment scatter, only in rings near the player.
            chunk.details.clear();
            if (ring <= detailRadius) {
                std::uint32_t rng = hash2(cx * 7 + 3, cz * 13 - 5) | 1U;
                auto nextUnit = [&rng]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return static_cast<float>(rng & 0xFFFFFFU) / 16777215.0F; };
                for (int attempt = 0; attempt < 52; ++attempt) {
                    const float px = originX + nextUnit() * chunkSize, pz = originZ + nextUnit() * chunkSize;
                    const float py = heightAt(px, pz);
                    const float rise = std::abs(heightAt(px + 2.0F, pz) - heightAt(px - 2.0F, pz)) + std::abs(heightAt(px, pz + 2.0F) - heightAt(px, pz - 2.0F));
                    const float slope = rise / 8.0F;
                    const float landmarkDistance = std::sqrt(px * px + pz * pz);
                    if (landmarkDistance < 44.0F) continue;
                    if (landmarkDistance < 95.0F && nextUnit() < 0.55F) continue;   // open glade around the ruins
                    const float pick = nextUnit();
                    // Forest density follows the moisture field, leaving natural clearings.
                    const float forest = fbm(px * 0.009F + 5.0F, pz * 0.009F + 5.0F, 3);
                    if (slope < 0.42F && py > waterLevel + 1.6F && pick < 0.82F && forest > 0.40F - 0.25F * nextUnit()) {
                        const float snagChance = 0.16F;
                        chunk.details.push_back({px, py - 0.25F, pz, 0.8F + nextUnit() * 0.9F, nextUnit() * 360.0F, nextUnit() < snagChance ? 1 : 0});
                    } else if ((slope >= 0.18F || pick >= 0.94F) && pick >= 0.82F) {
                        chunk.details.push_back({px, py - 0.35F, pz, 0.5F + nextUnit() * 1.3F, nextUnit() * 360.0F, 2});
                    }
                }
            }
        }
        if (chunk.list) visible_.push_back(key);
    }
    // Evict chunks far outside the streaming window so GPU memory stays bounded.
    if (chunks_.size() > 2200) {
        for (auto it = chunks_.begin(); it != chunks_.end();) {
            const int cx = static_cast<int>(static_cast<std::int32_t>(it->first >> 32));
            const int cz = static_cast<int>(static_cast<std::int32_t>(it->first & 0xFFFFFFFFU));
            if (std::max(std::abs(cx - centerX), std::abs(cz - centerZ)) > radius + 2) {
                if (it->second.list) glDeleteLists(it->second.list, 1);
                it = chunks_.erase(it);
            } else ++it;
        }
    }
}

void WorldStreamer::drawTerrain() const {
    for (const std::uint64_t key : visible_) {
        const auto it = chunks_.find(key);
        if (it != chunks_.end() && it->second.list) glCallList(it->second.list);
    }
}

void WorldStreamer::drawDetails(const unsigned int* lists, int listCount) const {
    if (!lists || listCount <= 0) return;
    for (const std::uint64_t key : visible_) {
        const auto it = chunks_.find(key);
        if (it == chunks_.end()) continue;
        for (const DetailInstance& instance : it->second.details) {
            const unsigned int list = lists[instance.type % listCount];
            if (!list) continue;
            glPushMatrix();
            glTranslatef(instance.x, instance.y, instance.z);
            glRotatef(instance.rotation, 0.0F, 1.0F, 0.0F);
            glScalef(instance.scale, instance.scale, instance.scale);
            glCallList(list);
            glPopMatrix();
        }
    }
}
}
