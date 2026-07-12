#pragma once

#include <string>

namespace aetherwake::renderer {
// Loads a PNG from disk into a mipmapped, repeat-wrapped GL texture. Returns 0 on failure.
unsigned int loadTexture2D(const std::string& path);
}
