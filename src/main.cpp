#include "game/AbilitySystem.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace {
constexpr float kWorldSize = 18.0F;
constexpr float kArenaPixels = 620.0F;
SDL_FPoint worldToScreen(float x, float y) { return {640.0F + (x / kWorldSize) * kArenaPixels, 360.0F + (y / kWorldSize) * kArenaPixels}; }
void disc(SDL_Renderer* renderer, SDL_FPoint center, float radius, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = static_cast<int>(-radius); y <= static_cast<int>(radius); ++y) {
        const float width = std::sqrt(std::max(0.0F, radius * radius - static_cast<float>(y * y)));
        SDL_RenderLine(renderer, center.x - width, center.y + static_cast<float>(y), center.x + width, center.y + static_cast<float>(y));
    }
}
}

int main() {
    using namespace aetherwake;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;
    SDL_Window* window = SDL_CreateWindow("Aetherwake — The Veiled Reach", 1280, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    AbilitySystem magic;
    PlayerState player{1, "Wayfinder", 100, 100, 0, {"ember_lance", "tidal_bind", "stone_lift", "veil_sight"}};
    EnemyState warden{101, "Thorn Warden", 120, false};
    WorldPropState heart{"hollowmere.observatory_heart", false, ""};
    const std::array<const char*, 4> spellIds{"ember_lance", "tidal_bind", "stone_lift", "veil_sight"};
    int selectedSpell = 0;
    float playerX = 0.0F, playerY = -8.0F, enemyX = 0.0F, enemyY = 4.0F;
    bool running = true, won = false;
    Uint64 lastTick = SDL_GetTicks();
    while (running) {
        const Uint64 now = SDL_GetTicks(); const float dt = std::min(0.05F, static_cast<float>(now - lastTick) / 1000.0F); lastTick = now;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (event.key.key >= SDLK_1 && event.key.key <= SDLK_4) selectedSpell = static_cast<int>(event.key.key - SDLK_1);
                if (event.key.key == SDLK_SPACE && !won) {
                    const float dx = playerX - enemyX, dy = playerY - enemyY;
                    const bool nearWarden = dx * dx + dy * dy < 90.0F;
                    const auto result = magic.cast(player, nearWarden ? &warden : nullptr, nearWarden ? &heart : nullptr, spellIds[selectedSpell]);
                    if (result.accepted && warden.health == 0) won = true;
                }
            }
        }
        const bool* keys = SDL_GetKeyboardState(nullptr); const float speed = 7.5F * dt;
        if (keys[SDL_SCANCODE_W]) playerY += speed;
        if (keys[SDL_SCANCODE_S]) playerY -= speed;
        if (keys[SDL_SCANCODE_A]) playerX -= speed;
        if (keys[SDL_SCANCODE_D]) playerX += speed;
        playerX = std::clamp(playerX, -16.0F, 16.0F); playerY = std::clamp(playerY, -16.0F, 16.0F);
        if (!won && warden.health > 0) { const float dx = playerX - enemyX, dy = playerY - enemyY; const float distance = std::sqrt(dx * dx + dy * dy); if (distance > 2.4F) { enemyX += (dx / distance) * dt * 1.1F; enemyY += (dy / distance) * dt * 1.1F; } }
        char title[220]; std::snprintf(title, sizeof(title), "Aetherwake | HP %d  Mana %d | %s | Warden %d | %s", player.health, player.mana, magic.find(spellIds[selectedSpell])->displayName.c_str(), warden.health, won ? "OBSERVATORY RESTORED — Press Esc" : "WASD move · 1-4 choose spell · Space cast"); SDL_SetWindowTitle(window, title);
        SDL_SetRenderDrawColor(renderer, 5, 15, 24, 255); SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 18, 55, 49, 255); SDL_FRect arena{20.0F, 20.0F, 1240.0F, 680.0F}; SDL_RenderFillRect(renderer, &arena);
        SDL_SetRenderDrawColor(renderer, 36, 92, 78, 255); for (int i = 0; i < 9; ++i) { SDL_RenderLine(renderer, 20, 80 + i * 70, 1260, 80 + i * 70); SDL_RenderLine(renderer, 80 + i * 140, 20, 80 + i * 140, 700); }
        const auto enemy = worldToScreen(enemyX, enemyY); disc(renderer, enemy, 36.0F, warden.exposed ? SDL_Color{0, 235, 184, 255} : SDL_Color{23, 65, 61, 255}); disc(renderer, enemy, 15.0F, SDL_Color{0, 182, 140, 255});
        const auto hero = worldToScreen(playerX, playerY); disc(renderer, hero, 22.0F, SDL_Color{52, 120, 202, 255}); disc(renderer, hero, 9.0F, SDL_Color{157, 237, 221, 255});
        const auto objective = worldToScreen(0, 10); disc(renderer, objective, 18.0F, heart.changed ? SDL_Color{76, 255, 201, 255} : SDL_Color{70, 170, 160, 255});
        SDL_SetRenderDrawColor(renderer, 176, 238, 220, 255); SDL_RenderDebugText(renderer, 30, 30, "THE VEILED REACH | Defeat the Thorn Warden and restore the Observatory Heart"); SDL_RenderDebugText(renderer, 30, 52, "WASD: move | 1-4: choose spell | SPACE: cast near enemy | ESC: quit");
        SDL_RenderPresent(renderer); SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
