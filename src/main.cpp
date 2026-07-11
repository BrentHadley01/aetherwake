#include "game/AbilitySystem.h"
#include "renderer/GltfPreview.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace {
void perspective(float fov, float aspect, float nearPlane, float farPlane) {
    const float top = nearPlane * std::tan(fov * 0.008726646F);
    glFrustum(-top * aspect, top * aspect, -top, top, nearPlane, farPlane);
}
}

int main() {
    using namespace aetherwake;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* window = SDL_CreateWindow("Aetherwake — The Veiled Reach", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    const GLfloat lightPos[] = { -8.0F, 14.0F, 10.0F, 1.0F }; const GLfloat lightColor[] = { 0.45F, 0.75F, 0.90F, 1.0F }; const GLfloat ambient[] = { 0.10F, 0.16F, 0.20F, 1.0F };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos); glLightfv(GL_LIGHT0, GL_DIFFUSE, lightColor); glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    renderer::GltfPreview environment; environment.load("assets/models/veiled_reach-realistic.glb");
    AbilitySystem magic; PlayerState player{1, "Wayfinder", 100, 100, 0, {"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}}; EnemyState warden{101, "Thorn Warden", 120, false}; WorldPropState heart{"hollowmere.observatory_heart", false, ""};
    const std::array<const char*, 4> spells{"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}; int selected = 0; float heroX = 0.0F, heroZ = -8.0F, yaw = 0.0F; bool running = true, won = false; Uint64 previous = SDL_GetTicks();
    while (running) {
        const Uint64 now = SDL_GetTicks(); const float dt = std::min(0.05F, static_cast<float>(now - previous) / 1000.0F); previous = now;
        SDL_Event event; while (SDL_PollEvent(&event)) { if (event.type == SDL_EVENT_QUIT) running = false; if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) { if (event.key.key == SDLK_ESCAPE) running = false; if (event.key.key >= SDLK_1 && event.key.key <= SDLK_4) selected = static_cast<int>(event.key.key - SDLK_1); if (event.key.key == SDLK_SPACE && !won) { const auto cast = magic.cast(player, &warden, &heart, spells[selected]); won = cast.accepted && warden.health == 0; } } }
        const bool* keys = SDL_GetKeyboardState(nullptr); const float speed = 6.0F * dt;
        if (keys[SDL_SCANCODE_W]) heroZ += speed; if (keys[SDL_SCANCODE_S]) heroZ -= speed; if (keys[SDL_SCANCODE_A]) heroX -= speed; if (keys[SDL_SCANCODE_D]) heroX += speed; if (keys[SDL_SCANCODE_Q]) yaw -= 45.0F * dt; if (keys[SDL_SCANCODE_E]) yaw += 45.0F * dt;
        heroX = std::clamp(heroX, -14.0F, 14.0F); heroZ = std::clamp(heroZ, -15.0F, 15.0F);
        char title[220]; std::snprintf(title, sizeof(title), "Aetherwake | %s | Warden %d | WASD move, Q/E camera, 1-4 spell, Space cast | %s", magic.find(spells[selected])->displayName.c_str(), warden.health, environment.status().c_str()); SDL_SetWindowTitle(window, title);
        int width, height; SDL_GetWindowSizeInPixels(window, &width, &height); glViewport(0, 0, width, height); glClearColor(0.006F, 0.016F, 0.025F, 1.0F); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); perspective(58.0F, static_cast<float>(width) / height, 0.1F, 100.0F); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glTranslatef(0.0F, -1.8F, -24.0F); glRotatef(16.0F, 1, 0, 0); glRotatef(yaw, 0, 1, 0); glTranslatef(-heroX, 0.0F, -heroZ);
        environment.draw();
        glDisable(GL_LIGHTING); glPointSize(16.0F); glBegin(GL_POINTS); glColor3f(0.05F, 0.95F, 0.70F); glVertex3f(heroX, 1.2F, heroZ); glColor3f(0.95F, 0.2F, 0.10F); glVertex3f(0.0F, 2.0F, 3.0F); glEnd(); glEnable(GL_LIGHTING);
        SDL_GL_SwapWindow(window);
    }
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
