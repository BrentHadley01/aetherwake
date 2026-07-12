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

void drawFullscreenQuad() {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0F, 0.0F); glVertex2f(-1.0F, -1.0F);
    glTexCoord2f(1.0F, 0.0F); glVertex2f(1.0F, -1.0F);
    glTexCoord2f(1.0F, 1.0F); glVertex2f(1.0F, 1.0F);
    glTexCoord2f(0.0F, 1.0F); glVertex2f(-1.0F, 1.0F);
    glEnd();
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
    const GLuint soilTexture = renderer::loadTexture2D("assets/textures/forest_floor_albedo.png");
    const GLuint rockTexture = renderer::loadTexture2D("assets/textures/granite_lichen_albedo.png");
    if (activeTexture && rockTexture) { activeTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rockTexture); glEnable(GL_TEXTURE_2D); activeTexture(GL_TEXTURE0); }

    // Moonlight shadow map: depth-only FBO sampled with hardware PCF.
    constexpr int shadowSize = 2048;
    GLuint shadowTexture = 0, shadowFbo = 0; bool shadowReady = false;
    if (genFramebuffers && bindFramebuffer && framebufferTexture2D && checkFramebufferStatus && activeTexture) {
        glGenTextures(1, &shadowTexture); glBindTexture(GL_TEXTURE_2D, shadowTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize, shadowSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        genFramebuffers(1, &shadowFbo); bindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
        framebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTexture, 0);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        shadowReady = checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        bindFramebuffer(GL_FRAMEBUFFER, 0);
        if (shadowReady) { activeTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, shadowTexture); activeTexture(GL_TEXTURE0); }
    }
    if (worldShader.valid()) { worldShader.use(); worldShader.setInt("uShadow", 2); worldShader.stop(); }

    // Post-processing chain: bloom, vignette, saturation, and debanding.
    renderer::ShaderProgram postShader; postShader.load("assets/shaders/post.vert", "assets/shaders/post.frag");
    if (postShader.valid()) { postShader.use(); postShader.setInt("uScene", 0); postShader.setInt("uBloom", 3); postShader.stop(); }
    const bool postCapable = postShader.valid() && genRenderbuffers && bindRenderbuffer && renderbufferStorage && framebufferRenderbuffer && blitFramebuffer && deleteFramebuffers && deleteRenderbuffers;
    PostTargets post{};
    std::printf("[aetherwake] shader: %s | soil tex %u | rock tex %u | shadow map %s | post chain %s\n", worldShader.status().c_str(), soilTexture, rockTexture, shadowReady ? "ready" : "unavailable", postCapable ? "ready" : "unavailable");
    const GLuint skyDomeList = buildSkyDomeList();
    const GLuint celestialList = buildCelestialList();

    // Blender-authored environment details, compiled once into display lists and
    // instanced across the streamed terrain by the world streamer.
    std::array<GLuint, 14> detailLists{};
    const std::array<const char*, 14> detailFiles{"assets/models/detail_pine.glb", "assets/models/detail_spruce.glb", "assets/models/detail_snag.glb", "assets/models/detail_boulder.glb", "assets/models/detail_fern.glb", "assets/models/detail_log.glb", "assets/models/detail_wildflower.glb", "assets/models/detail_heather.glb", "assets/models/detail_mushrooms.glb", "assets/models/detail_reeds.glb", "assets/models/detail_shrub.glb", "assets/models/detail_meadow_grass.glb", "assets/models/detail_pine_lod.glb", "assets/models/detail_spruce_lod.glb"};
    for (std::size_t i = 0; i < detailFiles.size(); ++i) {
        renderer::GltfPreview detail;
        if (!detail.load(detailFiles[i])) continue;
        detailLists[i] = glGenLists(1);
        glNewList(detailLists[i], GL_COMPILE); detail.draw(); glEndList();
    }
    renderer::GltfPreview wayfinderModel;
    GLuint wayfinderList = 0;
    if (wayfinderModel.load("assets/models/detail_wayfinder.glb")) {
        wayfinderList = glGenLists(1);
        glNewList(wayfinderList, GL_COMPILE); wayfinderModel.draw(); glEndList();
    }

    world::WorldStreamer streamedWorld;
    AbilitySystem magic; PlayerState player{1, "Wayfinder", 100, 100, 0, {"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}}; EnemyState warden{101, "Thorn Warden", 120, false}; WorldPropState heart{"hollowmere.observatory_heart", false, ""};
    const std::array<const char*, 4> spells{"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}; int selected = 0;
    float heroX = 0.0F, heroZ = -26.0F, yaw = 0.0F;
    if (const char* spawn = std::getenv("AETHERWAKE_POS")) std::sscanf(spawn, "%f,%f,%f", &heroX, &heroZ, &yaw);
    const char* autoshot = std::getenv("AETHERWAKE_AUTOSHOT");
    const bool autoexit = std::getenv("AETHERWAKE_AUTOEXIT") != nullptr;
    bool running = true, won = false; Uint64 previous = SDL_GetTicks(); int frame = 0; float elapsed = 0.0F; Uint64 steadyStart = 0;

    while (running) {
        const Uint64 now = SDL_GetTicks(); const float dt = std::min(0.05F, static_cast<float>(now - previous) / 1000.0F); previous = now; elapsed += dt; ++frame;
        if (frame == 100) steadyStart = now;   // streaming burst is over; measure real frame rate from here
        SDL_Event event; while (SDL_PollEvent(&event)) { if (event.type == SDL_EVENT_QUIT) running = false; if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) { if (event.key.key == SDLK_ESCAPE) running = false; if (event.key.key >= SDLK_1 && event.key.key <= SDLK_4) selected = static_cast<int>(event.key.key - SDLK_1); if (event.key.key == SDLK_SPACE && !won) { const auto cast = magic.cast(player, &warden, &heart, spells[selected]); won = cast.accepted && warden.health == 0; } } }

        const bool* keys = SDL_GetKeyboardState(nullptr);
        const float speed = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ? 24.0F : 9.0F) * dt;
        const float yawRadians = yaw * 0.017453293F;
        const float forwardX = std::sin(yawRadians), forwardZ = std::cos(yawRadians);
        if (keys[SDL_SCANCODE_W]) { heroX += forwardX * speed; heroZ += forwardZ * speed; }
        if (keys[SDL_SCANCODE_S]) { heroX -= forwardX * speed; heroZ -= forwardZ * speed; }
        if (keys[SDL_SCANCODE_A]) { heroX -= forwardZ * speed; heroZ += forwardX * speed; }
        if (keys[SDL_SCANCODE_D]) { heroX += forwardZ * speed; heroZ -= forwardX * speed; }
        if (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_LEFT]) yaw += 80.0F * dt;
        if (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_RIGHT]) yaw -= 80.0F * dt;
        heroX = std::clamp(heroX, -4000.0F, 4000.0F); heroZ = std::clamp(heroZ, -4000.0F, 4000.0F);
        const float heroY = std::max(world::WorldStreamer::heightAt(heroX, heroZ), world::WorldStreamer::waterLevel - 1.2F);
        streamedWorld.update(heroX, heroZ);

        // Depth-only moonlight pass into the shadow FBO, before the main view.
        // The light and world are static, so refreshing every other frame
        // halves the geometry submitted with no visible lag.
        static float lightMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        static bool shadowActive = false;
        if (shadowReady && worldShader.valid() && (frame % 2 == 1 || !shadowActive)) {
            const float dirLength = std::sqrt(0.35F * 0.35F + 0.72F * 0.72F + 0.48F * 0.48F);
            const float dirX = -0.35F / dirLength, dirY = 0.72F / dirLength, dirZ = 0.48F / dirLength;
            const float snap = 420.0F / shadowSize * 2.0F;   // texel-align to stop shadow swimming
            const float focusX = std::floor(heroX / snap) * snap, focusZ = std::floor(heroZ / snap) * snap;
            const float focusY = world::WorldStreamer::heightAt(focusX, focusZ);
            float lightView[16], lightProj[16];
            buildView(focusX + dirX * 400.0F, focusY + dirY * 400.0F, focusZ + dirZ * 400.0F, focusX, focusY, focusZ, lightView, nullptr);
            buildOrtho(210.0F, 210.0F, 60.0F, 800.0F, lightProj);
            bindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
            glViewport(0, 0, shadowSize, shadowSize); glClear(GL_DEPTH_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION); glLoadMatrixf(lightProj);
            glMatrixMode(GL_MODELVIEW); glLoadMatrixf(lightView);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(2.5F, 6.0F);
            streamedWorld.drawTerrain(4);
            // Beyond this radius shadows are imperceptible in the moon haze;
            // avoiding their depth submission leaves the visible scene intact.
            streamedWorld.drawDetails(detailLists.data(), static_cast<int>(detailLists.size()), heroX, heroZ, 0.0F, 180.0F);
            environment.draw();
            if (wayfinderList) {
                glPushMatrix(); glTranslatef(heroX, heroY, heroZ); glRotatef(180.0F - yaw, 0.0F, 1.0F, 0.0F); glCallList(wayfinderList); glPopMatrix();
            }
            glDisable(GL_POLYGON_OFFSET_FILL);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            bindFramebuffer(GL_FRAMEBUFFER, 0);
            float projTimesView[16]; mul4(lightProj, lightView, projTimesView);
            const float bias[16] = {0.5F, 0, 0, 0, 0, 0.5F, 0, 0, 0, 0, 0.5F, 0, 0.5F, 0.5F, 0.5F, 1};
            mul4(bias, projTimesView, lightMatrix);
            shadowActive = true;
        }

        char title[300]; std::snprintf(title, sizeof(title), "Aetherwake | %s | Warden %d | %d chunks | WASD move, Q/E turn, Shift sprint, Space cast | %s", magic.find(spells[selected])->displayName.c_str(), warden.health, streamedWorld.loadedChunkCount(), worldShader.status().c_str()); SDL_SetWindowTitle(window, title);

        int width, height; SDL_GetWindowSizeInPixels(window, &width, &height); glViewport(0, 0, width, height);
        if (postCapable && (post.width != width || post.height != height)) { destroyPostTargets(post); createPostTargets(post, width, height); }
        if (post.ready) bindFramebuffer(GL_FRAMEBUFFER, post.sceneFbo);
        glClearColor(0.26F, 0.34F, 0.40F, 1.0F); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); perspective(56.0F, static_cast<float>(width) / height, 0.2F, 3000.0F); glMatrixMode(GL_MODELVIEW); glLoadIdentity();

        const float orbit = 20.0F;
        float eyeX = heroX - forwardX * orbit, eyeZ = heroZ - forwardZ * orbit;
        float eyeY = heroY + 8.5F;
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
        else buildView(eyeX, eyeY, eyeZ, heroX + forwardX * 6.0F, heroY + 2.4F, heroZ + forwardZ * 6.0F, viewMatrix, inverseView);
        glMultMatrixf(viewMatrix);

        // Sky first: shader-driven cloud dome plus fixed-function stars/moon,
        // all centered on the camera so translation never reveals the boundary.
        glDepthMask(GL_FALSE); glDisable(GL_DEPTH_TEST);
        glPushMatrix(); glTranslatef(eyeX, eyeY, eyeZ);
        if (worldShader.valid()) {
            worldShader.use();
            worldShader.setInt("uMode", 4);
            worldShader.setFloat("uTime", elapsed);
            worldShader.setVec3("uEye", eyeX, eyeY, eyeZ);
            worldShader.setMat4("uInverseView", inverseView);
            worldShader.setInt("uShadowOn", 0);
            glCallList(skyDomeList);
            worldShader.stop();
        } else glCallList(skyDomeList);
        glDisable(GL_TEXTURE_2D);
        glCallList(celestialList);
        glPopMatrix();
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);

        if (worldShader.valid()) {
            worldShader.use(); worldShader.setFloat("uTime", elapsed);
            worldShader.setVec3("uEye", eyeX, eyeY, eyeZ);
            worldShader.setMat4("uInverseView", inverseView);
            worldShader.setMat4("uLight", lightMatrix);
            worldShader.setInt("uShadowOn", shadowActive ? 1 : 0);
            worldShader.setInt("uMode", 1);
            if (soilTexture) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, soilTexture); }
            streamedWorld.drawTerrain();
            worldShader.setInt("uMode", 3);
            streamedWorld.drawGrass(1);
            worldShader.setInt("uMode", 0);
            streamedWorld.drawDetails(detailLists.data(), static_cast<int>(detailLists.size()), eyeX, eyeZ, 8.0F, 420.0F, forwardX, forwardZ);
            environment.draw();
            if (wayfinderList) {
                glPushMatrix(); glTranslatef(heroX, heroY, heroZ); glRotatef(180.0F - yaw, 0.0F, 1.0F, 0.0F); glCallList(wayfinderList); glPopMatrix();
            }
            // Water last: a camera-following sheet blended over the flooded basins.
            worldShader.setInt("uMode", 2);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBegin(GL_QUADS);
            glNormal3f(0.0F, 1.0F, 0.0F); glColor3f(1.0F, 1.0F, 1.0F);
            const float waterExtent = 2400.0F, waterY = world::WorldStreamer::waterLevel;
            glVertex3f(eyeX - waterExtent, waterY, eyeZ - waterExtent); glVertex3f(eyeX + waterExtent, waterY, eyeZ - waterExtent);
            glVertex3f(eyeX + waterExtent, waterY, eyeZ + waterExtent); glVertex3f(eyeX - waterExtent, waterY, eyeZ + waterExtent);
            glEnd();
            glDisable(GL_BLEND);
            worldShader.stop();
        } else { streamedWorld.drawTerrain(); environment.draw(); }

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
            bindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, width, height);
            postShader.setInt("uPass", 3);
            if (activeTexture) { activeTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, post.blurTex[1]); activeTexture(GL_TEXTURE0); }
            glBindTexture(GL_TEXTURE_2D, post.resolveTex);
            drawFullscreenQuad();
            postShader.stop();
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
        }

        if (autoshot && frame == 150) { saveScreenshot(autoshot, width, height); if (autoexit) running = false; }
        SDL_GL_SwapWindow(window);
    }
    const float seconds = static_cast<float>(SDL_GetTicks()) / 1000.0F;
    if (seconds > 0.5F) std::printf("[aetherwake] %d frames in %.1fs (%.0f fps avg, %.0f fps steady)\n", frame, seconds, frame / seconds,
                                    frame > 100 && SDL_GetTicks() > steadyStart ? (frame - 100) * 1000.0F / (SDL_GetTicks() - steadyStart) : 0.0F);
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
