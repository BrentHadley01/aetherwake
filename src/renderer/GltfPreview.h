#pragma once

#include <string>

namespace aetherwake::renderer {
class GltfPreview {
public:
    bool load(const std::string& path);
    void draw() const;
    [[nodiscard]] const std::string& status() const { return status_; }
private:
    struct Impl;
    Impl* impl_{};
    std::string status_;
};
}
