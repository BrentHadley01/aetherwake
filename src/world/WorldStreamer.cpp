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
    // Sharp ridge crests confined to the mountain mass for dramatic skylines.
    const float ridgeNoise = fbm(x * 0.0019F - 84.0F, z * 0.0019F + 51.0F, 4);
    const float ridge = std::pow(1.0F - std::abs(2.0F * ridgeNoise - 1.0F), 3.2F) * std::max(0.0F, m - 0.38F) * 92.0F;
    float h = hills * (0.4F + 0.6F * m) + std::pow(m, 2.4F) * 72.0F - 14.0F + detail + ridge;
    // Flatten toward the authored Blender landmark so it sits seamlessly at the origin.
    const float distance = std::sqrt(x * x + z * z);
    const float t = smoothstep01(std::clamp((distance - 38.0F) / 72.0F, 0.0F, 1.0F));
    return -0.15F + (h - -0.15F) * t;
}

WorldStreamer::~WorldStreamer() {
    for (auto& [key, chunk] : chunks_) {
        if (chunk.list) glDeleteLists(chunk.list, 1);
        if (chunk.grassList) glDeleteLists(chunk.grassList, 1);
    }
}

namespace {
void emitVertex(float x, float z, float step) {
    const float y = WorldStreamer::heightAt(x, z);
    const float nx = WorldStreamer::heightAt(x - step, z) - WorldStreamer::heightAt(x + step, z);
    const float nz = WorldStreamer::heightAt(x, z - step) - WorldStreamer::heightAt(x, z + step);
    const float ny = 2.0F * step;
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    glNormal3f(nx / length, ny / length, nz / length);
    // Moisture-driven ground tint: mossy green only in wet basins, warm
    // leaf-litter loam on exposed ground so the floor reads forest, not lawn.
    const float moisture = fbm(x * 0.013F + 5.0F, z * 0.013F + 5.0F, 3);
    glColor3f(0.44F + 0.16F * (1.0F - moisture), 0.40F + 0.24F * moisture, 0.28F + 0.06F * moisture);
    glVertex3f(x, y, z);
}
}

void WorldStreamer::update(float playerX, float playerZ) {
    const int centerX = static_cast<int>(std::floor(playerX / chunkSize));
    const int centerZ = static_cast<int>(std::floor(playerZ / chunkSize));
    centerX_ = centerX; centerZ_ = centerZ;
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
            if (chunk.grassList) { glDeleteLists(chunk.grassList, 1); chunk.grassList = 0; }
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
            // Swaying grass blades on the closest ring; texcoord.x carries the
            // sway weight so the vertex shader can bend the tips.
            if (lod == 0) {
                std::uint32_t grassRng = hash2(cx * 31 + 7, cz * 17 + 3) | 1U;
                auto grassUnit = [&grassRng]() { grassRng ^= grassRng << 13; grassRng ^= grassRng >> 17; grassRng ^= grassRng << 5; return static_cast<float>(grassRng & 0xFFFFFFU) / 16777215.0F; };
                chunk.grassList = glGenLists(1);
                glNewList(chunk.grassList, GL_COMPILE);
                glBegin(GL_TRIANGLES);
                for (int clumpIndex = 0; clumpIndex < 640; ++clumpIndex) {
                    const float clumpX = originX + grassUnit() * chunkSize, clumpZ = originZ + grassUnit() * chunkSize;
                    const float clumpY = heightAt(clumpX, clumpZ);
                    if (clumpY < waterLevel + 0.8F) continue;
                    if (std::sqrt(clumpX * clumpX + clumpZ * clumpZ) < 30.0F) continue;
                    const float rise = std::abs(heightAt(clumpX + 2.0F, clumpZ) - heightAt(clumpX - 2.0F, clumpZ)) + std::abs(heightAt(clumpX, clumpZ + 2.0F) - heightAt(clumpX, clumpZ - 2.0F));
                    if (rise / 8.0F > 0.5F) continue;
                    const float moisture = fbm(clumpX * 0.013F + 5.0F, clumpZ * 0.013F + 5.0F, 3);
                    const int blades = 4 + static_cast<int>(grassUnit() * 3.9F);
                    for (int blade = 0; blade < blades; ++blade) {
                        const float bx = clumpX + (grassUnit() - 0.5F), bz = clumpZ + (grassUnit() - 0.5F);
                        const float by = heightAt(bx, bz);
                        const float angle = grassUnit() * 6.2831853F, lean = 0.06F + grassUnit() * 0.22F;
                        const float tall = 0.30F + grassUnit() * 0.45F;
                        const float sideX = std::cos(angle) * 0.035F, sideZ = std::sin(angle) * 0.035F;
                        const float tipX = bx - std::sin(angle) * lean, tipZ = bz + std::cos(angle) * lean;
                        const float shade = 0.7F + grassUnit() * 0.45F;
                        glNormal3f(0.0F, 1.0F, 0.0F);
                        glColor3f((0.024F + 0.020F * (1.0F - moisture)) * shade, (0.045F + 0.032F * moisture) * shade, 0.020F * shade);
                        glTexCoord2f(0.0F, 0.0F); glVertex3f(bx - sideX, by, bz - sideZ);
                        glTexCoord2f(0.0F, 0.0F); glVertex3f(bx + sideX, by, bz + sideZ);
                        glColor3f((0.040F + 0.026F * (1.0F - moisture)) * shade, (0.075F + 0.040F * moisture) * shade, 0.031F * shade);
                        glTexCoord2f(1.0F, 0.0F); glVertex3f(tipX, by + tall, tipZ);
                    }
                }
                glEnd();
                glEndList();
            }
            // Deterministic environment scatter, only in rings near the player.
            chunk.details.clear();
            if (ring <= detailRadius) {
                std::uint32_t rng = hash2(cx * 7 + 3, cz * 13 - 5) | 1U;
                auto nextUnit = [&rng]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return static_cast<float>(rng & 0xFFFFFFU) / 16777215.0F; };
                // Types: 0 pine, 1 spruce, 2 snag, 3 boulder, 4 fern, 5 log,
                // 6 wildflower, 7 heather, 8 mushrooms, 9 reeds, 10 shrub, 11 meadow grass.
                for (int attempt = 0; attempt < 104; ++attempt) {
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
                    const bool forested = forest > 0.40F - 0.25F * nextUnit();
                    const float moisture = fbm(px * 0.013F + 5.0F, pz * 0.013F + 5.0F, 3);
                    const bool wetMargin = py > waterLevel + 0.10F && py < waterLevel + 3.0F;
                    if (wetMargin && slope < 0.16F && pick < 0.60F) {
                        chunk.details.push_back({px, py - 0.03F, pz, 0.7F + nextUnit() * 0.85F, nextUnit() * 360.0F, 9});
                    } else if (py <= waterLevel + 1.15F) continue;
                    else if (pick < 0.41F && slope < 0.42F && forested) {
                        const float species = nextUnit();
                        const int type = species < 0.55F ? 0 : species < 0.86F ? 1 : 2;
                        chunk.details.push_back({px, py - 0.25F, pz, 0.75F + nextUnit() * 0.8F, nextUnit() * 360.0F, type});
                    } else if (pick < 0.56F && slope < 0.5F && forested) {
                        chunk.details.push_back({px, py - 0.06F, pz, 0.7F + nextUnit() * 0.9F, nextUnit() * 360.0F, 4});
                    } else if (pick < 0.63F && slope < 0.35F && forested && moisture > 0.48F) {
                        chunk.details.push_back({px, py - 0.05F, pz, 0.6F + nextUnit() * 0.8F, nextUnit() * 360.0F, 5});
                    } else if (pick < 0.73F && slope < 0.22F && forested && moisture > 0.54F) {
                        chunk.details.push_back({px, py - 0.02F, pz, 0.55F + nextUnit() * 0.75F, nextUnit() * 360.0F, 8});
                    } else if (pick < 0.78F && slope < 0.26F && !forested) {
                        chunk.details.push_back({px, py - 0.02F, pz, 0.65F + nextUnit() * 1.0F, nextUnit() * 360.0F, 11});
                    } else if (pick < 0.86F && slope < 0.26F && !forested) {
                        chunk.details.push_back({px, py - 0.02F, pz, 0.65F + nextUnit() * 0.95F, nextUnit() * 360.0F, 6});
                    } else if (pick < 0.93F && slope < 0.32F && !forested && moisture < 0.62F) {
                        chunk.details.push_back({px, py - 0.04F, pz, 0.75F + nextUnit() * 1.1F, nextUnit() * 360.0F, 7});
                    } else if (pick < 0.97F && slope < 0.35F && forested) {
                        chunk.details.push_back({px, py - 0.04F, pz, 0.65F + nextUnit() * 0.95F, nextUnit() * 360.0F, 10});
                    } else if (slope >= 0.18F || nextUnit() < 0.35F) {
                        chunk.details.push_back({px, py - 0.35F, pz, 0.5F + nextUnit() * 1.3F, nextUnit() * 360.0F, 3});
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
                if (it->second.grassList) glDeleteLists(it->second.grassList, 1);
                it = chunks_.erase(it);
            } else ++it;
        }
    }
}

void WorldStreamer::drawTerrain(int maxRing) const {
    for (const std::uint64_t key : visible_) {
        if (maxRing < radius) {
            const int cx = static_cast<int>(static_cast<std::int32_t>(key >> 32));
            const int cz = static_cast<int>(static_cast<std::int32_t>(key & 0xFFFFFFFFU));
            if (std::max(std::abs(cx - centerX_), std::abs(cz - centerZ_)) > maxRing) continue;
        }
        const auto it = chunks_.find(key);
        if (it != chunks_.end() && it->second.list) glCallList(it->second.list);
    }
}

void WorldStreamer::drawGrass(int maxRing) const {
    for (const std::uint64_t key : visible_) {
        const int cx = static_cast<int>(static_cast<std::int32_t>(key >> 32));
        const int cz = static_cast<int>(static_cast<std::int32_t>(key & 0xFFFFFFFFU));
        if (std::max(std::abs(cx - centerX_), std::abs(cz - centerZ_)) > maxRing) continue;
        const auto it = chunks_.find(key);
        if (it != chunks_.end() && it->second.grassList) glCallList(it->second.grassList);
    }
}

void WorldStreamer::resolveCollision(float& x, float& z, float radius) const {
    const int centerX = static_cast<int>(std::floor(x / chunkSize));
    const int centerZ = static_cast<int>(std::floor(z / chunkSize));
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
        const auto it = chunks_.find(chunkKey(centerX + dx, centerZ + dz));
        if (it == chunks_.end()) continue;
        for (const DetailInstance& instance : it->second.details) {
            float colliderRadius;
            if (instance.type <= 2) colliderRadius = 0.38F * instance.scale;        // conifer/snag trunks
            else if (instance.type == 3) colliderRadius = 1.30F * instance.scale;   // boulders
            else continue;                                                          // understory is walkable
            const float offsetX = x - instance.x, offsetZ = z - instance.z;
            const float distanceSquared = offsetX * offsetX + offsetZ * offsetZ;
            const float minimum = radius + colliderRadius;
            if (distanceSquared < minimum * minimum && distanceSquared > 1.0e-6F) {
                const float distance = std::sqrt(distanceSquared);
                const float push = minimum - distance;
                x += offsetX / distance * push;
                z += offsetZ / distance * push;
            }
        }
    }
}

void WorldStreamer::drawDetails(const unsigned int* lists, int listCount, float excludeX, float excludeZ, float excludeRadius,
                                float maxDistance, float viewForwardX, float viewForwardZ) const {
    if (!lists || listCount <= 0) return;
    for (const std::uint64_t key : visible_) {
        const auto it = chunks_.find(key);
        if (it == chunks_.end()) continue;
        for (const DetailInstance& instance : it->second.details) {
            const float cameraDx = instance.x - excludeX, cameraDz = instance.z - excludeZ;
            const float distanceSquared = cameraDx * cameraDx + cameraDz * cameraDz;
            if (excludeRadius > 0.0F && distanceSquared < excludeRadius * excludeRadius && instance.type <= 2) continue;
            if (maxDistance > 0.0F) {
                // Small props vanish into fog/grass long before the large tree
                // silhouettes do. This is a visibility budget, not a quality
                // reduction for anything the player can resolve.
                const float typeDistance = instance.type <= 2 ? maxDistance : instance.type <= 5 ? maxDistance * 0.52F : maxDistance * 0.30F;
                if (distanceSquared > typeDistance * typeDistance) continue;
                // The render back-end still clips off-screen geometry, but it
                // has already paid to submit it. Cull the rear hemisphere on
                // the CPU with a generous edge margin to preserve peripheral
                // vision and third-person camera movement.
                if ((viewForwardX != 0.0F || viewForwardZ != 0.0F) && distanceSquared > 34.0F * 34.0F) {
                    const float distance = std::sqrt(distanceSquared);
                    if ((cameraDx * viewForwardX + cameraDz * viewForwardZ) / distance < -0.18F) continue;
                }
            }
            int resolvedType = instance.type;
            // Full branch and needle geometry is only distinguishable nearby.
            // The far assets were authored from the same generator and retain
            // the silhouette, so this is perceptual LOD rather than a visual
            // quality toggle.
            if (instance.type <= 1 && listCount >= 14 && distanceSquared > 125.0F * 125.0F) resolvedType += 12;
            const unsigned int list = lists[resolvedType % listCount];
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
