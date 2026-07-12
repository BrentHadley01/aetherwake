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
            // Swaying grass carpet; texcoord.x carries the sway weight so the
            // vertex shader can bend the tips. The near ring is a dense carpet,
            // the mid rings a sparser, taller stand that reads at distance.
            if (lod <= 1) {
                std::uint32_t grassRng = hash2(cx * 31 + 7, cz * 17 + 3) | 1U;
                auto grassUnit = [&grassRng]() { grassRng ^= grassRng << 13; grassRng ^= grassRng >> 17; grassRng ^= grassRng << 5; return static_cast<float>(grassRng & 0xFFFFFFU) / 16777215.0F; };
                chunk.grassList = glGenLists(1);
                glNewList(chunk.grassList, GL_COMPILE);
                glBegin(GL_QUADS);
                // Curved near blades use three connected ribbon sections.
                // Mid-distance blades collapse to one tapered quad, retaining
                // the silhouette while keeping the streamed field affordable.
                const int clumps = lod == 0 ? 6400 : 2400;
                const float tallBias = lod == 0 ? 0.0F : 0.22F;
                const float widthScale = lod == 0 ? 0.82F : 1.5F;
                for (int clumpIndex = 0; clumpIndex < clumps; ++clumpIndex) {
                    const float clumpX = originX + grassUnit() * chunkSize, clumpZ = originZ + grassUnit() * chunkSize;
                    if (std::sqrt(clumpX * clumpX + clumpZ * clumpZ) < 30.0F) continue;
                    // Cheap noise gates run before any heightfield sampling.
                    const float moisture = fbm(clumpX * 0.013F + 5.0F, clumpZ * 0.013F + 5.0F, 3);
                    const float forest = fbm(clumpX * 0.009F + 5.0F, clumpZ * 0.009F + 5.0F, 3);
                    // Meadows carry the densest carpet, forest floor stays
                    // moderate, dry exposed ground goes patchy.
                    float density = 0.35F + 0.62F * moisture;
                    if (forest < 0.42F) density = std::min(1.0F, density * 1.3F);
                    if (grassUnit() > density) continue;
                    const float clumpY = heightAt(clumpX, clumpZ);
                    if (clumpY < waterLevel + 0.8F) continue;
                    const float rise = std::abs(heightAt(clumpX + 1.6F, clumpZ) - clumpY) + std::abs(heightAt(clumpX, clumpZ + 1.6F) - clumpY);
                    if (rise / 3.2F > 0.55F) continue;                     // cliffs and scree stay bare
                    const int blades = (lod == 0 ? 5 : 3) + static_cast<int>(grassUnit() * (lod == 0 ? 3.8F : 2.4F));
                    for (int blade = 0; blade < blades; ++blade) {
                        const float bx = clumpX + (grassUnit() - 0.5F) * 0.8F, bz = clumpZ + (grassUnit() - 0.5F) * 0.8F;
                        // Blades reuse the clump height (sunk slightly) so the
                        // carpet costs one heightfield sample per clump.
                        const float by = clumpY - 0.04F;
                        const float angle = grassUnit() * 6.2831853F;
                        const float bendX = -std::sin(angle), bendZ = std::cos(angle);
                        const float sideDirectionX = std::cos(angle), sideDirectionZ = std::sin(angle);
                        const float species = grassUnit();
                        const bool seedHead = species < 0.045F;
                        const bool broadBlade = moisture > 0.57F && species > 0.72F;
                        const bool dryBlade = moisture < 0.45F && species > 0.48F;
                        const float lean = (0.08F + grassUnit() * 0.28F) * (broadBlade ? 0.72F : 1.0F);
                        const float tall = seedHead ? 0.88F + grassUnit() * 0.38F :
                                           broadBlade ? 0.38F + grassUnit() * 0.42F + tallBias :
                                           0.32F + grassUnit() * 0.52F + tallBias;
                        const float bladeWidth = (seedHead ? 0.020F : broadBlade ? 0.060F : 0.039F) * widthScale;
                        const float shade = 0.7F + grassUnit() * 0.45F;
                        float rootR = (0.022F + 0.018F * (1.0F - moisture)) * shade;
                        float rootG = (0.050F + 0.038F * moisture) * shade;
                        float rootB = (0.014F + 0.010F * moisture) * shade;
                        float tipR = (0.042F + 0.025F * (1.0F - moisture)) * shade;
                        float tipG = (0.088F + 0.050F * moisture) * shade;
                        float tipB = (0.023F + 0.016F * moisture) * shade;
                        if (dryBlade || seedHead) {
                            const float dryMix = seedHead ? 0.72F : 0.42F;
                            rootR += (0.105F - rootR) * dryMix; rootG += (0.090F - rootG) * dryMix; rootB += (0.030F - rootB) * dryMix;
                            tipR += (0.155F - tipR) * dryMix; tipG += (0.135F - tipG) * dryMix; tipB += (0.048F - tipB) * dryMix;
                        }
                        const float flexibility = seedHead ? 0.58F : broadBlade ? 0.72F : 1.0F;
                        const int sections = lod == 0 ? 3 : 1;
                        // The ribbon normal follows its resting bend plane,
                        // producing a highlight that rolls across the field.
                        const float normalLength = std::sqrt(tall * tall + lean * lean);
                        glNormal3f(-bendX * tall / normalLength, lean / normalLength, -bendZ * tall / normalLength);
                        for (int section = 0; section < sections; ++section) {
                            const float t0 = static_cast<float>(section) / sections;
                            const float t1 = static_cast<float>(section + 1) / sections;
                            const auto emitBladeEdge = [&](float t, float sign) {
                                const float curve = t * t * (0.72F + 0.28F * t);
                                const float centerX = bx + bendX * lean * curve;
                                const float centerZ = bz + bendZ * lean * curve;
                                const float halfWidth = bladeWidth * (1.0F - t * 0.94F);
                                const float colorT = t * t;
                                glColor3f(rootR + (tipR - rootR) * colorT,
                                          rootG + (tipG - rootG) * colorT,
                                          rootB + (tipB - rootB) * colorT);
                                glTexCoord2f(t, flexibility);
                                glVertex3f(centerX + sideDirectionX * halfWidth * sign,
                                           by + tall * t,
                                           centerZ + sideDirectionZ * halfWidth * sign);
                            };
                            emitBladeEdge(t0, -1.0F); emitBladeEdge(t0, 1.0F);
                            emitBladeEdge(t1, 1.0F); emitBladeEdge(t1, -1.0F);
                        }
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
                // 6 wildflower, 7 heather, 8 mushrooms, 9 reeds, 10 shrub,
                // 11 meadow grass, 14 birch (12/13 and 15 are render-only LODs),
                // 16 clover, 17 sedge, 18 dry grass, 19 litter, 20 pebbles,
                // 21 lupine, 22 moss mat, 23 slab, 24 outcrop, 25 stump,
                // 26 branch pile.
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
                    const float moisture = fbm(px * 0.013F + 5.0F, pz * 0.013F + 5.0F, 3);
                    const float biome = fbm(px * 0.0022F - 31.0F, pz * 0.0022F + 17.0F, 4);
                    const bool meadow = biome > 0.47F && biome < 0.58F && slope < 0.28F;
                    const bool birchGrove = biome > 0.62F && moisture > 0.51F;
                    const bool spruceBasin = biome < 0.40F && moisture > 0.57F;
                    const bool rockyUpland = py > 34.0F || slope > 0.31F;
                    const bool forested = !meadow && forest > 0.40F - 0.25F * nextUnit();
                    const bool wetMargin = py > waterLevel + 0.10F && py < waterLevel + 3.0F;
                    if (wetMargin && slope < 0.16F && pick < 0.60F) {
                        chunk.details.push_back({px, py - 0.03F, pz, 0.7F + nextUnit() * 0.85F, nextUnit() * 360.0F, 9});
                    } else if (py <= waterLevel + 1.15F) continue;
                    else if (pick < 0.41F && slope < 0.42F && forested) {
                        const float species = nextUnit();
                        int type;
                        if (birchGrove) type = species < 0.70F ? 14 : species < 0.92F ? 1 : 2;
                        else if (spruceBasin) type = species < 0.72F ? 1 : species < 0.88F ? 14 : 2;
                        else type = species < 0.66F ? 0 : species < 0.92F ? 1 : 2;
                        chunk.details.push_back({px, py - 0.25F, pz, 0.75F + nextUnit() * 0.8F, nextUnit() * 360.0F, type});
                    } else if (pick < 0.56F && slope < 0.5F && forested) {
                        chunk.details.push_back({px, py - 0.06F, pz, 0.7F + nextUnit() * 0.9F, nextUnit() * 360.0F, 4});
                    } else if (pick < 0.63F && slope < 0.35F && forested && moisture > 0.48F) {
                        const int deadfall = nextUnit() < 0.46F ? 26 : 5;
                        chunk.details.push_back({px, py - 0.05F, pz, 0.6F + nextUnit() * 0.8F, nextUnit() * 360.0F, deadfall});
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
                        const float rockPick = nextUnit();
                        const int rockType = rockyUpland && rockPick < 0.34F ? 24 : rockPick < 0.67F ? 23 : 3;
                        chunk.details.push_back({px, py - 0.35F, pz, 0.5F + nextUnit() * 1.3F, nextUnit() * 360.0F, rockType});
                    }
                    if (forested && slope < 0.26F && nextUnit() < 0.022F)
                        chunk.details.push_back({px + nextUnit() * 2.0F - 1.0F, py - 0.03F, pz + nextUnit() * 2.0F - 1.0F, 0.65F + nextUnit() * 0.75F, nextUnit() * 360.0F, 25});
                    // A second, independent ground-layer decision creates
                    // overlapping ecological strata instead of one prop per
                    // sample. Each GLB is already a dense natural cluster.
                    if (slope < 0.34F && nextUnit() < 0.38F) {
                        const float gx = px + (nextUnit() - 0.5F) * 3.2F, gz = pz + (nextUnit() - 0.5F) * 3.2F;
                        const float gy = heightAt(gx, gz);
                        if (gy > waterLevel + 0.18F) {
                            const float layerPick = nextUnit();
                            int groundType = -1;
                            if (gy < waterLevel + 3.2F && moisture > 0.46F) groundType = 17;                  // shoreline sedge
                            else if (forested && moisture > 0.60F && layerPick < 0.34F) groundType = 22;      // moss cushions
                            else if (forested && layerPick < 0.70F) groundType = 19;                           // curled leaves/twigs
                            else if (forested) groundType = moisture > 0.52F ? 16 : 20;                       // clover or stones
                            else if (meadow && moisture > 0.54F && layerPick < 0.32F) groundType = 21;       // lupine pockets
                            else if (moisture > 0.48F) groundType = 16;                                       // clover meadow
                            else if (layerPick < 0.78F) groundType = 18;                                      // dry fescue
                            else groundType = 20;                                                              // pebble scatter
                            if (groundType >= 0) chunk.details.push_back({gx, gy - 0.025F, gz, 0.55F + nextUnit() * 0.90F, nextUnit() * 360.0F, groundType});
                        }
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

void WorldStreamer::drawGrass(int maxRing, float eyeX, float eyeZ, float viewForwardX, float viewForwardZ) const {
    for (const std::uint64_t key : visible_) {
        const int cx = static_cast<int>(static_cast<std::int32_t>(key >> 32));
        const int cz = static_cast<int>(static_cast<std::int32_t>(key & 0xFFFFFFFFU));
        const int ring = std::max(std::abs(cx - centerX_), std::abs(cz - centerZ_));
        if (ring > maxRing) continue;
        // The centre chunk surrounds the camera and is always visible. Cull
        // only whole outer chunks comfortably behind the view direction.
        if (ring > 0 && (viewForwardX != 0.0F || viewForwardZ != 0.0F)) {
            const float dx = (cx + 0.5F) * chunkSize - eyeX;
            const float dz = (cz + 0.5F) * chunkSize - eyeZ;
            const float distance = std::sqrt(dx * dx + dz * dz);
            if (distance > 1.0F && (dx * viewForwardX + dz * viewForwardZ) / distance < -0.10F) continue;
        }
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
            if (instance.type <= 2 || instance.type == 14) colliderRadius = 0.38F * instance.scale; // tree/snag trunks
            else if (instance.type == 3 || instance.type == 23 || instance.type == 24) colliderRadius = 1.30F * instance.scale;
            else if (instance.type == 25) colliderRadius = 0.55F * instance.scale;
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
            const bool tree = instance.type <= 2 || instance.type == 14;
            const bool microCover = instance.type >= 16 && instance.type <= 22;
            const bool largeNatural = instance.type >= 23 && instance.type <= 26;
            const bool shadowPass = viewForwardX == 0.0F && viewForwardZ == 0.0F;
            if (shadowPass && microCover) continue; // micro-cover is sub-pixel in the shadow map
            if (excludeRadius > 0.0F && distanceSquared < excludeRadius * excludeRadius && tree) continue;
            if (maxDistance > 0.0F) {
                // Small props vanish into fog/grass long before the large tree
                // silhouettes do. This is a visibility budget, not a quality
                // reduction for anything the player can resolve.
                const float typeDistance = tree ? maxDistance : (instance.type <= 5 || largeNatural) ? maxDistance * 0.52F : microCover ? maxDistance * 0.07F : maxDistance * 0.30F;
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
            // Individual needles resolve only in the immediate near field.
            // The authored LOD now mirrors the three-arm crown and continuous
            // bough silhouette, so switching at 32 m removes sub-pixel work
            // without collapsing the forest canopy.
            const float lodDistance = shadowPass ? 24.0F : 52.0F;
            const float farLodDistance = shadowPass ? 45.0F : 90.0F;
            const float impostorDistance = shadowPass ? 88.0F : 195.0F;
            if (instance.type <= 1 && listCount >= 31 && distanceSquared > impostorDistance * impostorDistance) resolvedType = 29 + instance.type;
            else if (instance.type <= 1 && listCount >= 29 && distanceSquared > farLodDistance * farLodDistance) resolvedType = 27 + instance.type;
            else if (instance.type <= 1 && listCount >= 14 && distanceSquared > lodDistance * lodDistance) resolvedType += 12;
            if (instance.type == 14 && listCount >= 16 && distanceSquared > lodDistance * lodDistance) resolvedType = 15;
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
