#include "renderer/Texture.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stb_image.h>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

namespace aetherwake::renderer {
unsigned int loadTexture2D(const std::string& path) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
    if (!pixels || width <= 0 || height <= 0) return 0;
    using GenerateMipmapFn = void (APIENTRYP)(GLenum);
    const auto generateMipmap = reinterpret_cast<GenerateMipmapFn>(SDL_GL_GetProcAddress("glGenerateMipmap"));
    GLuint texture = 0; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generateMipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0F);
    const GLenum format = channels == 4 ? GL_RGBA : channels == 1 ? GL_LUMINANCE : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
    if (generateMipmap) generateMipmap(GL_TEXTURE_2D);
    stbi_image_free(pixels);
    return texture;
}
}
