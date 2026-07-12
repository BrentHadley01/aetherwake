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

#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif

namespace {
using ActiveTextureFn = void (APIENTRYP)(GLenum);
ActiveTextureFn activeTexture{};

void perspective(float fov, float aspect, float nearPlane, float farPlane) {
    const float top = nearPlane * std::tan(fov * 0.008726646F);
    glFrustum(-top * aspect, top * aspect, -top, top, nearPlane, farPlane);
}
void lookAt(float ex, float ey, float ez, float cx, float cy, float cz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez; const float fl = std::sqrt(fx * fx + fy * fy + fz * fz); fx /= fl; fy /= fl; fz /= fl;
    float sx = -fz, sy = 0.0F, sz = fx; const float sl = std::sqrt(sx * sx + sz * sz); sx /= sl; sz /= sl;
    const float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    const GLfloat matrix[] = {sx, ux, -fx, 0, sy, uy, -fy, 0, sz, uz, -fz, 0, 0, 0, 0, 1}; glMultMatrixf(matrix); glTranslatef(-ex, -ey, -ez);
}

// The sky is authored in display-space colors so fogged terrain (which passes
// through the shader's tone map) dissolves into the horizon without a seam.
GLuint buildSkyList() {
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
    SDL_Window* window = SDL_CreateWindow("Aetherwake — The Veiled Reach", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    activeTexture = reinterpret_cast<ActiveTextureFn>(SDL_GL_GetProcAddress("glActiveTexture"));
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glShadeModel(GL_SMOOTH);

    renderer::GltfPreview environment; environment.load("assets/models/veiled_reach-realistic.glb");
    renderer::ShaderProgram worldShader; worldShader.load("assets/shaders/world.vert", "assets/shaders/world.frag");
    if (worldShader.valid()) { worldShader.use(); worldShader.setInt("uAlbedo", 0); worldShader.setInt("uRock", 1); worldShader.stop(); }
    const GLuint soilTexture = renderer::loadTexture2D("assets/textures/forest_floor_albedo.png");
    const GLuint rockTexture = renderer::loadTexture2D("assets/textures/granite_lichen_albedo.png");
    std::printf("[aetherwake] shader: %s | soil tex %u | rock tex %u\n", worldShader.status().c_str(), soilTexture, rockTexture);
    if (activeTexture && rockTexture) { activeTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rockTexture); glEnable(GL_TEXTURE_2D); activeTexture(GL_TEXTURE0); }
    const GLuint skyList = buildSkyList();

    // Blender-authored environment details, compiled once into display lists and
    // instanced across the streamed terrain by the world streamer.
    std::array<GLuint, 3> detailLists{};
    const std::array<const char*, 3> detailFiles{"assets/models/detail_pine.glb", "assets/models/detail_snag.glb", "assets/models/detail_boulder.glb"};
    for (std::size_t i = 0; i < detailFiles.size(); ++i) {
        renderer::GltfPreview detail;
        if (!detail.load(detailFiles[i])) continue;
        detailLists[i] = glGenLists(1);
        glNewList(detailLists[i], GL_COMPILE); detail.draw(); glEndList();
    }

    world::WorldStreamer streamedWorld;
    AbilitySystem magic; PlayerState player{1, "Wayfinder", 100, 100, 0, {"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}}; EnemyState warden{101, "Thorn Warden", 120, false}; WorldPropState heart{"hollowmere.observatory_heart", false, ""};
    const std::array<const char*, 4> spells{"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}; int selected = 0;
    float heroX = 0.0F, heroZ = -26.0F, yaw = 0.0F;
    if (const char* spawn = std::getenv("AETHERWAKE_POS")) std::sscanf(spawn, "%f,%f,%f", &heroX, &heroZ, &yaw);
    const char* autoshot = std::getenv("AETHERWAKE_AUTOSHOT");
    const bool autoexit = std::getenv("AETHERWAKE_AUTOEXIT") != nullptr;
    bool running = true, won = false; Uint64 previous = SDL_GetTicks(); int frame = 0; float elapsed = 0.0F;

    while (running) {
        const Uint64 now = SDL_GetTicks(); const float dt = std::min(0.05F, static_cast<float>(now - previous) / 1000.0F); previous = now; elapsed += dt; ++frame;
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

        char title[300]; std::snprintf(title, sizeof(title), "Aetherwake | %s | Warden %d | %d chunks | WASD move, Q/E turn, Shift sprint, Space cast | %s", magic.find(spells[selected])->displayName.c_str(), warden.health, streamedWorld.loadedChunkCount(), worldShader.status().c_str()); SDL_SetWindowTitle(window, title);

        int width, height; SDL_GetWindowSizeInPixels(window, &width, &height); glViewport(0, 0, width, height);
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
        if (freeCameraActive == 1) { eyeX = freeCamera[0]; eyeY = freeCamera[1]; eyeZ = freeCamera[2]; lookAt(eyeX, eyeY, eyeZ, freeCamera[3], freeCamera[4], freeCamera[5]); heroX = freeCamera[0]; heroZ = freeCamera[2]; }
        else lookAt(eyeX, eyeY, eyeZ, heroX + forwardX * 6.0F, heroY + 2.4F, heroZ + forwardZ * 6.0F);

        // Sky first: drawn around the camera without depth so the world overlays it.
        glDepthMask(GL_FALSE); glDisable(GL_DEPTH_TEST);
        glPushMatrix(); glTranslatef(eyeX, eyeY, eyeZ); glCallList(skyList); glPopMatrix();
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);

        if (worldShader.valid()) {
            worldShader.use(); worldShader.setFloat("uTime", elapsed);
            worldShader.setInt("uMode", 1);
            if (soilTexture) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, soilTexture); }
            streamedWorld.drawTerrain();
            worldShader.setInt("uMode", 0);
            streamedWorld.drawDetails(detailLists.data(), static_cast<int>(detailLists.size()));
            environment.draw();
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

        glPointSize(14.0F); glBegin(GL_POINTS); glColor3f(0.05F, 0.95F, 0.70F); glVertex3f(heroX, heroY + 1.3F, heroZ); glEnd();

        if (autoshot && frame == 150) { saveScreenshot(autoshot, width, height); if (autoexit) running = false; }
        SDL_GL_SwapWindow(window);
    }
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
