#include "renderer/ShaderProgram.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <fstream>
#include <sstream>
#include <vector>

namespace {
using CreateShaderFn = GLuint (APIENTRYP)(GLenum);
using ShaderSourceFn = void (APIENTRYP)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using CompileShaderFn = void (APIENTRYP)(GLuint);
using GetShaderivFn = void (APIENTRYP)(GLuint, GLenum, GLint*);
using GetShaderInfoLogFn = void (APIENTRYP)(GLuint, GLsizei, GLsizei*, GLchar*);
using DeleteShaderFn = void (APIENTRYP)(GLuint);
using CreateProgramFn = GLuint (APIENTRYP)();
using AttachShaderFn = void (APIENTRYP)(GLuint, GLuint);
using LinkProgramFn = void (APIENTRYP)(GLuint);
using GetProgramivFn = void (APIENTRYP)(GLuint, GLenum, GLint*);
using GetProgramInfoLogFn = void (APIENTRYP)(GLuint, GLsizei, GLsizei*, GLchar*);
using UseProgramFn = void (APIENTRYP)(GLuint);
using GetUniformLocationFn = GLint (APIENTRYP)(GLuint, const GLchar*);
using Uniform1iFn = void (APIENTRYP)(GLint, GLint);
using Uniform1fFn = void (APIENTRYP)(GLint, GLfloat);
using Uniform3fFn = void (APIENTRYP)(GLint, GLfloat, GLfloat, GLfloat);

CreateShaderFn createShader{}; ShaderSourceFn shaderSource{}; CompileShaderFn compileShader{}; GetShaderivFn getShaderiv{}; GetShaderInfoLogFn getShaderInfoLog{}; DeleteShaderFn deleteShader{};
CreateProgramFn createProgram{}; AttachShaderFn attachShader{}; LinkProgramFn linkProgram{}; GetProgramivFn getProgramiv{}; GetProgramInfoLogFn getProgramInfoLog{}; UseProgramFn useProgram{};
GetUniformLocationFn getUniformLocation{}; Uniform1iFn uniform1i{}; Uniform1fFn uniform1f{}; Uniform3fFn uniform3f{};

template <typename T> T glProc(const char* name) { return reinterpret_cast<T>(SDL_GL_GetProcAddress(name)); }
std::string readText(const std::string& path) { std::ifstream stream(path); std::ostringstream text; text << stream.rdbuf(); return text.str(); }
GLuint compile(GLenum type, const std::string& source, std::string& error) {
    GLuint shader = createShader(type); const GLchar* raw = source.c_str(); shaderSource(shader, 1, &raw, nullptr); compileShader(shader);
    GLint okay = GL_FALSE; getShaderiv(shader, GL_COMPILE_STATUS, &okay); if (okay) return shader;
    GLint length = 0; getShaderiv(shader, GL_INFO_LOG_LENGTH, &length); std::vector<GLchar> log(static_cast<std::size_t>(length)); getShaderInfoLog(shader, length, nullptr, log.data()); error.assign(log.begin(), log.end()); deleteShader(shader); return 0;
}
}

namespace aetherwake::renderer {
bool ShaderProgram::load(const std::string& vertexPath, const std::string& fragmentPath) {
    createShader = glProc<CreateShaderFn>("glCreateShader"); shaderSource = glProc<ShaderSourceFn>("glShaderSource"); compileShader = glProc<CompileShaderFn>("glCompileShader"); getShaderiv = glProc<GetShaderivFn>("glGetShaderiv"); getShaderInfoLog = glProc<GetShaderInfoLogFn>("glGetShaderInfoLog"); deleteShader = glProc<DeleteShaderFn>("glDeleteShader");
    createProgram = glProc<CreateProgramFn>("glCreateProgram"); attachShader = glProc<AttachShaderFn>("glAttachShader"); linkProgram = glProc<LinkProgramFn>("glLinkProgram"); getProgramiv = glProc<GetProgramivFn>("glGetProgramiv"); getProgramInfoLog = glProc<GetProgramInfoLogFn>("glGetProgramInfoLog"); useProgram = glProc<UseProgramFn>("glUseProgram");
    getUniformLocation = glProc<GetUniformLocationFn>("glGetUniformLocation"); uniform1i = glProc<Uniform1iFn>("glUniform1i"); uniform1f = glProc<Uniform1fFn>("glUniform1f"); uniform3f = glProc<Uniform3fFn>("glUniform3f");
    if (!createShader || !useProgram) { status_ = "OpenGL shader API unavailable"; return false; }
    std::string error; const GLuint vertex = compile(GL_VERTEX_SHADER, readText(vertexPath), error); if (!vertex) { status_ = "Vertex shader: " + error; return false; }
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, readText(fragmentPath), error); if (!fragment) { deleteShader(vertex); status_ = "Fragment shader: " + error; return false; }
    program_ = createProgram(); attachShader(program_, vertex); attachShader(program_, fragment); linkProgram(program_); deleteShader(vertex); deleteShader(fragment);
    GLint okay = GL_FALSE; getProgramiv(program_, GL_LINK_STATUS, &okay); if (!okay) { GLint length = 0; getProgramiv(program_, GL_INFO_LOG_LENGTH, &length); std::vector<GLchar> log(static_cast<std::size_t>(length)); getProgramInfoLog(program_, length, nullptr, log.data()); status_.assign(log.begin(), log.end()); program_ = 0; return false; }
    status_ = "GLSL world shader active"; return true;
}
void ShaderProgram::use() const { if (useProgram) useProgram(program_); }
void ShaderProgram::stop() const { if (useProgram) useProgram(0); }
void ShaderProgram::setInt(const char* name, int value) const { if (program_ && getUniformLocation && uniform1i) uniform1i(getUniformLocation(program_, name), value); }
void ShaderProgram::setFloat(const char* name, float value) const { if (program_ && getUniformLocation && uniform1f) uniform1f(getUniformLocation(program_, name), value); }
void ShaderProgram::setVec3(const char* name, float x, float y, float z) const { if (program_ && getUniformLocation && uniform3f) uniform3f(getUniformLocation(program_, name), x, y, z); }
}
