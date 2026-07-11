#include "renderer/GltfPreview.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#include <SDL3/SDL_opengl.h>

#include <cmath>
#include <memory>

namespace aetherwake::renderer {
struct GltfPreview::Impl { tinygltf::Model model; };

bool GltfPreview::load(const std::string& path) {
    impl_ = new Impl{};
    std::string warn, err;
    if (!tinygltf::TinyGLTF{}.LoadBinaryFromFile(&impl_->model, &err, &warn, path)) {
        status_ = "Could not load " + path + ": " + err;
        delete impl_; impl_ = nullptr; return false;
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
static void drawNode(const tinygltf::Model& model, int nodeIndex) {
    const auto& node = model.nodes[nodeIndex]; glPushMatrix();
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
        float r = 0.18F, g = 0.34F, b = 0.29F;
        if (primitive.material >= 0) { const auto& c = model.materials[primitive.material].pbrMetallicRoughness.baseColorFactor; if (c.size() == 4) { r = static_cast<float>(c[0]); g = static_cast<float>(c[1]); b = static_cast<float>(c[2]); } }
        glColor3f(r, g, b); glBegin(GL_TRIANGLES);
        const int count = primitive.indices >= 0 ? static_cast<int>(model.accessors[primitive.indices].count) : static_cast<int>(positions.count);
        for (int i = 0; i < count; ++i) { const unsigned int idx = primitive.indices >= 0 ? indexAt(model, model.accessors[primitive.indices], i) : static_cast<unsigned int>(i); glVertex3fv(vertices + idx * stride); }
        glEnd();
    }
    for (int child : node.children) drawNode(model, child); glPopMatrix();
}
void GltfPreview::draw() const {
    if (!impl_ || impl_->model.scenes.empty()) return;
    const int scene = impl_->model.defaultScene >= 0 ? impl_->model.defaultScene : 0;
    for (int node : impl_->model.scenes[scene].nodes) drawNode(impl_->model, node);
}
}
