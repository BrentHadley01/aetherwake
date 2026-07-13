#include "game/AbilitySystem.h"
#include "renderer/GltfPreview.h"
#include "renderer/ShaderProgram.h"
#include "renderer/Texture.h"
#include "world/WorldStreamer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_R_TO_TEXTURE 0x884E
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_TEXTURE3
#define GL_TEXTURE3 0x84C3
#endif
#ifndef GL_TEXTURE4
#define GL_TEXTURE4 0x84C4
#endif

namespace {
using ActiveTextureFn = void (APIENTRYP)(GLenum);
using GenFramebuffersFn = void (APIENTRYP)(GLsizei, GLuint*);
using BindFramebufferFn = void (APIENTRYP)(GLenum, GLuint);
using FramebufferTexture2DFn = void (APIENTRYP)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFramebufferStatusFn = GLenum (APIENTRYP)(GLenum);
using DeleteFramebuffersFn = void (APIENTRYP)(GLsizei, const GLuint*);
using GenRenderbuffersFn = void (APIENTRYP)(GLsizei, GLuint*);
using BindRenderbufferFn = void (APIENTRYP)(GLenum, GLuint);
using RenderbufferStorageFn = void (APIENTRYP)(GLenum, GLenum, GLsizei, GLsizei);
using RenderbufferStorageMultisampleFn = void (APIENTRYP)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using FramebufferRenderbufferFn = void (APIENTRYP)(GLenum, GLenum, GLenum, GLuint);
using DeleteRenderbuffersFn = void (APIENTRYP)(GLsizei, const GLuint*);
using BlitFramebufferFn = void (APIENTRYP)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
ActiveTextureFn activeTexture{};
GenFramebuffersFn genFramebuffers{};
BindFramebufferFn bindFramebuffer{};
FramebufferTexture2DFn framebufferTexture2D{};
CheckFramebufferStatusFn checkFramebufferStatus{};
DeleteFramebuffersFn deleteFramebuffers{};
GenRenderbuffersFn genRenderbuffers{};
BindRenderbufferFn bindRenderbuffer{};
RenderbufferStorageFn renderbufferStorage{};
RenderbufferStorageMultisampleFn renderbufferStorageMultisample{};
FramebufferRenderbufferFn framebufferRenderbuffer{};
DeleteRenderbuffersFn deleteRenderbuffers{};
BlitFramebufferFn blitFramebuffer{};

void perspective(float fov, float aspect, float nearPlane, float farPlane) {
    const float top = nearPlane * std::tan(fov * 0.008726646F);
    glFrustum(-top * aspect, top * aspect, -top, top, nearPlane, farPlane);
}

// out = a * b, all matrices column-major.
void mul4(const float* a, const float* b, float* out) {
    for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row) {
        float sum = 0.0F;
        for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[column * 4 + k];
        out[column * 4 + row] = sum;
    }
}

// Builds a right-handed view matrix (and optionally its inverse for
// reconstructing world space in shaders).
void buildView(float ex, float ey, float ez, float cx, float cy, float cz, float* view, float* inverse) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez; const float fl = std::sqrt(fx * fx + fy * fy + fz * fz); fx /= fl; fy /= fl; fz /= fl;
    float sx = -fz, sy = 0.0F, sz = fx; const float sl = std::sqrt(sx * sx + sz * sz); sx /= sl; sz /= sl;
    const float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    const float viewMatrix[16] = {sx, ux, -fx, 0, sy, uy, -fy, 0, sz, uz, -fz, 0,
                                  -(sx * ex + sy * ey + sz * ez), -(ux * ex + uy * ey + uz * ez), fx * ex + fy * ey + fz * ez, 1};
    std::copy_n(viewMatrix, 16, view);
    if (inverse) {
        const float inverseMatrix[16] = {sx, sy, sz, 0, ux, uy, uz, 0, -fx, -fy, -fz, 0, ex, ey, ez, 1};
        std::copy_n(inverseMatrix, 16, inverse);
    }
}

void buildOrtho(float halfWidth, float halfHeight, float nearPlane, float farPlane, float* m) {
    std::fill_n(m, 16, 0.0F);
    m[0] = 1.0F / halfWidth; m[5] = 1.0F / halfHeight;
    m[10] = -2.0F / (farPlane - nearPlane); m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane); m[15] = 1.0F;
}

enum class SpellVfxKind : unsigned char { Ember, Tide, Stone, Veil, Terrain };
struct SpellParticle {
    float x{}, y{}, z{}, vx{}, vy{}, vz{};
    float age{}, lifetime{}, size{};
    SpellVfxKind kind{};
};

GLuint buildSpellParticleTexture() {
    constexpr int size = 64;
    std::array<unsigned char, size * size * 4> pixels{};
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const float nx = (static_cast<float>(x) + 0.5F) / size * 2.0F - 1.0F;
        const float ny = (static_cast<float>(y) + 0.5F) / size * 2.0F - 1.0F;
        const float radius = std::sqrt(nx * nx + ny * ny);
        const float core = std::exp(-radius * radius * 7.5F);
        const float halo = std::max(0.0F, 1.0F - radius) * 0.28F;
        const float filament = 0.88F + 0.12F * std::sin(nx * 19.0F + ny * 13.0F);
        const float alpha = std::clamp((core + halo) * filament, 0.0F, 1.0F);
        const std::size_t index = static_cast<std::size_t>(y * size + x) * 4;
        pixels[index] = pixels[index + 1] = pixels[index + 2] = 255;
        pixels[index + 3] = static_cast<unsigned char>(alpha * 255.0F);
    }
    GLuint texture = 0; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return texture;
}

float vfxNoise(int value) { return std::sin(static_cast<float>(value) * 78.233F) * 0.5F + 0.5F; }

void spawnSpellVfx(std::vector<SpellParticle>& particles, int spellIndex, float x, float y, float z, float forwardX, float forwardZ) {
    const SpellVfxKind kind = static_cast<SpellVfxKind>(spellIndex);
    const int count = kind == SpellVfxKind::Ember ? 68 : kind == SpellVfxKind::Tide ? 76 : kind == SpellVfxKind::Stone ? 54 : 88;
    const float sideX = -forwardZ, sideZ = forwardX;
    for (int i = 0; i < count; ++i) {
        const float a = static_cast<float>(i) * 2.39996F + vfxNoise(i + count) * 0.55F;
        const float radial = 0.12F + vfxNoise(i * 3 + count) * 0.85F;
        SpellParticle p{}; p.kind = kind; p.age = 0.0F; p.size = 0.065F + vfxNoise(i * 11) * 0.13F;
        if (kind == SpellVfxKind::Ember) {
            p.x = x + forwardX * 0.7F + sideX * std::cos(a) * 0.18F; p.y = y + 1.48F + std::sin(a) * 0.16F; p.z = z + forwardZ * 0.7F + sideZ * std::cos(a) * 0.18F;
            p.vx = forwardX * (9.0F + vfxNoise(i) * 5.0F) + sideX * std::sin(a) * 1.9F; p.vy = std::sin(a * 1.7F) * 1.4F + 0.5F; p.vz = forwardZ * (9.0F + vfxNoise(i) * 5.0F) + sideZ * std::sin(a) * 1.9F;
            p.size *= 1.35F; p.lifetime = 0.72F + vfxNoise(i * 9) * 0.48F;
        } else if (kind == SpellVfxKind::Tide) {
            p.x = x + std::cos(a) * radial * 1.2F; p.y = y + 0.20F + vfxNoise(i) * 0.55F; p.z = z + std::sin(a) * radial * 1.2F;
            p.vx = -std::sin(a) * (2.2F + radial); p.vy = 0.55F + vfxNoise(i * 2) * 1.1F; p.vz = std::cos(a) * (2.2F + radial); p.lifetime = 1.05F + vfxNoise(i * 7) * 0.65F;
        } else if (kind == SpellVfxKind::Stone) {
            p.x = x + forwardX * 1.3F + std::cos(a) * radial; p.y = y + 0.30F; p.z = z + forwardZ * 1.3F + std::sin(a) * radial;
            p.vx = std::cos(a) * (0.8F + radial); p.vy = 3.8F + vfxNoise(i) * 4.8F; p.vz = std::sin(a) * (0.8F + radial); p.size *= 1.65F; p.lifetime = 0.75F + vfxNoise(i * 3) * 0.55F;
        } else {
            p.x = x + std::cos(a) * radial * 1.7F; p.y = y + 0.55F + vfxNoise(i) * 1.65F; p.z = z + std::sin(a) * radial * 1.7F;
            p.vx = -std::sin(a) * 0.7F; p.vy = 0.35F + vfxNoise(i) * 0.75F; p.vz = std::cos(a) * 0.7F; p.lifetime = 1.35F + vfxNoise(i * 5) * 1.10F;
        }
        particles.push_back(p);
    }
}

void spawnTerrainVfx(std::vector<SpellParticle>& particles, float x, float y, float z, int seed) {
    for (int i = 0; i < 9; ++i) {
        const float angle = (i + seed * 0.37F) * 2.39996F;
        const float radial = 0.25F + vfxNoise(seed * 17 + i * 5) * 1.55F;
        SpellParticle p{}; p.kind = SpellVfxKind::Terrain;
        p.x = x + std::cos(angle) * radial; p.y = y + 0.04F; p.z = z + std::sin(angle) * radial;
        p.vx = -std::sin(angle) * 0.34F; p.vy = 1.2F + vfxNoise(seed + i) * 2.1F; p.vz = std::cos(angle) * 0.34F;
        p.size = 0.07F + vfxNoise(seed * 7 + i) * 0.14F;
        p.lifetime = 0.72F + vfxNoise(seed * 3 + i) * 0.58F;
        particles.push_back(p);
    }
}

void updateSpellVfx(std::vector<SpellParticle>& particles, float dt) {
    for (SpellParticle& p : particles) {
        p.age += dt; p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
        if (p.kind == SpellVfxKind::Ember) { p.vy -= 3.3F * dt; p.vx *= 0.985F; p.vz *= 0.985F; }
        else if (p.kind == SpellVfxKind::Stone) p.vy -= 11.0F * dt;
        else if (p.kind == SpellVfxKind::Veil) { p.vx *= 0.992F; p.vz *= 0.992F; }
        else if (p.kind == SpellVfxKind::Terrain) { p.vy *= std::pow(0.22F, dt); p.vx *= 0.985F; p.vz *= 0.985F; }
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const SpellParticle& p) { return p.age >= p.lifetime; }), particles.end());
}

void drawSpellVfx(const std::vector<SpellParticle>& particles, float forwardX, float forwardZ, GLuint particleTexture) {
    if (particles.empty()) return;
    const float rightX = -forwardZ, rightZ = forwardX;
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, particleTexture); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for (const SpellParticle& p : particles) {
        const float t = p.age / p.lifetime, fade = (1.0F - t) * (1.0F - t);
        float r = 0.3F, g = 0.8F, b = 1.0F;
        if (p.kind == SpellVfxKind::Ember) { r = 1.0F; g = 0.16F + 0.48F * (1.0F - t); b = 0.025F; }
        else if (p.kind == SpellVfxKind::Stone) { r = 0.92F; g = 0.49F; b = 0.14F; }
        else if (p.kind == SpellVfxKind::Veil) { r = 0.22F + 0.30F * t; g = 0.82F; b = 0.92F; }
        else if (p.kind == SpellVfxKind::Terrain) { r = 0.48F; g = 0.30F + 0.18F * (1.0F - t); b = 0.13F; }
        const float s = p.size * (0.7F + fade * 1.4F);
        glColor4f(r, g, b, fade * 0.72F);
        glTexCoord2f(0, 0); glVertex3f(p.x - rightX * s, p.y - s, p.z - rightZ * s);
        glTexCoord2f(1, 0); glVertex3f(p.x + rightX * s, p.y - s, p.z + rightZ * s);
        glTexCoord2f(1, 1); glVertex3f(p.x + rightX * s, p.y + s, p.z + rightZ * s);
        glTexCoord2f(0, 1); glVertex3f(p.x - rightX * s, p.y + s, p.z - rightZ * s);
    }
    glEnd(); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_TEXTURE_2D);
}

// The sky is authored in display-space colors so fogged terrain (which passes
// through the shader's tone map) dissolves into the horizon without a seam.
GLuint buildSkyDomeList() {
    const GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    const float radius = 1500.0F;
    const int stacks = 12, slices = 20;
    const float horizon[3] = {0.26F, 0.34F, 0.40F}, zenith[3] = {0.030F, 0.060F, 0.11F};
    for (int stack = 0; stack < stacks; ++stack) {
        const float e0 = static_cast<float>(stack) / stacks, e1 = static_cast<float>(stack + 1) / stacks;
        const float a0 = e0 * 1.5707963F - 0.12F, a1 = e1 * 1.5707963F - 0.12F;
        glBegin(GL_QUAD_STRIP);
        for (int slice = 0; slice <= slices; ++slice) {
            const float azimuth = static_cast<float>(slice) / slices * 6.2831853F;
            for (const auto& [elevation, blend] : {std::pair{a0, e0}, std::pair{a1, e1}}) {
                const float mixT = std::pow(blend, 0.62F);
                glColor3f(horizon[0] + (zenith[0] - horizon[0]) * mixT, horizon[1] + (zenith[1] - horizon[1]) * mixT, horizon[2] + (zenith[2] - horizon[2]) * mixT);
                glVertex3f(radius * std::cos(elevation) * std::sin(azimuth), radius * std::sin(elevation), radius * std::cos(elevation) * std::cos(azimuth));
            }
        }
        glEnd();
    }
    glEndList();
    return list;
}

GLuint buildCelestialList() {
    const GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    // Star field, kept above the haze band near the horizon.
    std::uint32_t rng = 77773U;
    auto nextUnit = [&rng]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return static_cast<float>(rng & 0xFFFFFFU) / 16777215.0F; };
    glPointSize(1.6F);
    glBegin(GL_POINTS);
    for (int i = 0; i < 420; ++i) {
        const float elevation = 0.14F + nextUnit() * 1.35F, azimuth = nextUnit() * 6.2831853F;
        const float brightness = 0.25F + nextUnit() * 0.7F;
        glColor3f(brightness * 0.85F, brightness * 0.9F, brightness);
        glVertex3f(1400.0F * std::cos(elevation) * std::sin(azimuth), 1400.0F * std::sin(elevation), 1400.0F * std::cos(elevation) * std::cos(azimuth));
    }
    glEnd();
    // Moon disc with a soft halo, aligned with the shader's light direction.
    const float mx = -0.35F, my = 0.72F, mz = 0.48F;
    const float ml = std::sqrt(mx * mx + my * my + mz * mz);
    const float cx = mx / ml * 1300.0F, cy = my / ml * 1300.0F, cz = mz / ml * 1300.0F;
    float ax = -mz, ay = 0.0F, az = mx; const float al = std::sqrt(ax * ax + az * az); ax /= al; az /= al;
    const float bx = ay * mz - az * my, by = az * mx - ax * mz, bz = ax * my - ay * mx;
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (const auto& [discRadius, alpha] : {std::pair{46.0F, 0.95F}, std::pair{110.0F, 0.10F}}) {
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(0.82F, 0.90F, 1.0F, alpha); glVertex3f(cx, cy, cz);
        glColor4f(0.82F, 0.90F, 1.0F, 0.0F);
        for (int i = 0; i <= 24; ++i) {
            const float angle = static_cast<float>(i) / 24.0F * 6.2831853F;
            if (alpha > 0.5F) glColor4f(0.82F, 0.90F, 1.0F, alpha);
            glVertex3f(cx + (ax * std::cos(angle) + bx * std::sin(angle)) * discRadius, cy + (ay * std::cos(angle) + by * std::sin(angle)) * discRadius, cz + (az * std::cos(angle) + bz * std::sin(angle)) * discRadius);
        }
        glEnd();
    }
    glDisable(GL_BLEND);
    glEndList();
    return list;
}

// Off-screen targets for the post chain: a multisampled scene buffer, its
// single-sample resolve texture, and a quarter-res bright/blur chain.
struct PostTargets {
    GLuint sceneFbo{}, sceneColor{}, sceneDepth{};
    GLuint resolveFbo{}, resolveTex{};
    GLuint brightFbo{}, brightTex{};
    GLuint blurFbo[2]{}, blurTex[2]{};
    int width{}, height{};
    bool ready{};
};

GLuint makeColorTexture(int width, int height) {
    GLuint texture = 0; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    return texture;
}

bool attachColorTexture(GLuint fbo, GLuint texture) {
    bindFramebuffer(GL_FRAMEBUFFER, fbo);
    framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    return checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void destroyPostTargets(PostTargets& post) {
    const GLuint fbos[] = {post.sceneFbo, post.resolveFbo, post.brightFbo, post.blurFbo[0], post.blurFbo[1]};
    for (GLuint fbo : fbos) if (fbo && deleteFramebuffers) deleteFramebuffers(1, &fbo);
    const GLuint renderbuffers[] = {post.sceneColor, post.sceneDepth};
    for (GLuint rb : renderbuffers) if (rb && deleteRenderbuffers) deleteRenderbuffers(1, &rb);
    const GLuint textures[] = {post.resolveTex, post.brightTex, post.blurTex[0], post.blurTex[1]};
    for (GLuint texture : textures) if (texture) glDeleteTextures(1, &texture);
    post = PostTargets{};
}

bool createPostTargets(PostTargets& post, int width, int height) {
    post.width = width; post.height = height;
    genRenderbuffers(1, &post.sceneColor); bindRenderbuffer(GL_RENDERBUFFER, post.sceneColor);
    if (renderbufferStorageMultisample) renderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);
    else renderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    genRenderbuffers(1, &post.sceneDepth); bindRenderbuffer(GL_RENDERBUFFER, post.sceneDepth);
    if (renderbufferStorageMultisample) renderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, width, height);
    else renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    genFramebuffers(1, &post.sceneFbo); bindFramebuffer(GL_FRAMEBUFFER, post.sceneFbo);
    framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, post.sceneColor);
    framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, post.sceneDepth);
    bool okay = checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    const int quarterWidth = std::max(1, width / 4), quarterHeight = std::max(1, height / 4);
    post.resolveTex = makeColorTexture(width, height);
    post.brightTex = makeColorTexture(quarterWidth, quarterHeight);
    post.blurTex[0] = makeColorTexture(quarterWidth, quarterHeight);
    post.blurTex[1] = makeColorTexture(quarterWidth, quarterHeight);
    genFramebuffers(1, &post.resolveFbo); okay = attachColorTexture(post.resolveFbo, post.resolveTex) && okay;
    genFramebuffers(1, &post.brightFbo); okay = attachColorTexture(post.brightFbo, post.brightTex) && okay;
    genFramebuffers(1, &post.blurFbo[0]); okay = attachColorTexture(post.blurFbo[0], post.blurTex[0]) && okay;
    genFramebuffers(1, &post.blurFbo[1]); okay = attachColorTexture(post.blurFbo[1], post.blurTex[1]) && okay;
    bindFramebuffer(GL_FRAMEBUFFER, 0);
    post.ready = okay;
    if (!okay) destroyPostTargets(post);
    return post.ready;
}

float smoothstepf(float edge0, float edge1, float x) {
    x = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return x * x * (3.0F - 2.0F * x);
}

// Everything the day/night cycle feeds into shaders and sky rendering.
struct CycleState {
    float lightDir[3], lightColor[3], ambient[3], fogLinear[3], zenithLinear[3];
    float horizonDisplay[3], zenithDisplay[3], cloudDark[3], cloudLit[3];
    float sunDir[3];
    float night{}, sunHeight{};
};

// Matches the shader's tone map so sky colors authored in linear space land
// on the same display values as fogged geometry.
float toDisplay(float linear) { return std::pow(1.0F - std::exp(-linear * 3.2F), 1.0F / 2.2F); }

CycleState computeCycle(float dayTime) {
    CycleState cycle{};
    const float phi = dayTime * 6.2831853F - 1.5707963F;
    float sunX = 0.35F, sunY = std::sin(phi), sunZ = std::cos(phi) * 0.75F;
    const float sunLength = std::sqrt(sunX * sunX + sunY * sunY + sunZ * sunZ);
    sunX /= sunLength; sunY /= sunLength; sunZ /= sunLength;
    cycle.sunDir[0] = sunX; cycle.sunDir[1] = sunY; cycle.sunDir[2] = sunZ;
    cycle.sunHeight = sunY;
    const bool daylight = sunY > 0.0F;
    cycle.lightDir[0] = daylight ? sunX : -sunX; cycle.lightDir[1] = daylight ? sunY : -sunY; cycle.lightDir[2] = daylight ? sunZ : -sunZ;
    const float dayF = smoothstepf(0.04F, 0.30F, sunY);
    const float duskWarmth = daylight ? smoothstepf(0.0F, 0.05F, sunY) * (1.0F - smoothstepf(0.08F, 0.30F, sunY)) : 0.0F;
    cycle.night = smoothstepf(0.06F, -0.04F, sunY);
    const auto blend = [&](float* out, const float night[3], const float day[3]) {
        for (int i = 0; i < 3; ++i) out[i] = night[i] + (day[i] - night[i]) * dayF;
    };
    if (daylight) {
        const float horizonSun[3] = {1.08F, 0.54F, 0.28F}, noonSun[3] = {0.98F, 0.93F, 0.82F};
        const float lift = smoothstepf(0.0F, 0.05F, sunY), warm = smoothstepf(0.04F, 0.32F, sunY);
        for (int i = 0; i < 3; ++i) cycle.lightColor[i] = (horizonSun[i] + (noonSun[i] - horizonSun[i]) * warm) * lift;
    } else {
        const float moon[3] = {0.62F, 0.80F, 0.95F};
        const float lift = smoothstepf(0.0F, 0.07F, -sunY) * 0.95F;
        for (int i = 0; i < 3; ++i) cycle.lightColor[i] = moon[i] * lift;
    }
    const float ambientNight[3] = {0.022F, 0.032F, 0.048F}, ambientDay[3] = {0.085F, 0.115F, 0.16F};
    const float fogNight[3] = {0.016F, 0.030F, 0.045F}, fogDay[3] = {0.10F, 0.16F, 0.24F};
    const float zenithNight[3] = {0.004F, 0.010F, 0.022F}, zenithDay[3] = {0.025F, 0.080F, 0.22F};
    const float cloudDarkNight[3] = {0.115F, 0.14F, 0.175F}, cloudDarkDay[3] = {0.25F, 0.30F, 0.36F};
    const float cloudLitNight[3] = {0.20F, 0.225F, 0.26F}, cloudLitDay[3] = {0.56F, 0.61F, 0.68F};
    blend(cycle.ambient, ambientNight, ambientDay);
    blend(cycle.fogLinear, fogNight, fogDay);
    blend(cycle.zenithLinear, zenithNight, zenithDay);
    blend(cycle.cloudDark, cloudDarkNight, cloudDarkDay);
    blend(cycle.cloudLit, cloudLitNight, cloudLitDay);
    const float warmFog[3] = {0.10F, 0.02F, -0.01F}, warmCloud[3] = {0.22F, 0.03F, -0.05F};
    for (int i = 0; i < 3; ++i) {
        cycle.fogLinear[i] = std::max(0.0F, cycle.fogLinear[i] + warmFog[i] * duskWarmth);
        cycle.cloudDark[i] = std::max(0.0F, cycle.cloudDark[i] + warmCloud[i] * duskWarmth * 0.7F);
        cycle.cloudLit[i] = std::max(0.0F, cycle.cloudLit[i] + warmCloud[i] * duskWarmth);
    }
    for (int i = 0; i < 3; ++i) {
        cycle.horizonDisplay[i] = toDisplay(cycle.fogLinear[i]);
        cycle.zenithDisplay[i] = toDisplay(cycle.zenithLinear[i]);
    }
    return cycle;
}

// Rotates the current matrix so geometry baked toward the reference moon
// direction points along the requested direction instead.
void rotateFromBakedDirection(float targetX, float targetY, float targetZ) {
    const float bakedLength = std::sqrt(0.35F * 0.35F + 0.72F * 0.72F + 0.48F * 0.48F);
    const float bakedX = -0.35F / bakedLength, bakedY = 0.72F / bakedLength, bakedZ = 0.48F / bakedLength;
    const float dot = std::clamp(bakedX * targetX + bakedY * targetY + bakedZ * targetZ, -1.0F, 1.0F);
    float axisX = bakedY * targetZ - bakedZ * targetY, axisY = bakedZ * targetX - bakedX * targetZ, axisZ = bakedX * targetY - bakedY * targetX;
    const float axisLength = std::sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
    if (axisLength < 1.0e-5F) { if (dot < 0.0F) glRotatef(180.0F, 0.0F, 1.0F, 0.0F); return; }
    glRotatef(std::acos(dot) * 57.29578F, axisX / axisLength, axisY / axisLength, axisZ / axisLength);
}

GLuint buildSunList() {
    const GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    const float ml = std::sqrt(0.35F * 0.35F + 0.72F * 0.72F + 0.48F * 0.48F);
    const float cx = -0.35F / ml * 1300.0F, cy = 0.72F / ml * 1300.0F, cz = 0.48F / ml * 1300.0F;
    float ax = -0.48F / ml, ay = 0.0F, az = -0.35F / ml; const float al = std::sqrt(ax * ax + az * az); ax /= al; az /= al;
    const float mx = -0.35F / ml, my = 0.72F / ml, mz = 0.48F / ml;
    const float bx = ay * mz - az * my, by = az * mx - ax * mz, bz = ax * my - ay * mx;
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (const auto& [discRadius, alpha] : {std::pair{58.0F, 0.98F}, std::pair{170.0F, 0.14F}}) {
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(1.0F, 0.86F, 0.62F, alpha); glVertex3f(cx, cy, cz);
        glColor4f(1.0F, 0.72F, 0.42F, 0.0F);
        for (int i = 0; i <= 24; ++i) {
            const float angle = static_cast<float>(i) / 24.0F * 6.2831853F;
            if (alpha > 0.5F) glColor4f(1.0F, 0.86F, 0.62F, alpha);
            glVertex3f(cx + (ax * std::cos(angle) + bx * std::sin(angle)) * discRadius, cy + (ay * std::cos(angle) + by * std::sin(angle)) * discRadius, cz + (az * std::cos(angle) + bz * std::sin(angle)) * discRadius);
        }
        glEnd();
    }
    glDisable(GL_BLEND);
    glEndList();
    return list;
}

void drawFullscreenQuad() {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0F, 0.0F); glVertex2f(-1.0F, -1.0F);
    glTexCoord2f(1.0F, 0.0F); glVertex2f(1.0F, -1.0F);
    glTexCoord2f(1.0F, 1.0F); glVertex2f(1.0F, 1.0F);
    glTexCoord2f(0.0F, 1.0F); glVertex2f(-1.0F, 1.0F);
    glEnd();
}

void hudRect(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    glColor4f(r, g, b, a); glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1); glEnd();
}

void hudRing(float cx, float cy, float radius, float r, float g, float b, float a) {
    glColor4f(r, g, b, a); glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 48; ++i) { const float angle = i / 48.0F * 6.2831853F; glVertex2f(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius); }
    glEnd();
}

void drawHud(int width, int height, int selectedSpell, int health, int mana, bool inventoryOpen,
             bool customizeOpen, int appearance, int bodyType, float heroX, float heroZ, float yaw, bool terrainCasting) {
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    // Health and mana glass bars.
    hudRect(24, height - 76, 278, height - 24, 0.015F, 0.022F, 0.030F, 0.78F);
    hudRect(36, height - 64, 36 + 224 * std::clamp(health / 100.0F, 0.0F, 1.0F), height - 49, 0.58F, 0.055F, 0.045F, 0.92F);
    hudRect(36, height - 43, 36 + 224 * std::clamp(mana / 100.0F, 0.0F, 1.0F), height - 30, 0.035F, 0.42F, 0.72F, 0.92F);

    // Spell belt with an illuminated selected slot.
    const float beltX = width * 0.5F - 122.0F;
    const float spellColors[4][3] = {{1.0F,0.20F,0.04F},{0.08F,0.58F,0.95F},{0.62F,0.35F,0.12F},{0.22F,0.84F,0.90F}};
    for (int i = 0; i < 4; ++i) {
        const float x = beltX + i * 62.0F;
        hudRect(x, height - 72, x + 52, height - 20, 0.012F, 0.018F, 0.026F, 0.82F);
        const float inset = i == selectedSpell ? 8.0F : 14.0F;
        hudRect(x + inset, height - 72 + inset, x + 52 - inset, height - 20 - inset,
                spellColors[i][0], spellColors[i][1], spellColors[i][2], i == selectedSpell ? 0.95F : 0.48F);
    }

    // Rotating minimap with terrain contour hints and player heading.
    const float mapX = width - 112.0F, mapY = 112.0F;
    glPointSize(3.0F); glBegin(GL_POINTS);
    for (int iz = -5; iz <= 5; ++iz) for (int ix = -5; ix <= 5; ++ix) {
        const float sampleX = heroX + ix * 9.0F, sampleZ = heroZ + iz * 9.0F;
        const float h = aetherwake::world::WorldStreamer::heightAt(sampleX, sampleZ);
        const float shade = std::clamp(0.22F + (h + 12.0F) * 0.012F, 0.18F, 0.62F);
        glColor4f(shade * 0.45F, shade * 0.78F, shade * 0.52F, 0.74F);
        glVertex2f(mapX + ix * 9.0F, mapY + iz * 9.0F);
    }
    glEnd(); hudRing(mapX, mapY, 52.0F, 0.46F, 0.76F, 0.82F, 0.86F);
    const float heading = yaw * 0.017453293F;
    glColor4f(0.74F, 0.94F, 1.0F, 1.0F); glBegin(GL_TRIANGLES);
    glVertex2f(mapX + std::sin(heading) * 13.0F, mapY - std::cos(heading) * 13.0F);
    glVertex2f(mapX - 6.0F, mapY + 7.0F); glVertex2f(mapX + 6.0F, mapY + 7.0F); glEnd();

    // Centre aiming reticle; amber while earth-shaping is active.
    const float cr = terrainCasting ? 1.0F : 0.65F, cg = terrainCasting ? 0.48F : 0.86F;
    glColor4f(cr, cg, 0.18F, 0.90F); glBegin(GL_LINES);
    glVertex2f(width * 0.5F - 9, height * 0.5F); glVertex2f(width * 0.5F - 2, height * 0.5F);
    glVertex2f(width * 0.5F + 2, height * 0.5F); glVertex2f(width * 0.5F + 9, height * 0.5F);
    glVertex2f(width * 0.5F, height * 0.5F - 9); glVertex2f(width * 0.5F, height * 0.5F - 2);
    glVertex2f(width * 0.5F, height * 0.5F + 2); glVertex2f(width * 0.5F, height * 0.5F + 9); glEnd();

    if (inventoryOpen) {
        hudRect(width * 0.5F - 238, height * 0.5F - 185, width * 0.5F + 238, height * 0.5F + 185, 0.012F, 0.018F, 0.026F, 0.94F);
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 6; ++col) {
            const float x = width * 0.5F - 202 + col * 68, y = height * 0.5F - 122 + row * 68;
            hudRect(x, y, x + 54, y + 54, 0.08F, 0.105F, 0.12F, 0.92F);
            if (row == 0 && col < 4) hudRect(x + 14, y + 14, x + 40, y + 40, spellColors[col][0], spellColors[col][1], spellColors[col][2], 0.82F);
        }
    }
    if (customizeOpen) {
        const float panelX = 28.0F, panelY = 110.0F;
        hudRect(panelX, panelY, panelX + 220, panelY + 126, 0.015F, 0.022F, 0.030F, 0.92F);
        hudRect(panelX + 18, panelY + 12, panelX + 96, panelY + 38, 0.14F, 0.32F, 0.52F, bodyType == 0 ? 1.0F : 0.35F);
        hudRect(panelX + 106, panelY + 12, panelX + 202, panelY + 38, 0.46F, 0.19F, 0.34F, bodyType == 1 ? 1.0F : 0.35F);
        for (int i = 0; i < 4; ++i) {
            const bool active = i == appearance;
            hudRect(panelX + 18 + i * 48, panelY + 48, panelX + 54 + i * 48, panelY + 92,
                    0.18F + i * 0.12F, 0.24F + (3 - i) * 0.08F, 0.34F + i * 0.06F, active ? 1.0F : 0.48F);
        }
    }

    glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
}

void saveScreenshot(const char* path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::vector<unsigned char> flipped(pixels.size());
    for (int row = 0; row < height; ++row)
        std::copy_n(pixels.data() + static_cast<std::size_t>(row) * width * 3, static_cast<std::size_t>(width) * 3, flipped.data() + static_cast<std::size_t>(height - 1 - row) * width * 3);
    stbi_write_png(path, width, height, 3, flipped.data(), width * 3);
}
}

int main() {
    using namespace aetherwake;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1); SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_Window* window = SDL_CreateWindow("Aetherwake — The Veiled Reach", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context && window) {
        // Fall back to a non-multisampled context on GPUs that refuse MSAA.
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0); SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        context = SDL_GL_CreateContext(window);
    }
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    // Capture relative mouse deltas so turning has no screen-edge limit and
    // the pointer does not leave the game while looking around.
    SDL_SetWindowRelativeMouseMode(window, true);
    activeTexture = reinterpret_cast<ActiveTextureFn>(SDL_GL_GetProcAddress("glActiveTexture"));
    genFramebuffers = reinterpret_cast<GenFramebuffersFn>(SDL_GL_GetProcAddress("glGenFramebuffers"));
    bindFramebuffer = reinterpret_cast<BindFramebufferFn>(SDL_GL_GetProcAddress("glBindFramebuffer"));
    framebufferTexture2D = reinterpret_cast<FramebufferTexture2DFn>(SDL_GL_GetProcAddress("glFramebufferTexture2D"));
    checkFramebufferStatus = reinterpret_cast<CheckFramebufferStatusFn>(SDL_GL_GetProcAddress("glCheckFramebufferStatus"));
    deleteFramebuffers = reinterpret_cast<DeleteFramebuffersFn>(SDL_GL_GetProcAddress("glDeleteFramebuffers"));
    genRenderbuffers = reinterpret_cast<GenRenderbuffersFn>(SDL_GL_GetProcAddress("glGenRenderbuffers"));
    bindRenderbuffer = reinterpret_cast<BindRenderbufferFn>(SDL_GL_GetProcAddress("glBindRenderbuffer"));
    renderbufferStorage = reinterpret_cast<RenderbufferStorageFn>(SDL_GL_GetProcAddress("glRenderbufferStorage"));
    renderbufferStorageMultisample = reinterpret_cast<RenderbufferStorageMultisampleFn>(SDL_GL_GetProcAddress("glRenderbufferStorageMultisample"));
    framebufferRenderbuffer = reinterpret_cast<FramebufferRenderbufferFn>(SDL_GL_GetProcAddress("glFramebufferRenderbuffer"));
    deleteRenderbuffers = reinterpret_cast<DeleteRenderbuffersFn>(SDL_GL_GetProcAddress("glDeleteRenderbuffers"));
    blitFramebuffer = reinterpret_cast<BlitFramebufferFn>(SDL_GL_GetProcAddress("glBlitFramebuffer"));
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glShadeModel(GL_SMOOTH);
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
    glEnable(GL_MULTISAMPLE);

    renderer::GltfPreview environment; environment.load("assets/models/veiled_reach-realistic.glb");
    renderer::ShaderProgram worldShader; worldShader.load("assets/shaders/world.vert", "assets/shaders/world.frag");
    if (worldShader.valid()) { worldShader.use(); worldShader.setInt("uAlbedo", 0); worldShader.setInt("uRock", 1); worldShader.stop(); }
    // CC0 photoscans from PolyHaven (forest_ground_04, mossy_rock).
    const GLuint soilTexture = renderer::loadTexture2D("assets/textures/forest_ground_diff.jpg");
    const GLuint rockTexture = renderer::loadTexture2D("assets/textures/mossy_rock_diff.jpg");
    const GLuint spellParticleTexture = buildSpellParticleTexture();
    if (activeTexture && rockTexture) { activeTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rockTexture); glEnable(GL_TEXTURE_2D); activeTexture(GL_TEXTURE0); }

    // Sun/moon shadows use TWO depth maps: the previous and next time-slice
    // of the moving light. Both are static between updates (no edge boil) and
    // the shader cross-fades them continuously, so shadows glide smoothly.
    constexpr int shadowSize = 2048;
    GLuint shadowTextures[2] = {0, 0}, shadowFbos[2] = {0, 0}; bool shadowReady = false;
    if (genFramebuffers && bindFramebuffer && framebufferTexture2D && checkFramebufferStatus && activeTexture) {
        shadowReady = true;
        for (int i = 0; i < 2; ++i) {
            glGenTextures(1, &shadowTextures[i]); glBindTexture(GL_TEXTURE_2D, shadowTextures[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize, shadowSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            genFramebuffers(1, &shadowFbos[i]); bindFramebuffer(GL_FRAMEBUFFER, shadowFbos[i]);
            framebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTextures[i], 0);
            glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
            shadowReady = shadowReady && checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        bindFramebuffer(GL_FRAMEBUFFER, 0);
        if (shadowReady) {
            activeTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, shadowTextures[0]);
            activeTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, shadowTextures[0]);
            activeTexture(GL_TEXTURE0);
        }
    }
    if (worldShader.valid()) { worldShader.use(); worldShader.setInt("uShadow", 2); worldShader.setInt("uShadowOld", 4); worldShader.stop(); }

    // Post-processing chain: bloom, vignette, saturation, and debanding.
    renderer::ShaderProgram postShader; postShader.load("assets/shaders/post.vert", "assets/shaders/post.frag");
    if (postShader.valid()) { postShader.use(); postShader.setInt("uScene", 0); postShader.setInt("uBloom", 3); postShader.stop(); }
    const bool postCapable = postShader.valid() && genRenderbuffers && bindRenderbuffer && renderbufferStorage && framebufferRenderbuffer && blitFramebuffer && deleteFramebuffers && deleteRenderbuffers;
    PostTargets post{};
    std::printf("[aetherwake] shader: %s | soil tex %u | rock tex %u | shadow map %s | post chain %s\n", worldShader.status().c_str(), soilTexture, rockTexture, shadowReady ? "ready" : "unavailable", postCapable ? "ready" : "unavailable");
    const GLuint skyDomeList = buildSkyDomeList();
    const GLuint celestialList = buildCelestialList();
    const GLuint sunList = buildSunList();

    // Blender-authored environment details, compiled once into display lists and
    // instanced across the streamed terrain by the world streamer.
    std::array<GLuint, 39> detailLists{};
    const std::array<const char*, 39> detailFiles{"assets/models/detail_pine.glb", "assets/models/detail_spruce.glb", "assets/models/detail_snag.glb", "assets/models/detail_boulder.glb", "assets/models/detail_fern.glb", "assets/models/detail_log.glb", "assets/models/detail_wildflower.glb", "assets/models/detail_heather.glb", "assets/models/detail_mushrooms.glb", "assets/models/detail_reeds.glb", "assets/models/detail_shrub.glb", "assets/models/detail_meadow_grass.glb", "assets/models/detail_pine_lod.glb", "assets/models/detail_spruce_lod.glb", "assets/models/detail_birch.glb", "assets/models/detail_birch_lod.glb", "assets/models/detail_clover.glb", "assets/models/detail_sedge.glb", "assets/models/detail_dry_grass.glb", "assets/models/detail_forest_litter.glb", "assets/models/detail_pebbles.glb", "assets/models/detail_lupine.glb", "assets/models/detail_moss_mat.glb", "assets/models/detail_rock_slab.glb", "assets/models/detail_rock_outcrop.glb", "assets/models/detail_stump.glb", "assets/models/detail_branch_pile.glb", "assets/models/detail_pine_far.glb", "assets/models/detail_spruce_far.glb", "assets/models/detail_pine_impostor.glb", "assets/models/detail_spruce_impostor.glb", "assets/models/detail_pine_old.glb", "assets/models/detail_spruce_old.glb", "assets/models/detail_pine_old_lod.glb", "assets/models/detail_spruce_old_lod.glb", "assets/models/detail_pine_old_far.glb", "assets/models/detail_spruce_old_far.glb", "assets/models/detail_pine_old_impostor.glb", "assets/models/detail_spruce_old_impostor.glb"};
    for (std::size_t i = 0; i < detailFiles.size(); ++i) {
        renderer::GltfPreview detail;
        if (!detail.load(detailFiles[i])) continue;
        detailLists[i] = glGenLists(1);
        glNewList(detailLists[i], GL_COMPILE); detail.draw(); glEndList();
    }
    std::array<renderer::GltfPreview, 2> playerModels;
    std::array<GLuint, 2> playerLists{};
    const std::array<const char*, 2> playerFiles{"assets/models/player_male.glb", "assets/models/player_female.glb"};
    for (int playerIndex = 0; playerIndex < 2; ++playerIndex) {
        if (!playerModels[playerIndex].load(playerFiles[playerIndex])) continue;
        playerLists[playerIndex] = glGenLists(1);
        glNewList(playerLists[playerIndex], GL_COMPILE); playerModels[playerIndex].draw(); glEndList();
    }
    // Keep the authored Wayfinder as a safe fallback if a character asset is missing.
    renderer::GltfPreview fallbackPlayer;
    if ((!playerLists[0] || !playerLists[1]) && fallbackPlayer.load("assets/models/detail_wayfinder.glb")) {
        const GLuint fallbackList = glGenLists(1); glNewList(fallbackList, GL_COMPILE); fallbackPlayer.draw(); glEndList();
        if (!playerLists[0]) playerLists[0] = fallbackList;
        if (!playerLists[1]) playerLists[1] = fallbackList;
    }

    world::WorldStreamer streamedWorld;
    AbilitySystem magic; PlayerState player{1, "Wayfinder", 100, 100, 0, {"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}}; EnemyState warden{101, "Thorn Warden", 120, false}; WorldPropState heart{"hollowmere.observatory_heart", false, ""};
    const std::array<const char*, 4> spells{"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}; int selected = 0;
    std::vector<SpellParticle> spellParticles;
    bool inventoryOpen = std::getenv("AETHERWAKE_INVENTORY") != nullptr;
    bool customizeOpen = std::getenv("AETHERWAKE_CUSTOMIZE") != nullptr;
    bool terrainCasting = std::getenv("AETHERWAKE_TERRAIN_CAST") != nullptr;
    int appearancePreset = std::getenv("AETHERWAKE_APPEARANCE") ? std::clamp(std::atoi(std::getenv("AETHERWAKE_APPEARANCE")), 0, 3) : 0;
    int bodyType = std::getenv("AETHERWAKE_BODY") ? std::clamp(std::atoi(std::getenv("AETHERWAKE_BODY")), 0, 1) : 0;
    float castAnimation = 0.0F;
    float heroX = 0.0F, heroZ = -26.0F, yaw = 0.0F;
    if (const char* spawn = std::getenv("AETHERWAKE_POS")) std::sscanf(spawn, "%f,%f,%f", &heroX, &heroZ, &yaw);
    const char* autoshot = std::getenv("AETHERWAKE_AUTOSHOT");
    const char* autospell = std::getenv("AETHERWAKE_AUTOSPELL");
    const bool autoexit = std::getenv("AETHERWAKE_AUTOEXIT") != nullptr;
    const int autoFrame = std::getenv("AETHERWAKE_AUTOFRAME") ? std::max(30, std::atoi(std::getenv("AETHERWAKE_AUTOFRAME"))) : 150;
    bool running = true, won = false; Uint64 previous = SDL_GetTicks(); int frame = 0; float elapsed = 0.0F; Uint64 steadyStart = 0;
    // 0 = first person at the Wayfinder's eye; larger values form the
    // collision-safe third-person camera boom.
    float cameraDistance = 20.0F;
    float cameraElevation = 18.0F;
    float firstPersonPitch = 0.0F;
    if (const char* distance = std::getenv("AETHERWAKE_CAMERA_DISTANCE")) cameraDistance = std::clamp(static_cast<float>(std::atof(distance)), 0.0F, 30.0F);

    while (running) {
        const Uint64 now = SDL_GetTicks(); const float dt = std::min(0.05F, static_cast<float>(now - previous) / 1000.0F); previous = now; elapsed += dt; ++frame;
        if (frame == 100) steadyStart = now;   // streaming burst is over; measure real frame rate from here
        SDL_Event event; while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_MOUSE_WHEEL && !inventoryOpen && !customizeOpen) cameraDistance = std::clamp(cameraDistance - event.wheel.y * 2.2F, 0.0F, 30.0F);
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && !inventoryOpen && !customizeOpen) terrainCasting = true;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && (inventoryOpen || customizeOpen)) {
                int uiWidth = 0, uiHeight = 0; SDL_GetWindowSize(window, &uiWidth, &uiHeight);
                if (inventoryOpen) {
                    const float localX = event.button.x - (uiWidth * 0.5F - 202.0F);
                    const float localY = event.button.y - (uiHeight * 0.5F - 122.0F);
                    const int column = static_cast<int>(localX / 68.0F), row = static_cast<int>(localY / 68.0F);
                    if (row == 0 && column >= 0 && column < 4 && localX >= 0.0F && localY >= 0.0F) selected = column;
                } else {
                    if (event.button.y >= 122.0F && event.button.y <= 148.0F) {
                        if (event.button.x >= 46.0F && event.button.x <= 124.0F) bodyType = 0;
                        else if (event.button.x >= 134.0F && event.button.x <= 230.0F) bodyType = 1;
                    } else {
                        const float localX = event.button.x - 46.0F, localY = event.button.y - 158.0F;
                        const int preset = static_cast<int>(localX / 48.0F);
                        if (preset >= 0 && preset < 4 && localY >= 0.0F && localY <= 44.0F) appearancePreset = preset;
                    }
                }
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) terrainCasting = false;
            if (event.type == SDL_EVENT_MOUSE_MOTION && !inventoryOpen && !customizeOpen) {
                // Mouse-right turns right. Third-person changes the orbit;
                // first-person uses a separate conventional, non-inverted aim.
                yaw -= event.motion.xrel * 0.13F;
                if (cameraDistance < 1.0F) firstPersonPitch = std::clamp(firstPersonPitch - event.motion.yrel * 0.13F, -70.0F, 70.0F);
                else cameraElevation = std::clamp(cameraElevation + event.motion.yrel * 0.13F, -28.0F, 65.0F);
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (event.key.key >= SDLK_1 && event.key.key <= SDLK_4) selected = static_cast<int>(event.key.key - SDLK_1);
                if (event.key.key == SDLK_I) { inventoryOpen = !inventoryOpen; customizeOpen = false; terrainCasting = false; SDL_SetWindowRelativeMouseMode(window, !inventoryOpen); }
                if (event.key.key == SDLK_C) { customizeOpen = !customizeOpen; inventoryOpen = false; terrainCasting = false; SDL_SetWindowRelativeMouseMode(window, !customizeOpen); }
                if (event.key.key == SDLK_V) appearancePreset = (appearancePreset + 1) % 4;
                if (event.key.key == SDLK_B) bodyType = 1 - bodyType;
                if (event.key.key == SDLK_SPACE && !won) { const auto cast = magic.cast(player, &warden, &heart, spells[selected]); if (cast.accepted) { spawnSpellVfx(spellParticles, selected, heroX, world::WorldStreamer::heightAt(heroX, heroZ), heroZ, std::sin(yaw * 0.017453293F), std::cos(yaw * 0.017453293F)); castAnimation = 1.0F; } won = cast.accepted && warden.health == 0; }
            }
        }

        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool controlsLocked = inventoryOpen || customizeOpen;
        const float speed = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ? 24.0F : 9.0F) * dt;
        const float yawRadians = yaw * 0.017453293F;
        const float forwardX = std::sin(yawRadians), forwardZ = std::cos(yawRadians);
        if (!controlsLocked && keys[SDL_SCANCODE_W]) { heroX += forwardX * speed; heroZ += forwardZ * speed; }
        if (!controlsLocked && keys[SDL_SCANCODE_S]) { heroX -= forwardX * speed; heroZ -= forwardZ * speed; }
        // buildView's screen-right axis is (-forwardZ, forwardX). The old
        // mapping used it for A, making lateral movement feel inverted.
        if (!controlsLocked && keys[SDL_SCANCODE_A]) { heroX += forwardZ * speed; heroZ -= forwardX * speed; }
        if (!controlsLocked && keys[SDL_SCANCODE_D]) { heroX -= forwardZ * speed; heroZ += forwardX * speed; }
        heroX = std::clamp(heroX, -4000.0F, 4000.0F); heroZ = std::clamp(heroZ, -4000.0F, 4000.0F);
        streamedWorld.resolveCollision(heroX, heroZ, 0.55F);
        const float heroY = std::max(world::WorldStreamer::heightAt(heroX, heroZ), world::WorldStreamer::waterLevel - 1.2F);
        streamedWorld.update(heroX, heroZ);
        if (autospell && frame == 143) {
            const int captureSpell = std::clamp(std::atoi(autospell) - 1, 0, 3);
            spawnSpellVfx(spellParticles, captureSpell, heroX, heroY, heroZ, forwardX, forwardZ);
            castAnimation = 1.0F;
        }
        updateSpellVfx(spellParticles, dt);
        castAnimation = std::max(0.0F, castAnimation - dt * 1.65F);

        // Procedural locomotion: walk bob and forward lean while moving,
        // a slow breathing bob at rest, all smoothed to avoid pops.
        const bool moving = !controlsLocked && (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_D]);
        const bool sprinting = moving && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
        static float animPhase = 0.0F, bobAmount = 0.0F, lean = 0.0F;
        animPhase += (sprinting ? 3.4F : moving ? 2.3F : 1.0F) * 6.2831853F * dt;
        const float targetBob = sprinting ? 0.15F : moving ? 0.10F : 0.03F;
        const float targetLean = sprinting ? 8.0F : moving ? 4.5F : 0.0F;
        bobAmount += (targetBob - bobAmount) * std::min(1.0F, dt * 6.0F);
        lean += (targetLean - lean) * std::min(1.0F, dt * 6.0F);
        const float bob = std::abs(std::sin(animPhase)) * bobAmount;

        // Day/night cycle: a full day every 12 minutes; hold T to fast-forward,
        // or freeze at a specific time with AETHERWAKE_TIME=0..1 (0 = midnight).
        static float dayTime = -1.0F; static bool dayTimeFixed = false;
        if (dayTime < 0.0F) {
            const char* spec = std::getenv("AETHERWAKE_TIME");
            if (spec) { dayTime = std::clamp(static_cast<float>(std::atof(spec)), 0.0F, 1.0F); dayTimeFixed = true; }
            else dayTime = 0.0F;
        }
        // A 72-minute day (Skyrim's default timescale) keeps sun/shadow drift
        // below the speed the eye tracks; a fast cycle makes every shadow
        // edge visibly slide. AETHERWAKE_DAYSECONDS overrides.
        static float daySeconds = -1.0F;
        if (daySeconds < 0.0F) {
            const char* spec = std::getenv("AETHERWAKE_DAYSECONDS");
            daySeconds = spec ? std::max(60.0F, static_cast<float>(std::atof(spec))) : 4320.0F;
        }
        if (!dayTimeFixed) dayTime += dt / daySeconds;
        // Dev time controls (work even with a fixed start time):
        // T fast-forwards, U rips through a whole day in a few seconds.
        dayTime += (keys[SDL_SCANCODE_T] ? dt / 18.0F : 0.0F) + (keys[SDL_SCANCODE_U] ? dt / 4.0F : 0.0F);
        dayTime -= std::floor(dayTime);
        const CycleState cycle = computeCycle(dayTime);

        // Two immutable sun-angle snapshots are blended continuously. At a
        // slice boundary the old "next" map becomes current and only a new
        // future map is rendered, while its blend weight is still zero.
        static float lightMatrices[2][16] = {
            {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
            {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
        static bool shadowActive = false;
        static int shadowBaseStep = -1, shadowCurrent = 0, shadowNext = 1;
        static float shadowHeroX = 1.0e9F, shadowHeroZ = 1.0e9F;
        const auto renderShadowSnapshot = [&](int mapIndex, int angleStep) {
            const CycleState shadowCycle = computeCycle((static_cast<float>(angleStep) + 0.5F) / 3600.0F);
            float dirX = shadowCycle.lightDir[0], dirY = std::max(shadowCycle.lightDir[1], 0.18F), dirZ = shadowCycle.lightDir[2];
            const float dirLength = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
            dirX /= dirLength; dirY /= dirLength; dirZ /= dirLength;
            const float focusY = world::WorldStreamer::heightAt(heroX, heroZ);
            float lightView[16], lightProj[16];
            buildView(heroX + dirX * 400.0F, focusY + dirY * 400.0F, heroZ + dirZ * 400.0F,
                      heroX, focusY, heroZ, lightView, nullptr);
            const float texelWorld = 2.0F * 140.0F / shadowSize;
            lightView[12] = std::round(lightView[12] / texelWorld) * texelWorld;
            lightView[13] = std::round(lightView[13] / texelWorld) * texelWorld;
            buildOrtho(140.0F, 140.0F, 60.0F, 800.0F, lightProj);
            bindFramebuffer(GL_FRAMEBUFFER, shadowFbos[mapIndex]);
            glViewport(0, 0, shadowSize, shadowSize); glClear(GL_DEPTH_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION); glLoadMatrixf(lightProj);
            glMatrixMode(GL_MODELVIEW); glLoadMatrixf(lightView);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(2.5F, 6.0F);
            streamedWorld.drawTerrain(4);
            // Beyond this radius shadows are imperceptible in the moon haze;
            // avoiding their depth submission leaves the visible scene intact.
            streamedWorld.drawDetails(detailLists.data(), static_cast<int>(detailLists.size()), heroX, heroZ, 0.0F, 150.0F);
            environment.draw();
            if (playerLists[bodyType]) {
                glPushMatrix(); glTranslatef(heroX, heroY + bob, heroZ); glRotatef(90.0F - yaw, 0.0F, 1.0F, 0.0F); glRotatef(lean, 1.0F, 0.0F, 0.0F); glCallList(playerLists[bodyType]); glPopMatrix();
            }
            glDisable(GL_POLYGON_OFFSET_FILL);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            bindFramebuffer(GL_FRAMEBUFFER, 0);
            float projTimesView[16]; mul4(lightProj, lightView, projTimesView);
            const float bias[16] = {0.5F, 0, 0, 0, 0, 0.5F, 0, 0, 0, 0, 0.5F, 0, 0.5F, 0.5F, 0.5F, 1};
            mul4(bias, projTimesView, lightMatrices[mapIndex]);
        };
        const float shadowPhase = dayTime * 3600.0F;
        const int desiredShadowStep = static_cast<int>(std::floor(shadowPhase));
        if (shadowReady && worldShader.valid()) {
            if (!shadowActive || desiredShadowStep < shadowBaseStep || desiredShadowStep > shadowBaseStep + 1) {
                shadowCurrent = 0; shadowNext = 1; shadowBaseStep = desiredShadowStep;
                renderShadowSnapshot(shadowCurrent, shadowBaseStep);
                renderShadowSnapshot(shadowNext, shadowBaseStep + 1);
                shadowActive = true;
                shadowHeroX = heroX; shadowHeroZ = heroZ;
            } else if (desiredShadowStep == shadowBaseStep + 1) {
                shadowCurrent = shadowNext; shadowNext = 1 - shadowCurrent;
                shadowBaseStep = desiredShadowStep;
                renderShadowSnapshot(shadowNext, shadowBaseStep + 1);
            } else {
                const float dx = heroX - shadowHeroX, dz = heroZ - shadowHeroZ;
                if (dx * dx + dz * dz > 0.12F * 0.12F) {
                    // Alternate the expensive refresh. Each snapshot remains
                    // internally consistent and is never sampled with a stale matrix.
                    const int movingMap = (frame & 1) ? shadowCurrent : shadowNext;
                    renderShadowSnapshot(movingMap, shadowBaseStep + (movingMap == shadowNext ? 1 : 0));
                    shadowHeroX = heroX; shadowHeroZ = heroZ;
                }
            }
        }
        const float shadowBlend = std::clamp(shadowPhase - std::floor(shadowPhase), 0.0F, 1.0F);
        if (shadowActive && activeTexture) {
            activeTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, shadowTextures[shadowCurrent]);
            activeTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, shadowTextures[shadowNext]);
            activeTexture(GL_TEXTURE0);
        }

        char title[340]; std::snprintf(title, sizeof(title), "Aetherwake | %s | Warden %d | %d chunks | WASD move, mouse look, wheel camera, Shift sprint, Space cast | %s", magic.find(spells[selected])->displayName.c_str(), warden.health, streamedWorld.loadedChunkCount(), worldShader.status().c_str()); SDL_SetWindowTitle(window, title);

        int width, height; SDL_GetWindowSizeInPixels(window, &width, &height); glViewport(0, 0, width, height);
        if (postCapable && (post.width != width || post.height != height)) { destroyPostTargets(post); createPostTargets(post, width, height); }
        if (post.ready) bindFramebuffer(GL_FRAMEBUFFER, post.sceneFbo);
        glClearColor(cycle.horizonDisplay[0], cycle.horizonDisplay[1], cycle.horizonDisplay[2], 1.0F); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); perspective(56.0F, static_cast<float>(width) / height, 0.2F, 3000.0F); glMatrixMode(GL_MODELVIEW); glLoadIdentity();

        const float orbit = cameraDistance;
        const float elevationRadians = cameraElevation * 0.017453293F;
        const float horizontalOrbit = orbit * std::cos(elevationRadians);
        float eyeX = heroX - forwardX * horizontalOrbit, eyeZ = heroZ - forwardZ * horizontalOrbit;
        float eyeY = heroY + 2.35F + orbit * std::sin(elevationRadians);
        // Keep the whole camera boom above the terrain so ridges never swallow the view.
        for (int sample = 0; sample <= 6; ++sample) {
            const float t = static_cast<float>(sample) / 6.0F;
            const float sx = heroX + (eyeX - heroX) * t, sz = heroZ + (eyeZ - heroZ) * t;
            eyeY = std::max(eyeY, world::WorldStreamer::heightAt(sx, sz) + 2.6F);
        }
        eyeY = std::max(eyeY, world::WorldStreamer::waterLevel + 2.0F);
        static float freeCamera[6] = {0, 0, 0, 0, 0, 0}; static int freeCameraActive = -1;
        if (freeCameraActive < 0) { const char* spec = std::getenv("AETHERWAKE_CAM"); freeCameraActive = spec && std::sscanf(spec, "%f,%f,%f,%f,%f,%f", &freeCamera[0], &freeCamera[1], &freeCamera[2], &freeCamera[3], &freeCamera[4], &freeCamera[5]) == 6 ? 1 : 0; }
        float viewMatrix[16], inverseView[16];
        if (freeCameraActive == 1) { eyeX = freeCamera[0]; eyeY = freeCamera[1]; eyeZ = freeCamera[2]; buildView(eyeX, eyeY, eyeZ, freeCamera[3], freeCamera[4], freeCamera[5], viewMatrix, inverseView); heroX = freeCamera[0]; heroZ = freeCamera[2]; }
        else if (orbit < 1.0F) {
            const float pitchRadians = firstPersonPitch * 0.017453293F;
            const float lookDistance = 6.0F, horizontalLook = std::cos(pitchRadians) * lookDistance;
            buildView(eyeX, eyeY, eyeZ, eyeX + forwardX * horizontalLook, eyeY + std::sin(pitchRadians) * lookDistance, eyeZ + forwardZ * horizontalLook, viewMatrix, inverseView);
        } else buildView(eyeX, eyeY, eyeZ, heroX + forwardX * 6.0F, heroY + 2.4F, heroZ + forwardZ * 6.0F, viewMatrix, inverseView);
        glMultMatrixf(viewMatrix);

        // The reticle is the terrain-magic cursor. Ray-march from the camera
        // into the procedural heightfield and paint persistent smooth brushes
        // while the left button is held; Shift reverses the brush to carve.
        if (terrainCasting && !controlsLocked) {
            float rayX, rayY, rayZ;
            if (orbit < 1.0F) {
                const float pitch = firstPersonPitch * 0.017453293F;
                rayX = forwardX * std::cos(pitch); rayY = std::sin(pitch); rayZ = forwardZ * std::cos(pitch);
            } else {
                rayX = heroX + forwardX * 6.0F - eyeX;
                rayY = heroY + 2.4F - eyeY;
                rayZ = heroZ + forwardZ * 6.0F - eyeZ;
                const float length = std::sqrt(rayX * rayX + rayY * rayY + rayZ * rayZ);
                rayX /= length; rayY /= length; rayZ /= length;
            }
            static float lastTerrainBrush = -10.0F;
            for (float distance = 1.0F; distance < 120.0F; distance += 0.38F) {
                const float tx = eyeX + rayX * distance, ty = eyeY + rayY * distance, tz = eyeZ + rayZ * distance;
                const float ground = world::WorldStreamer::heightAt(tx, tz);
                if (ty <= ground + 0.10F) {
                    if (elapsed - lastTerrainBrush > 0.18F) {
                        const float direction = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) ? -1.0F : 1.0F;
                        streamedWorld.deformTerrain(tx, tz, 8.0F, direction * 0.09F);
                        spawnTerrainVfx(spellParticles, tx, ground, tz, frame);
                        lastTerrainBrush = elapsed;
                    }
                    break;
                }
            }
        }

        // Sky first: shader-driven cloud dome plus fixed-function stars/moon,
        // all centered on the camera so translation never reveals the boundary.
        glDepthMask(GL_FALSE); glDisable(GL_DEPTH_TEST);
        glPushMatrix(); glTranslatef(eyeX, eyeY, eyeZ);
        if (worldShader.valid()) {
            worldShader.use();
            // Cycle uniforms persist in program state for the world pass below.
            worldShader.setVec3("uLightDir", cycle.lightDir[0], cycle.lightDir[1], cycle.lightDir[2]);
            worldShader.setVec3("uLightColor", cycle.lightColor[0], cycle.lightColor[1], cycle.lightColor[2]);
            worldShader.setVec3("uAmbient", cycle.ambient[0], cycle.ambient[1], cycle.ambient[2]);
            worldShader.setVec3("uFog", cycle.fogLinear[0], cycle.fogLinear[1], cycle.fogLinear[2]);
            worldShader.setVec3("uZenithLin", cycle.zenithLinear[0], cycle.zenithLinear[1], cycle.zenithLinear[2]);
            worldShader.setVec3("uHorizonDisp", cycle.horizonDisplay[0], cycle.horizonDisplay[1], cycle.horizonDisplay[2]);
            worldShader.setVec3("uZenithDisp", cycle.zenithDisplay[0], cycle.zenithDisplay[1], cycle.zenithDisplay[2]);
            worldShader.setVec3("uCloudDark", cycle.cloudDark[0], cycle.cloudDark[1], cycle.cloudDark[2]);
            worldShader.setVec3("uCloudLit", cycle.cloudLit[0], cycle.cloudLit[1], cycle.cloudLit[2]);
            worldShader.setFloat("uNight", cycle.night);
            worldShader.setInt("uMode", 4);
            worldShader.setFloat("uTime", elapsed);
            worldShader.setVec3("uEye", eyeX, eyeY, eyeZ);
            worldShader.setMat4("uInverseView", inverseView);
            worldShader.setInt("uShadowOn", 0);
            glCallList(skyDomeList);
            worldShader.stop();
        } else glCallList(skyDomeList);
        glDisable(GL_TEXTURE_2D);
        if (cycle.sunHeight < 0.02F) {
            // Stars and moon track the night side of the celestial arc.
            // Explicit additive state prevents textured world display lists
            // from leaving celestial points black-modulated after movement.
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glPushMatrix(); rotateFromBakedDirection(-cycle.sunDir[0], -cycle.sunDir[1], -cycle.sunDir[2]); glCallList(celestialList); glPopMatrix();
            glDisable(GL_BLEND);
        }
        if (cycle.sunHeight > -0.06F) {
            glPushMatrix(); rotateFromBakedDirection(cycle.sunDir[0], cycle.sunDir[1], cycle.sunDir[2]); glCallList(sunList); glPopMatrix();
        }
        glPopMatrix();
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);

        if (worldShader.valid()) {
            worldShader.use(); worldShader.setFloat("uTime", elapsed);
            worldShader.setVec3("uEye", eyeX, eyeY, eyeZ);
            worldShader.setMat4("uInverseView", inverseView);
            worldShader.setMat4("uLight", lightMatrices[shadowCurrent]);
            worldShader.setMat4("uLightNext", lightMatrices[shadowNext]);
            worldShader.setFloat("uShadowBlend", shadowBlend);
            worldShader.setInt("uShadowOn", shadowActive ? 1 : 0);
            worldShader.setInt("uMode", 1);
            if (soilTexture) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, soilTexture); }
            streamedWorld.drawTerrain();
            worldShader.setInt("uMode", 3);
            worldShader.setVec3("uPlayer", heroX, heroY, heroZ);
            streamedWorld.drawGrass(3, eyeX, eyeZ, forwardX, forwardZ);
            worldShader.setInt("uMode", 0);
            // Third-person keeps a small camera-obstruction bubble. In first
            // person collision already prevents entering trunks, so excluding
            // nearby trees only makes solid colliders appear invisible.
            const float treeCameraExclusion = orbit < 1.0F ? 0.0F : 8.0F;
            streamedWorld.drawDetails(detailLists.data(), static_cast<int>(detailLists.size()), eyeX, eyeZ,
                                      treeCameraExclusion, 420.0F, forwardX, forwardZ);
            environment.draw();
            if (playerLists[bodyType] && cameraDistance > 1.0F) {
                static const float skinPalette[4][3] = {{0.34F,0.19F,0.12F},{0.62F,0.40F,0.27F},{0.82F,0.62F,0.47F},{0.22F,0.12F,0.085F}};
                static const float clothPalette[4][3] = {{0.11F,0.22F,0.34F},{0.28F,0.095F,0.065F},{0.10F,0.28F,0.17F},{0.26F,0.18F,0.36F}};
                worldShader.setInt("uMode", 5);
                worldShader.setVec3("uSkinTint", skinPalette[appearancePreset][0], skinPalette[appearancePreset][1], skinPalette[appearancePreset][2]);
                worldShader.setVec3("uClothTint", clothPalette[appearancePreset][0], clothPalette[appearancePreset][1], clothPalette[appearancePreset][2]);
                worldShader.setVec3("uCharacterAnim", animPhase, sprinting ? 1.35F : moving ? 0.82F : 0.0F, castAnimation);
                glPushMatrix(); glTranslatef(heroX, heroY + bob, heroZ); glRotatef(90.0F - yaw, 0.0F, 1.0F, 0.0F); glRotatef(lean, 1.0F, 0.0F, 0.0F); glCallList(playerLists[bodyType]); glPopMatrix();
                worldShader.setInt("uMode", 0);
            }
            // Water last: a camera-following sheet blended over the flooded basins.
            worldShader.setInt("uMode", 2);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glBegin(GL_QUADS);
            glNormal3f(0.0F, 1.0F, 0.0F); glColor3f(1.0F, 1.0F, 1.0F);
            const float waterExtent = 2400.0F, waterY = world::WorldStreamer::waterLevel;
            glVertex3f(eyeX - waterExtent, waterY, eyeZ - waterExtent); glVertex3f(eyeX + waterExtent, waterY, eyeZ - waterExtent);
            glVertex3f(eyeX + waterExtent, waterY, eyeZ + waterExtent); glVertex3f(eyeX - waterExtent, waterY, eyeZ + waterExtent);
            glEnd();
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            worldShader.stop();
        } else { streamedWorld.drawTerrain(); environment.draw(); }

        // The particles are rendered after the world shader, but before the
        // post chain, so their additive cores naturally feed into bloom.
        drawSpellVfx(spellParticles, forwardX, forwardZ, spellParticleTexture);

        if (post.ready) {
            // Resolve the multisampled scene, run the quarter-res bloom chain,
            // and composite to the backbuffer with vignette and dither.
            bindFramebuffer(GL_READ_FRAMEBUFFER, post.sceneFbo);
            bindFramebuffer(GL_DRAW_FRAMEBUFFER, post.resolveFbo);
            blitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            bindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
            glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            postShader.use();
            postShader.setFloat("uTime", elapsed);
            const int quarterWidth = std::max(1, width / 4), quarterHeight = std::max(1, height / 4);
            postShader.setVec3("uPixel", 1.0F / quarterWidth, 1.0F / quarterHeight, 0.0F);
            glEnable(GL_TEXTURE_2D);
            bindFramebuffer(GL_FRAMEBUFFER, post.brightFbo); glViewport(0, 0, quarterWidth, quarterHeight);
            postShader.setInt("uPass", 0); glBindTexture(GL_TEXTURE_2D, post.resolveTex); drawFullscreenQuad();
            bindFramebuffer(GL_FRAMEBUFFER, post.blurFbo[0]);
            postShader.setInt("uPass", 1); glBindTexture(GL_TEXTURE_2D, post.brightTex); drawFullscreenQuad();
            bindFramebuffer(GL_FRAMEBUFFER, post.blurFbo[1]);
            postShader.setInt("uPass", 2); glBindTexture(GL_TEXTURE_2D, post.blurTex[0]); drawFullscreenQuad();
            // Second iteration widens the halo so small hot spots (the moon)
            // bloom into a smooth glow that cannot pulse frame to frame.
            bindFramebuffer(GL_FRAMEBUFFER, post.blurFbo[0]);
            postShader.setInt("uPass", 1); glBindTexture(GL_TEXTURE_2D, post.blurTex[1]); drawFullscreenQuad();
            bindFramebuffer(GL_FRAMEBUFFER, post.blurFbo[1]);
            postShader.setInt("uPass", 2); glBindTexture(GL_TEXTURE_2D, post.blurTex[0]); drawFullscreenQuad();
            bindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, width, height);
            postShader.setInt("uPass", 3);
            postShader.setVec3("uPixel", 1.0F / width, 1.0F / height, 0.0F);
            if (activeTexture) { activeTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, post.blurTex[1]); activeTexture(GL_TEXTURE0); }
            glBindTexture(GL_TEXTURE_2D, post.resolveTex);
            drawFullscreenQuad();
            postShader.stop();
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
        }

        drawHud(width, height, selected, player.health, player.mana, inventoryOpen, customizeOpen,
                appearancePreset, bodyType, heroX, heroZ, yaw, terrainCasting);

        if (autoshot && frame == autoFrame) { saveScreenshot(autoshot, width, height); if (autoexit) running = false; }
        SDL_GL_SwapWindow(window);
    }
    const float seconds = static_cast<float>(SDL_GetTicks()) / 1000.0F;
    if (seconds > 0.5F) std::printf("[aetherwake] %d frames in %.1fs (%.0f fps avg, %.0f fps steady)\n", frame, seconds, frame / seconds,
                                    frame > 100 && SDL_GetTicks() > steadyStart ? (frame - 100) * 1000.0F / (SDL_GetTicks() - steadyStart) : 0.0F);
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
