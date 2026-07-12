#pragma once

#include <string>

namespace aetherwake::renderer {
class ShaderProgram {
public:
    bool load(const std::string& vertexPath, const std::string& fragmentPath);
    void use() const;
    void stop() const;
    [[nodiscard]] bool valid() const { return program_ != 0; }
    [[nodiscard]] const std::string& status() const { return status_; }
private:
    unsigned int program_{};
    std::string status_;
};
}
