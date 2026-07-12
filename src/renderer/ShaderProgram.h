#pragma once

#include <string>

namespace aetherwake::renderer {
class ShaderProgram {
public:
    bool load(const std::string& vertexPath, const std::string& fragmentPath);
    void use() const;
    void stop() const;
    void setInt(const char* name, int value) const;
    void setFloat(const char* name, float value) const;
    void setVec3(const char* name, float x, float y, float z) const;
    [[nodiscard]] bool valid() const { return program_ != 0; }
    [[nodiscard]] const std::string& status() const { return status_; }
private:
    unsigned int program_{};
    std::string status_;
};
}
