#include "renderer/GltfPreview.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <cmath>
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace aetherwake::renderer {
using MultiTexCoord3fFn = void(APIENTRYP)(GLenum, GLfloat, GLfloat, GLfloat);
static MultiTexCoord3fFn multiTexCoord3f{};
struct GltfPreview::Impl { tinygltf::Model model; std::vector<GLuint> textures; GLuint whiteTexture{}; };

bool GltfPreview::load(const std::string& path) {
    if (!multiTexCoord3f) multiTexCoord3f = reinterpret_cast<MultiTexCoord3fFn>(SDL_GL_GetProcAddress("glMultiTexCoord3f"));
    impl_ = new Impl{};
    std::string warn, err;
    if (!tinygltf::TinyGLTF{}.LoadBinaryFromFile(&impl_->model, &err, &warn, path)) {
        status_ = "Could not load " + path + ": " + err;
        delete impl_; impl_ = nullptr; return false;
    }
    impl_->textures.resize(impl_->model.images.size(), 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const unsigned char white[] = {255, 255, 255, 255}; glGenTextures(1, &impl_->whiteTexture); glBindTexture(GL_TEXTURE_2D, impl_->whiteTexture); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    for (std::size_t i = 0; i < impl_->model.images.size(); ++i) {
        const auto& image = impl_->model.images[i];
        if (image.image.empty() || image.width <= 0 || image.height <= 0) continue;
        glGenTextures(1, &impl_->textures[i]); glBindTexture(GL_TEXTURE_2D, impl_->textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        const GLenum format = image.component == 4 ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, image.width, image.height, 0, format, GL_UNSIGNED_BYTE, image.image.data());
    }
    status_ = warn.empty() ? "Loaded Blender scene" : "Loaded with warning: " + warn;
    return true;
}

static int componentSize(int type) { return type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ? 1 : type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2 : 4; }
static unsigned int indexAt(const tinygltf::Model& model, const tinygltf::Accessor& accessor, int i) {
    const auto& view = model.bufferViews[accessor.bufferView];
    const auto* raw = model.buffers[view.buffer].data.data() + view.byteOffset + accessor.byteOffset + i * componentSize(accessor.componentType);
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) return *raw;
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) return *reinterpret_cast<const unsigned short*>(raw);
    return *reinterpret_cast<const unsigned int*>(raw);
}
static void drawNode(const tinygltf::Model& model, const std::vector<GLuint>& textures, GLuint whiteTexture, int nodeIndex) {
    const auto& node = model.nodes[nodeIndex]; glPushMatrix();
    // Blender volume materials are not polygonal GLB surfaces. The authored fog
    // cube must be skipped here and recreated by the runtime fog pass later.
    if (node.name == "Low rain fog") { glPopMatrix(); return; }
    // The streamed world now owns the ground and forest; the authored flat
    // terrain plane and placeholder trees would clash with it.
    if (node.name == "Hollowmere terrain" || node.name.rfind("Old growth", 0) == 0) { glPopMatrix(); return; }
    if (node.matrix.size() == 16) glMultMatrixd(node.matrix.data());
    else {
        if (node.translation.size() == 3) glTranslated(node.translation[0], node.translation[1], node.translation[2]);
        if (node.rotation.size() == 4) { const double angle = 2.0 * std::acos(node.rotation[3]) * 57.2957795; glRotated(angle, node.rotation[0], node.rotation[1], node.rotation[2]); }
        if (node.scale.size() == 3) glScaled(node.scale[0], node.scale[1], node.scale[2]);
    }
    if (node.mesh >= 0) for (const auto& primitive : model.meshes[node.mesh].primitives) {
        const auto posIt = primitive.attributes.find("POSITION"); if (posIt == primitive.attributes.end()) continue;
        const auto& positions = model.accessors[posIt->second]; const auto& view = model.bufferViews[positions.bufferView];
        const float* vertices = reinterpret_cast<const float*>(model.buffers[view.buffer].data.data() + view.byteOffset + positions.byteOffset);
        const int stride = view.byteStride ? view.byteStride / sizeof(float) : 3;
        const float* normals = nullptr; int normalStride = 3;
        const auto normalIt = primitive.attributes.find("NORMAL");
        if (normalIt != primitive.attributes.end()) { const auto& normalAccessor = model.accessors[normalIt->second]; const auto& normalView = model.bufferViews[normalAccessor.bufferView]; normals = reinterpret_cast<const float*>(model.buffers[normalView.buffer].data.data() + normalView.byteOffset + normalAccessor.byteOffset); normalStride = normalView.byteStride ? normalView.byteStride / sizeof(float) : 3; }
        const float* texcoords = nullptr; int texcoordStride = 2;
        const auto uvIt = primitive.attributes.find("TEXCOORD_0");
        if (uvIt != primitive.attributes.end()) { const auto& uvAccessor = model.accessors[uvIt->second]; const auto& uvView = model.bufferViews[uvAccessor.bufferView]; texcoords = reinterpret_cast<const float*>(model.buffers[uvView.buffer].data.data() + uvView.byteOffset + uvAccessor.byteOffset); texcoordStride = uvView.byteStride ? uvView.byteStride / sizeof(float) : 2; }
        float r = 0.16F, g = 0.22F, b = 0.20F;
        float roughness = 0.82F, metallic = 0.0F, materialClass = 0.0F;
        GLuint texture = 0;
        if (primitive.material >= 0) {
            const auto& material = model.materials[primitive.material]; const auto& c = material.pbrMetallicRoughness.baseColorFactor;
            roughness = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);
            metallic = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
            const int textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
            if (textureIndex >= 0 && textureIndex < static_cast<int>(model.textures.size())) { const int source = model.textures[textureIndex].source; if (source >= 0 && source < static_cast<int>(textures.size())) texture = textures[source]; }
            if (c.size() == 4) { r = static_cast<float>(c[0]); g = static_cast<float>(c[1]); b = static_cast<float>(c[2]); }
            std::string name = material.name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (name.find("basalt") != std::string::npos || name.find("rock") != std::string::npos || name.find("stone") != std::string::npos || name.find("pebble") != std::string::npos) {
                materialClass = 1.0F;
                if (texture) { r *= 0.48F; g *= 0.52F; b *= 0.54F; }
            } else if (name.find("bark") != std::string::npos || name.find("wood") != std::string::npos || name.find("trunk") != std::string::npos || name.find("twig") != std::string::npos) {
                materialClass = 2.0F;
                if (texture) { r *= 0.62F; g *= 0.54F; b *= 0.44F; }
            } else if (name.find("needle") != std::string::npos || name.find("leaf") != std::string::npos || name.find("foliage") != std::string::npos || name.find("fern") != std::string::npos || name.find("moss") != std::string::npos || name.find("grass") != std::string::npos || name.find("stem") != std::string::npos || name.find("petal") != std::string::npos || name.find("bloom") != std::string::npos) {
                materialClass = 3.0F;
            } else if (name.find("wool") != std::string::npos || name.find("cloth") != std::string::npos || name.find("robe") != std::string::npos || name.find("cloak") != std::string::npos || name.find("hood") != std::string::npos || name.find("mantle") != std::string::npos) {
                materialClass = 4.0F;
            } else if (name.find("skin") != std::string::npos || name.find("hand") != std::string::npos) {
                materialClass = 5.0F;
            } else if (name.find("metal") != std::string::npos || name.find("iron") != std::string::npos || name.find("ferrule") != std::string::npos) {
                materialClass = 6.0F;
            } else if (name.find("rune") != std::string::npos || name.find("aether") != std::string::npos || name.find("focus") != std::string::npos || name.find("crystal") != std::string::npos) {
                materialClass = 7.0F;
            }
        }
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, texture && texcoords ? texture : whiteTexture); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        if (multiTexCoord3f) multiTexCoord3f(GL_TEXTURE1, roughness, metallic, materialClass);
        glColor3f(r, g, b); glBegin(GL_TRIANGLES);
        const int count = primitive.indices >= 0 ? static_cast<int>(model.accessors[primitive.indices].count) : static_cast<int>(positions.count);
        for (int i = 0; i < count; ++i) { const unsigned int idx = primitive.indices >= 0 ? indexAt(model, model.accessors[primitive.indices], i) : static_cast<unsigned int>(i); if (normals) glNormal3fv(normals + idx * normalStride); if (texcoords) glTexCoord2fv(texcoords + idx * texcoordStride); glVertex3fv(vertices + idx * stride); }
        glEnd();
    }
    for (int child : node.children) drawNode(model, textures, whiteTexture, child);
    glPopMatrix();
}
void GltfPreview::draw() const {
    if (!impl_ || impl_->model.scenes.empty()) return;
    const int scene = impl_->model.defaultScene >= 0 ? impl_->model.defaultScene : 0;
    for (int node : impl_->model.scenes[scene].nodes) drawNode(impl_->model, impl_->textures, impl_->whiteTexture, node);
}
}
