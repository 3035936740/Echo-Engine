#include "echo/focus.hpp"
#include "echo/platform_features.hpp"
#include "echo/sampling_shader.hpp"
#include "echo/renderer.hpp"
#include "echo/objects.hpp"
#include "echo/collision.hpp"
#include "echo/resources.hpp"
#include "echo/transition.hpp"
#include "echo/transition_shader.hpp"
#include <GL/gl.h>
#include <GL/glext.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <wincodec.h>
namespace echo {
struct Renderer::Impl {
    HWND window{};
    HDC dc{};
    HGLRC context{};
    int width = 1280, height = 720;
    struct LayerTexture {
        GLuint id{};
        int width{}, height{};
    };
    std::map<std::string, LayerTexture, std::less<>> layerTextures;
    ObjectLibrary objects;
    AnimationLibrary animations;
    std::map<std::string, LayerTexture, std::less<>> animationTextures;
    Resources* resources{};
    ProjectConfig config{};
    GLuint bg{}, sdf{}, fluidProgram{}, lighting{}, transitionProgram{}, customTransition{}, sampler{},
        reduction{}, fxaa{};
    int imageFilter{}, sceneFactor = 1;
    struct Target {
        GLuint texture{}, fbo{};
        int width{}, height{};
    };
    Target scene{}, composite{}, reduced{}, filtered{}, velocity[2]{}, pressure[2]{}, dye[2]{},
        divergence{};
    GLuint dialogueTexture{};
    int dialogueW{}, dialogueH{}, dialogueX{}, dialogueY{};
    bool dialogueVisible{};
    GLuint tileTexture{};
    int tileW{}, tileH{};
    GLuint playerTexture{}, bossTexture{};
    int playerW{}, playerH{}, bossW{}, bossH{};
    int vi{}, pi{}, di{};
    std::uint64_t generation = ~0ull;
    double previousTime{};
#define API(type, name) type name{};
    API(PFNGLCREATESHADERPROC, CreateShader)
    API(PFNGLSHADERSOURCEPROC, ShaderSource)
    API(PFNGLCOMPILESHADERPROC, CompileShader)
    API(PFNGLGETSHADERIVPROC, GetShaderiv)
    API(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)
    API(PFNGLCREATEPROGRAMPROC, CreateProgram)
    API(PFNGLATTACHSHADERPROC, AttachShader)
    API(PFNGLLINKPROGRAMPROC, LinkProgram)
    API(PFNGLGETPROGRAMIVPROC, GetProgramiv)
    API(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)
    API(PFNGLUSEPROGRAMPROC, UseProgram)
    API(PFNGLDELETESHADERPROC, DeleteShader)
    API(PFNGLDELETEPROGRAMPROC, DeleteProgram)
    API(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)
    API(PFNGLUNIFORM4FVPROC, Uniform4fv)
    API(PFNGLUNIFORM1FPROC, Uniform1f)
    API(PFNGLUNIFORM1IPROC, Uniform1i)
    API(PFNGLUNIFORM2FPROC, Uniform2f)
    API(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers)
    API(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)
    API(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D)
    API(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus)
    API(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)
    API(PFNGLACTIVETEXTUREPROC, ActiveTexture)
#undef API
    struct Vertex {
        float x, y;
        std::uint8_t r, g, b, a;
    };
    std::vector<Vertex> vertices;
    void rect(float x, float y, float w, float h, int r, int g, int b, int a = 255) {
        std::uint8_t rr = r, gg = g, bb = b, aa = a;
        Vertex v[6] = {{x, y, rr, gg, bb, aa},         {x + w, y, rr, gg, bb, aa},
                       {x + w, y + h, rr, gg, bb, aa}, {x, y, rr, gg, bb, aa},
                       {x + w, y + h, rr, gg, bb, aa}, {x, y + h, rr, gg, bb, aa}};
        vertices.insert(vertices.end(), std::begin(v), std::end(v));
    }
    void flush() {
        if (vertices.empty())
            return;
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_FLOAT, sizeof(Vertex), &vertices[0].x);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &vertices[0].r);
        glDrawArrays(GL_TRIANGLES, 0, vertices.size());
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        vertices.clear();
    }
    void ortho(int w, int h) {
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, w, h, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }
    void quad(int w, int h) {
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);
        glVertex2f(0, h);
        glTexCoord2f(1, 0);
        glVertex2f(w, h);
        glTexCoord2f(1, 1);
        glVertex2f(w, 0);
        glTexCoord2f(0, 1);
        glVertex2f(0, 0);
        glEnd();
    }
    void f(GLuint program, const char* name, float v) {
        Uniform1f(GetUniformLocation(program, name), v);
    }
    void i(GLuint program, const char* name, int v) {
        Uniform1i(GetUniformLocation(program, name), v);
    }
    void xy(GLuint program, const char* name, float x, float y) {
        Uniform2f(GetUniformLocation(program, name), x, y);
    }
    Target target(int w, int h, bool floating = false) {
        Target t;
        t.width = w;
        t.height = h;
        glGenTextures(1, &t.texture);
        glBindTexture(GL_TEXTURE_2D, t.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, floating ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, floating ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, floating ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA,
                     floating ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
        GenFramebuffers(1, &t.fbo);
        BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
        if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            DeleteFramebuffers(1, &t.fbo);
            glDeleteTextures(1, &t.texture);
            BindFramebuffer(GL_FRAMEBUFFER, 0);
            throw std::runtime_error("OpenGL framebuffer unavailable");
        }
        ortho(w, h);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        return t;
    }
    GLuint shader(const std::string& source) {
        GLuint s = CreateShader(GL_FRAGMENT_SHADER);
        const char* text = source.c_str();
        ShaderSource(s, 1, &text, nullptr);
        CompileShader(s);
        GLint ok;
        GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096]{};
            GetShaderInfoLog(s, sizeof(log), nullptr, log);
            DeleteShader(s);
            throw std::runtime_error(std::string("Shader compile: ") + log);
        }
        GLuint p = CreateProgram();
        AttachShader(p, s);
        LinkProgram(p);
        DeleteShader(s);
        GetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096]{};
            GetProgramInfoLog(p, sizeof(log), nullptr, log);
            DeleteProgram(p);
            throw std::runtime_error(std::string("Shader link: ") + log);
        }
        return p;
    }
    template <class T> struct Com {
        T* p{};
        ~Com() {
            if (p)
                p->Release();
        }
        T** address() {
            return &p;
        }
        T* operator->() {
            return p;
        }
    };
    static void checked(HRESULT value) {
        if (FAILED(value))
            throw std::runtime_error("Cannot decode project image with Windows WIC");
    }
    GLuint image(const std::string& path, int& w, int& h) {
        if (path.empty())
            return 0;
        auto bytes = resources->read(path);
        if (bytes.size() < 8)
            throw std::runtime_error("Invalid RGBA sprite");
        std::uint32_t ww{}, hh{};
        std::vector<std::uint8_t> decoded;
        const std::uint8_t* pixels = nullptr;
        auto extension = resources->resolve(path);
        for (auto& c : extension)
            if (c >= 'A' && c <= 'Z')
                c += 32;
        if (extension.ends_with(".rgba")) {
            std::memcpy(&ww, bytes.data(), 4);
            std::memcpy(&hh, bytes.data() + 4, 4);
            pixels = bytes.data() + 8;
            if (!ww || !hh || ww > 8192 || hh > 8192 ||
                std::uint64_t(ww) * hh * 4 > 64ull * 1024 * 1024 ||
                std::uint64_t(ww) * hh * 4 + 8 != bytes.size())
                throw std::runtime_error("Invalid sprite dimensions");
        } else {
            Com<IWICImagingFactory> factory;
            checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_IWICImagingFactory,
                                     reinterpret_cast<void**>(factory.address())));
            Com<IWICStream> stream;
            checked(factory->CreateStream(stream.address()));
            checked(stream->InitializeFromMemory(bytes.data(), DWORD(bytes.size())));
            Com<IWICBitmapDecoder> decoder;
            checked(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnLoad,
                                                     decoder.address()));
            Com<IWICBitmapFrameDecode> frame;
            checked(decoder->GetFrame(0, frame.address()));
            checked(frame->GetSize(&ww, &hh));
            if (!ww || !hh || ww > 8192 || hh > 8192 || std::uint64_t(ww) * hh * 4 > 64ull * 1024 * 1024)
                throw std::runtime_error("Project image size limit: 8192");
            Com<IWICFormatConverter> converter;
            checked(factory->CreateFormatConverter(converter.address()));
            checked(converter->Initialize(frame.p, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                          nullptr, 0, WICBitmapPaletteTypeCustom));
            decoded.resize(std::size_t(ww) * hh * 4);
            checked(converter->CopyPixels(nullptr, ww * 4, UINT(decoded.size()), decoded.data()));
            pixels = decoded.data();
        }
        w = ww;
        h = hh;
        GLuint id;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        return id;
    }
    std::map<std::string, LayerTexture, std::less<>> prepareAnimations(const AnimationLibrary& library) {
        validateAnimations(library);
        std::map<std::string, LayerTexture, std::less<>> result;
        std::size_t bytes = 0;
        try {
            for (const auto& clip : library.clips) {
                auto path = std::string(clip.sprite.c_str());
                if (!result.contains(path)) {
                    auto& texture = result[path];
                    texture.id = image(path, texture.width, texture.height);
                    bytes += std::size_t(texture.width) * texture.height * 4;
                    if (bytes > 64ull * 1024 * 1024)
                        throw std::runtime_error("Animation textures exceed 64 MiB GPU budget");
                }
                const auto& texture = result.at(path);
                for (const auto& frame : clip.frames)
                    if (std::int64_t(frame.column + 1) * clip.frameWidth > texture.width ||
                        std::int64_t(frame.row + 1) * clip.frameHeight > texture.height)
                        throw std::runtime_error("Animation frame lies outside sprite sheet: " + path);
            }
        } catch (...) {
            for (auto& [path, texture] : result)
                glDeleteTextures(1, &texture.id);
            throw;
        }
        return result;
    }
    bool animated(const AnimationPlayback& playback, const CameraState& camera, Vec2 origin,
                  float bodyWidth, int facing, float alpha) {
        const auto* clip = findAnimation(animations, playback.clip);
        if (!clip || playback.frame >= clip->frames.size())
            return false;
        auto found = animationTextures.find(clip->sprite.c_str());
        if (found == animationTextures.end())
            return false;
        const auto& texture = found->second;
        const auto& frame = clip->frames[playback.frame];
        bool flip = clip->flip && facing < 0;
        float anchor = flip ? clip->frameWidth - clip->anchorX : clip->anchorX;
        auto pos = camera.project(origin.x + (flip ? bodyWidth : 0) - anchor * clip->scale,
                                  origin.y - clip->anchorY * clip->scale);
        float width = clip->frameWidth * clip->scale * pixelsPerUnit;
        float height = clip->frameHeight * clip->scale * pixelsPerUnit;
        if (pos.x + width < 0 || pos.y + height < 0 || pos.x > 320 || pos.y > 180)
            return true;
        float left = frame.column * clip->frameWidth / float(texture.width);
        float right = (frame.column + 1) * clip->frameWidth / float(texture.width);
        float top = frame.row * clip->frameHeight / float(texture.height);
        float bottom = (frame.row + 1) * clip->frameHeight / float(texture.height);
        if (flip)
            std::swap(left, right);
        flush();
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture.id);
        beginImage(texture.width, texture.height, left, top, right, bottom);
        glColor4f(1, 1, 1, alpha);
        glBegin(GL_QUADS);
        glTexCoord2f(left, top);
        glVertex2f(pos.x, pos.y);
        glTexCoord2f(right, top);
        glVertex2f(pos.x + width, pos.y);
        glTexCoord2f(right, bottom);
        glVertex2f(pos.x + width, pos.y + height);
        glTexCoord2f(left, bottom);
        glVertex2f(pos.x, pos.y + height);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        UseProgram(0);
        return true;
    }
    void beginImage(int w, int h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
        UseProgram(sampler);
        i(sampler, "u_image", 0);
        i(sampler, "u_mode", imageFilter == 2 ? 6 : imageFilter);
        xy(sampler, "u_size", w, h);
        xy(sampler, "u_cell", 0, 0);
        f(sampler, "u_sharpness", 0);
        float bounds[4]{std::min(u0, u1) + .5f / w, std::min(v0, v1) + .5f / h,
                        std::max(u0, u1) - .5f / w, std::max(v0, v1) - .5f / h};
        Uniform4fv(GetUniformLocation(sampler, "u_bounds"), 1, bounds);
    }
    void sprite(GLuint texture, int texW, int texH, const SpriteConfig& cfg, int state, float time,
                Vec2 p, int facing, float alpha = 1) {
        if (!texture)
            return;
        auto clip = cfg.clips[std::clamp(state, 0, 3)];
        int col = clip.first + int(std::floor(time * clip.fps)) % clip.count;
        float u0 = float(col * cfg.frame) / texW, u1 = float((col + 1) * cfg.frame) / texW,
              v0 = float(clip.row * cfg.frame) / texH, v1 = float((clip.row + 1) * cfg.frame) / texH;
        if (facing < 0)
            std::swap(u0, u1);
        float x = p.x + std::floor(cfg.offsetX * pixelsPerUnit),
              y = p.y + std::floor(cfg.offsetY * pixelsPerUnit),
              w = std::floor(cfg.width * pixelsPerUnit), h = std::floor(cfg.height * pixelsPerUnit);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        beginImage(texW, texH, u0, v0, u1, v1);
        glColor4f(1, 1, 1, alpha);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0);
        glVertex2f(x, y);
        glTexCoord2f(u1, v0);
        glVertex2f(x + w, y);
        glTexCoord2f(u1, v1);
        glVertex2f(x + w, y + h);
        glTexCoord2f(u0, v1);
        glVertex2f(x, y + h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        UseProgram(0);
    }
    void sample(Target& out, int pass, float dt, Vec2 source, Vec2 force) {
        BindFramebuffer(GL_FRAMEBUFFER, out.fbo);
        ortho(out.width, out.height);
        UseProgram(fluidProgram);
        xy(fluidProgram, "u_resolution", out.width, out.height);
        xy(fluidProgram, "u_source", source.x, source.y);
        xy(fluidProgram, "u_force", force.x, force.y);
        f(fluidProgram, "u_dt", dt);
        i(fluidProgram, "u_pass", pass);
        const char* names[] = {"u_velocity", "u_pressure", "u_divergence", "u_dye"};
        GLuint ids[] = {velocity[vi].texture, pressure[pi].texture, divergence.texture, dye[di].texture};
        for (int unit = 0; unit < 4; ++unit) {
            ActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, ids[unit]);
            i(fluidProgram, names[unit], unit);
        }
        glColor4f(1, 1, 1, 1);
        quad(out.width, out.height);
        ActiveTexture(GL_TEXTURE0);
        UseProgram(0);
    }
    void resetFluid() {
        for (auto* t :
             {&velocity[0], &velocity[1], &pressure[0], &pressure[1], &dye[0], &dye[1], &divergence}) {
            BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
};
Renderer::Renderer() : p_(std::make_unique<Impl>()) {}
Renderer::~Renderer() {
    if (p_->context) {
        auto& p = *p_;
        for (auto* t : {&p.scene, &p.composite, &p.reduced, &p.filtered, &p.velocity[0], &p.velocity[1],
                        &p.pressure[0], &p.pressure[1], &p.dye[0], &p.dye[1], &p.divergence})
            if (t->fbo) {
                p.DeleteFramebuffers(1, &t->fbo);
                glDeleteTextures(1, &t->texture);
            }
        for (auto program : {p.sampler, p.reduction, p.fxaa})
            if (program)
                p.DeleteProgram(program);
        if (p.transitionProgram)
            p.DeleteProgram(p.transitionProgram);
        if (p.customTransition)
            p.DeleteProgram(p.customTransition);
        if (p.lighting)
            p.DeleteProgram(p.lighting);
        if (p.bg)
            p.DeleteProgram(p.bg);
        if (p.sdf)
            p.DeleteProgram(p.sdf);
        if (p.fluidProgram)
            p.DeleteProgram(p.fluidProgram);
        for (auto& [path, texture] : p.layerTextures)
            glDeleteTextures(1, &texture.id);
        for (auto& [path, texture] : p.animationTextures)
            glDeleteTextures(1, &texture.id);
        glDeleteTextures(1, &p.dialogueTexture);
        glDeleteTextures(1, &p.tileTexture);
        glDeleteTextures(1, &p.playerTexture);
        glDeleteTextures(1, &p.bossTexture);
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(p.context);
    }
    if (p_->dc)
        ReleaseDC(p_->window, p_->dc);
}
void Renderer::initialize(HWND window, Resources* resources) {
    p_->window = window;
    p_->dc = GetDC(window);
    PIXELFORMATDESCRIPTOR desc{};
    desc.nSize = sizeof(desc);
    desc.nVersion = 1;
    desc.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    desc.iPixelType = PFD_TYPE_RGBA;
    desc.cColorBits = 32;
    int fmt = ChoosePixelFormat(p_->dc, &desc);
    if (!fmt || !SetPixelFormat(p_->dc, fmt, &desc))
        throw std::runtime_error("OpenGL pixel format failed");
    p_->context = wglCreateContext(p_->dc);
    if (!p_->context || !wglMakeCurrent(p_->dc, p_->context))
        throw std::runtime_error("OpenGL context failed");
#define LOAD(name)                                                                                      \
    do {                                                                                                \
        auto address = wglGetProcAddress("gl" #name);                                                   \
        if (!address || reinterpret_cast<std::uintptr_t>(address) <= 3 ||                               \
            reinterpret_cast<std::uintptr_t>(address) == ~std::uintptr_t{})                             \
            throw std::runtime_error("OpenGL 3 framebuffer / GLSL support required: gl" #name);         \
        p_->name = std::bit_cast<decltype(p_->name)>(address);                                          \
    } while (false)
    LOAD(CreateShader);
    LOAD(ShaderSource);
    LOAD(CompileShader);
    LOAD(GetShaderiv);
    LOAD(GetShaderInfoLog);
    LOAD(CreateProgram);
    LOAD(AttachShader);
    LOAD(LinkProgram);
    LOAD(GetProgramiv);
    LOAD(GetProgramInfoLog);
    LOAD(UseProgram);
    LOAD(DeleteShader);
    LOAD(DeleteProgram);
    LOAD(GetUniformLocation);
    LOAD(Uniform4fv);
    LOAD(Uniform1f);
    LOAD(Uniform1i);
    LOAD(Uniform2f);
    LOAD(GenFramebuffers);
    LOAD(BindFramebuffer);
    LOAD(FramebufferTexture2D);
    LOAD(CheckFramebufferStatus);
    LOAD(DeleteFramebuffers);
    LOAD(ActiveTexture);
#undef LOAD
    setVsync(true);
    p_->vertices.reserve(100000);
    p_->sampler = p_->shader(samplingShader);
    p_->reduction = p_->shader(reductionShader);
    p_->fxaa = p_->shader(fxaaShader);
    p_->reduced = p_->target(320, 180);
    p_->filtered = p_->target(320, 180);
    p_->scene = p_->target(320, 180);
    p_->composite = p_->target(320, 180);
    for (int j = 0; j < 2; ++j) {
        p_->velocity[j] = p_->target(80, 45, true);
        p_->pressure[j] = p_->target(80, 45, true);
        p_->dye[j] = p_->target(80, 45, true);
    }
    p_->divergence = p_->target(80, 45, true);
    p_->BindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (resources)
        setResources(resources);
}

void Renderer::setAnimations(const AnimationLibrary& library) {
    auto next = library;
    auto textures = p_->prepareAnimations(next);
    for (auto& [path, texture] : p_->animationTextures)
        glDeleteTextures(1, &texture.id);
    p_->animationTextures = std::move(textures);
    p_->animations = std::move(next);
}
void Renderer::setResources(Resources* resources) {
    auto cfg = resources->config();
    auto objectLibrary = resources->contains("objects.echo")
                             ? loadObjects(resources->text("objects.echo"))
                             : ObjectLibrary{};
    auto animations = resources->contains("animations.echo")
                          ? loadAnimations(resources->text("animations.echo"))
                          : AnimationLibrary{};
    auto& p = *p_;
    auto previousResources = p.resources;
    GLuint programs[6]{}, textures[3]{};
    std::map<std::string, Impl::LayerTexture, std::less<>> layers, animationTextures;
    int dimensions[6]{};
    p.resources = resources;
    try {
        programs[0] = p.shader(resources->text(cfg.backgroundShader));
        programs[1] = p.shader(resources->text(cfg.lightingShader));
        programs[2] = p.shader(resources->text(cfg.raymarchShader));
        programs[3] = p.shader(resources->text(cfg.fluidShader));
        programs[4] = p.shader(std::string(transitionPrelude) + transitionBuiltins + transitionPostlude);
        if (!cfg.transitionShader.empty())
            programs[5] = p.shader(std::string(transitionPrelude) +
                                   resources->text(cfg.transitionShader) + transitionPostlude);
        animationTextures = p.prepareAnimations(animations);
        std::size_t imageBudget = 0;
        for (auto& map : cfg.maps) {
            auto level = loadLevel(resources->text(map));
            for (const auto& portal : level.portals)
                if (portal.transitionOverride && portal.transition.kind == TransitionKind::Custom &&
                    cfg.transitionShader.empty())
                    throw std::runtime_error("Custom portal transition requires transition_shader");
            for (auto& layer : level.layers) {
                std::string path(layer.image.c_str());
                if (path.empty() || layers.contains(path))
                    continue;
                auto& texture = layers[path];
                texture.id = p.image(path, texture.width, texture.height);
                imageBudget += std::size_t(texture.width) * texture.height * 4;
                if (imageBudget > 128ull * 1024 * 1024)
                    throw std::runtime_error("Layer images exceed 128 MiB GPU budget");
            }
        }
        for (const auto& t : objectLibrary.templates) {
            std::string path(t.sprite.c_str());
            if (path.empty() || layers.contains(path))
                continue;
            auto& texture = layers[path];
            texture.id = p.image(path, texture.width, texture.height);
            imageBudget += std::size_t(texture.width) * texture.height * 4;
            if (imageBudget > 128ull * 1024 * 1024)
                throw std::runtime_error("Scene images exceed 128 MiB GPU budget");
        }
        textures[0] = p.image(cfg.tileSprite, dimensions[0], dimensions[1]);
        textures[1] = p.image(cfg.player.path, dimensions[2], dimensions[3]);
        textures[2] = p.image(cfg.boss.path, dimensions[4], dimensions[5]);
        if (!cfg.tileSprite.empty() &&
            (dimensions[0] < cfg.tileFrame * 4 || dimensions[1] < cfg.tileFrame * 4))
            throw std::runtime_error("Tile atlas requires at least 4x4 tiles");
    } catch (...) {
        p.resources = previousResources;
        for (auto program : programs)
            if (program)
                p.DeleteProgram(program);
        glDeleteTextures(3, textures);
        for (auto& [path, texture] : layers)
            glDeleteTextures(1, &texture.id);
        for (auto& [path, texture] : animationTextures)
            glDeleteTextures(1, &texture.id);
        throw;
    }
    for (auto program :
         {p.bg, p.lighting, p.sdf, p.fluidProgram, p.transitionProgram, p.customTransition})
        if (program)
            p.DeleteProgram(program);
    glDeleteTextures(1, &p.tileTexture);
    glDeleteTextures(1, &p.playerTexture);
    glDeleteTextures(1, &p.bossTexture);
    for (auto& [path, texture] : p.layerTextures)
        glDeleteTextures(1, &texture.id);
    for (auto& [path, texture] : p.animationTextures)
        glDeleteTextures(1, &texture.id);
    p.animationTextures = std::move(animationTextures);
    p.animations = std::move(animations);
    p.layerTextures = std::move(layers);
    p.bg = programs[0];
    p.lighting = programs[1];
    p.sdf = programs[2];
    p.fluidProgram = programs[3];
    p.transitionProgram = programs[4];
    p.customTransition = programs[5];
    p.config = std::move(cfg);
    p.objects = std::move(objectLibrary);
    p.tileTexture = textures[0];
    p.playerTexture = textures[1];
    p.bossTexture = textures[2];
    p.tileW = dimensions[0];
    p.tileH = dimensions[1];
    p.playerW = dimensions[2];
    p.playerH = dimensions[3];
    p.bossW = dimensions[4];
    p.bossH = dimensions[5];
    const auto& committed = p.config;
    setSampling(committed.textureFilter, committed.upscaleFilter, committed.antiAliasing,
                committed.renderSharpness);
    setVsync(committed.vsync);
    sunlight = committed.sunlight;
    fireflies = committed.fireflies;
    fluid = committed.fluid;
    raymarch = committed.raymarch;
    environment = committed.environment;
    weather = committed.weather;
    weatherStrength = committed.weatherStrength;
    wind = committed.wind;
    fog = committed.fog;
    p_->resetFluid();
    p_->generation = ~0ull;
}
void Renderer::setBackgroundSource(const std::string& source) {
    auto next = p_->shader(source);
    if (p_->bg)
        p_->DeleteProgram(p_->bg);
    p_->bg = next;
}
void Renderer::setTransitionSource(const std::string& source) {
    auto next = p_->shader(std::string(transitionPrelude) + source + transitionPostlude);
    if (p_->customTransition)
        p_->DeleteProgram(p_->customTransition);
    p_->customTransition = next;
}
void Renderer::resize(int w, int h) {
    p_->width = std::max(1, w);
    p_->height = std::max(1, h);
    scale = std::max(1, std::min(w / 320, h / 180));
    offsetX = (w - 320 * scale) / 2;
    offsetY = (h - 180 * scale) / 2;
}
void Renderer::draw(const WorldState& w, const CameraState& camera, bool editing, int,
                    const BarrageLibrary* emitters, std::uint64_t selectedEmitter) {
    auto& p = *p_;
    drawCalls = 0;
    float delta = std::clamp(float(w.clock - p.previousTime), 0.0f, .033f);
    if (w.timelineGenerationId != p.generation || w.clock < p.previousTime) {
        p.resetFluid();
        p.generation = w.timelineGenerationId;
    }
    p.previousTime = w.clock;
    if (fluid && delta > 0) {
        glDisable(GL_BLEND);
        Vec2 pos = camera.project(w.kino.x + 14, w.kino.y + 32);
        Vec2 source{pos.x / 320, 1 - pos.y / 180}, force{w.kino.vx * .08f, -w.kino.vy * .04f};
        p.sample(p.velocity[1 - p.vi], 0, delta, source, force);
        p.vi = 1 - p.vi;
        for (int iteration = 0; iteration < 2; ++iteration) {
            p.sample(p.velocity[1 - p.vi], 5, delta, source, force);
            p.vi = 1 - p.vi;
        }
        p.sample(p.divergence, 1, delta, source, force);
        for (int iteration = 0; iteration < 16; ++iteration) {
            p.sample(p.pressure[1 - p.pi], 2, delta, source, force);
            p.pi = 1 - p.pi;
        }
        p.sample(p.velocity[1 - p.vi], 3, delta, source, force);
        p.vi = 1 - p.vi;
        p.sample(p.dye[1 - p.di], 4, delta, source, force);
        p.di = 1 - p.di;
        drawCalls += 22;
        glEnable(GL_BLEND);
    }
    p.BindFramebuffer(GL_FRAMEBUFFER, p.scene.fbo);
    p.ortho(320, 180);
    glViewport(0, 0, p.scene.width, p.scene.height);
    glClearColor(.025f, .04f, .055f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (p.bg && backgroundEnabled) {
        p.UseProgram(p.bg);
        p.xy(p.bg, "u_resolution", p.scene.width, p.scene.height);
        p.xy(p.bg, "u_camera", camera.logicalX, camera.logicalY);
        p.f(p.bg, "u_time", w.clock);
        p.f(p.bg, "u_generation", float(w.timelineGenerationId));
        p.i(p.bg, "u_environment", environment);
        p.i(p.bg, "u_sunlight", sunlight);
        p.i(p.bg, "u_weather", weather);
        p.f(p.bg, "u_intensity", weatherStrength);
        p.f(p.bg, "u_wind", wind);
        p.f(p.bg, "u_fog", fog);
        glColor4f(1, 1, 1, 1);
        p.quad(320, 180);
        p.UseProgram(0);
        ++drawCalls;
    }
    if (raymarch) {
        p.UseProgram(p.sdf);
        p.xy(p.sdf, "u_resolution", p.scene.width, p.scene.height);
        p.xy(p.sdf, "u_camera", camera.logicalX, camera.logicalY);
        p.f(p.sdf, "u_time", w.clock);
        glColor4f(1, 1, 1, 1);
        p.quad(320, 180);
        p.UseProgram(0);
        ++drawCalls;
    }
    if (fluid) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, p.dye[p.di].texture);
        glColor4f(1, 1, 1, .4f);
        p.quad(320, 180);
        glDisable(GL_TEXTURE_2D);
        p.UseProgram(0);
        ++drawCalls;
    }
    auto rect = [&](float x, float y, float a, float b, int r, int g, int bl, int alpha = 255) {
        p.rect(x, y, a, b, r, g, bl, alpha);
    };
    auto tiles = [&](int layerIndex) {
        auto& layer = w.level.layers[layerIndex];
        if (!layer.visible || (editing && editorHidden[layerIndex]))
            return;
        if (!layer.image.empty()) {
            auto found = p.layerTextures.find(layer.image.c_str());
            if (found != p.layerTextures.end()) {
                p.flush();
                auto& r = layer.imageBounds;
                float x = std::floor((r.x - camera.renderX * layer.parallax) * pixelsPerUnit);
                float y = std::floor((r.y - camera.renderY * layer.parallax) * pixelsPerUnit);
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, found->second.id);
                p.beginImage(found->second.width, found->second.height);
                glColor4f(1, 1, 1, 1);
                glBegin(GL_QUADS);
                glTexCoord2f(0, 0);
                glVertex2f(x, y);
                glTexCoord2f(1, 0);
                glVertex2f(x + r.w * pixelsPerUnit, y);
                glTexCoord2f(1, 1);
                glVertex2f(x + r.w * pixelsPerUnit, y + r.h * pixelsPerUnit);
                glTexCoord2f(0, 1);
                glVertex2f(x, y + r.h * pixelsPerUnit);
                glEnd();
                glDisable(GL_TEXTURE_2D);
                p.UseProgram(0);
                ++drawCalls;
            }
        }
        if (!p.tileTexture)
            return;
        p.flush();
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, p.tileTexture);
        p.beginImage(p.tileW, p.tileH);
        p.xy(p.sampler, "u_cell", p.config.tileFrame, p.config.tileFrame);
        glColor4f(layerIndex == 0 ? .65f : 1, layerIndex == 0 ? .75f : 1, layerIndex == 0 ? .85f : 1, 1);
        int frame = p.config.tileFrame, columns = p.tileW / frame, rows = p.tileH / frame;
        if (columns < 1 || rows < 1) {
            glDisable(GL_TEXTURE_2D);
            p.UseProgram(0);
            return;
        }
        float cameraX = camera.renderX * layer.parallax, cameraY = camera.renderY * layer.parallax;
        glBegin(GL_QUADS);
        int first = std::max(0, int(cameraX / tileSize)), last = std::min(mapWidth, first + 42);
        for (int y = 0; y < mapHeight; ++y)
            for (int x = first; x < last; ++x) {
                int index = int(layer.cells[y * mapWidth + x]) - 1;
                if (index < 0 || index >= columns * rows)
                    continue;
                float px = std::floor((x * tileSize - cameraX) * pixelsPerUnit),
                      py = std::floor((y * tileSize - cameraY) * pixelsPerUnit);
                float u0 = float((index % columns) * frame) / p.tileW,
                      v0 = float((index / columns) * frame) / p.tileH, u1 = u0 + float(frame) / p.tileW,
                      v1 = v0 + float(frame) / p.tileH;
                glTexCoord2f(u0, v0);
                glVertex2f(px, py);
                glTexCoord2f(u1, v0);
                glVertex2f(px + 8, py);
                glTexCoord2f(u1, v1);
                glVertex2f(px + 8, py + 8);
                glTexCoord2f(u0, v1);
                glVertex2f(px, py + 8);
            }
        glEnd();
        glDisable(GL_TEXTURE_2D);
        p.UseProgram(0);
        ++drawCalls;
    };
    tiles(0);
    tiles(1);
    for (int y = 0; y < mapHeight; ++y)
        for (int x = std::max(0, int(camera.renderX / tileSize));
             x < std::min(mapWidth, int(camera.renderX / tileSize) + 42); ++x) {
            Tile t = w.level.at(x, y);
            if (t == Tile::Empty)
                continue;
            auto v = camera.project(x * tileSize, y * tileSize);
            if (t == Tile::Solid && (!p.tileTexture || w.level.layers[1].cells[y * mapWidth + x] == 0)) {
                rect(v.x, v.y, 8, 8, 29, 47, 51);
                rect(v.x, v.y, 8, 1, 109, 149, 125);
                rect(v.x + 1, v.y + 2, 5, 1, 43, 67, 64);
                rect(v.x + 6, v.y + 4, 1, 3, 18, 34, 37);
            } else if (t == Tile::OneWay) {
                rect(v.x, v.y, 8, 1, 185, 166, 122);
                rect(v.x + 1, v.y + 1, 6, 1, 97, 116, 92);
            } else if (t == Tile::Spike) {
                rect(v.x + 2, v.y, 4, 2, 212, 119, 114);
                rect(v.x, v.y + 2, 8, 2, 172, 79, 93);
            }
        }
    if ((editing && editorCollisions) || (!editing && debugCollisions)) {
        for (auto& c : w.level.colliders) {
            p.flush();
            glColor4f(.40f, .66f, .96f, .8f);
            glBegin(GL_LINE_LOOP);
            if (c.kind == ColliderKind::Circle) {
                for (int i = 0; i < 32; ++i) {
                    float angle = i * 6.2831853f / 32;
                    auto v = camera.project(
                        c.bounds.x + c.bounds.w * .5f + std::cos(angle) * c.bounds.w * .5f,
                        c.bounds.y + c.bounds.h * .5f + std::sin(angle) * c.bounds.h * .5f);
                    glVertex2f(v.x, v.y);
                }
            } else {
                std::array<Vec2, 16> vertices{};
                int count = colliderVertices(c, vertices);
                for (int i = 0; i < count; ++i) {
                    auto v = camera.project(vertices[i].x, vertices[i].y);
                    glVertex2f(v.x, v.y);
                }
            }
            glEnd();
            ++drawCalls;
        }
    }
    for (auto& platform : w.level.platforms) {
        auto v = camera.project(platform.pos.x, platform.pos.y);
        rect(v.x, v.y, platform.base.w * pixelsPerUnit, 2, 141, 202, 194);
    }
    for (auto& z : w.aster.attack.zones) {
        float t = w.aster.attack.t;
        if (t < z.start - z.lead || t > z.end)
            continue;
        auto v = camera.project(z.x, z.y);
        int r = z.brake ? 226 : 100, g = z.brake ? 170 : 170, b = z.brake ? 86 : 226;
        int alpha = t < z.start ? 75 : 160;
        rect(v.x, v.y, z.w * pixelsPerUnit, z.h * pixelsPerUnit, r, g, b, alpha);
        rect(v.x, v.y, z.w * pixelsPerUnit, 1, r, g, b);
        rect(v.x, v.y, 1, z.h * pixelsPerUnit, r, g, b);
    }
    for (auto& volley : w.aster.attack.volleys)
        if (!volley.fired && w.aster.attack.t >= volley.at - .85f) {
            auto v = camera.project(volley.origin.x, volley.origin.y);
            rect(v.x - 3, v.y - 3, 7, 1, 233, 140, 142);
            rect(v.x - 3, v.y + 3, 7, 1, 233, 140, 142);
        }
    if (!editing) {
        p.flush();
        glColor4f(.86f, .61f, .31f, .55f);
        glBegin(GL_LINES);
        for (const auto& emitter : w.barrage.emitters) {
            if (!emitter.warning || !emitter.enabled || emitter.finished)
                continue;
            int count = std::min(int(emitter.warningCount), 64);
            for (int i = 0; i < count; ++i) {
                float fraction = emitter.warningShape == 2 ? 0.f
                                 : emitter.warningShape == 0 || emitter.warningShape == 3
                                     ? float(i) / count
                                     : (count > 1 ? float(i) / (count - 1) - .5f : 0);
                float angle = emitter.warningAngle + fraction * emitter.warningSpread * .01745329252f;
                auto a = camera.project(emitter.origin.x, emitter.origin.y);
                auto b = camera.project(emitter.origin.x + std::cos(angle) * 180,
                                        emitter.origin.y + std::sin(angle) * 180);
                glVertex2f(a.x, a.y);
                glVertex2f(b.x, b.y);
            }
        }
        glEnd();
        ++drawCalls;
    }
    for (auto& b : w.bullets) {
        auto v = camera.project(b.x, b.y);
        int radius = std::max(1, int(std::ceil(b.visualR * pixelsPerUnit)));
        if (v.x + radius < 0 || v.y + radius < 0 || v.x - radius > 320 || v.y - radius > 180)
            continue;
        constexpr std::array<std::array<int, 3>, 8> colors{{{227, 165, 113},
                                                            {130, 207, 231},
                                                            {196, 151, 228},
                                                            {229, 127, 145},
                                                            {172, 210, 152},
                                                            {242, 217, 134},
                                                            {163, 169, 236},
                                                            {221, 226, 237}}};
        const auto& color = colors[std::min(std::size_t(b.palette), colors.size() - 1)];
        rect(v.x - radius, v.y - radius / 2, radius * 2 + 1, radius + 1, color[0], color[1], color[2]);
        rect(v.x - radius / 2, v.y - radius, radius + 1, radius * 2 + 1, color[0], color[1], color[2]);
        rect(v.x, v.y, 1, 1, 245, 238, 211);
    }
    for (const auto& event : w.level.events)
        if (event.visible && !event.erased && (!p.tileTexture || event.graphic == 0)) {
            auto pos = camera.project(event.region.x, event.region.y);
            if (pos.x + event.region.w * pixelsPerUnit < 0 || pos.x > 320 || pos.y > 180 ||
                pos.y + event.region.h * pixelsPerUnit < 0)
                continue;
            rect(pos.x, pos.y, event.region.w * pixelsPerUnit, event.region.h * pixelsPerUnit,
                 event.graphic ? 112 : 143, event.graphic ? 142 : 120, event.graphic ? 177 : 190,
                 editing ? 150 : 255);
            rect(pos.x + 2, pos.y + 2, 3, 1, 235, 228, 246);
            rect(pos.x + 2, pos.y + 3, 1, 4, 235, 228, 246);
            rect(pos.x + 2, pos.y + 4, 3, 1, 235, 228, 246);
            rect(pos.x + 2, pos.y + 6, 3, 1, 235, 228, 246);
        }
    if (p.tileTexture && std::any_of(w.level.events.begin(), w.level.events.end(), [](const auto& e) {
            return e.visible && !e.erased && e.graphic > 0;
        })) {
        p.flush();
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, p.tileTexture);
        p.beginImage(p.tileW, p.tileH);
        p.xy(p.sampler, "u_cell", p.config.tileFrame, p.config.tileFrame);
        glColor4f(1, 1, 1, 1);
        glBegin(GL_QUADS);
        int frame = p.config.tileFrame, columns = p.tileW / frame, rows = p.tileH / frame;
        if (columns > 0 && rows > 0)
            for (const auto& event : w.level.events)
                if (event.visible && !event.erased && event.graphic > 0 &&
                    event.graphic <= columns * rows) {
                    auto pos = camera.project(event.region.x, event.region.y);
                    int index = event.graphic - 1;
                    float u = float(index % columns * frame) / p.tileW,
                          v = float(index / columns * frame) / p.tileH;
                    float u1 = u + float(frame) / p.tileW, v1 = v + float(frame) / p.tileH;
                    float width = event.region.w * pixelsPerUnit,
                          height = event.region.h * pixelsPerUnit;
                    glTexCoord2f(u, v);
                    glVertex2f(pos.x, pos.y);
                    glTexCoord2f(u1, v);
                    glVertex2f(pos.x + width, pos.y);
                    glTexCoord2f(u1, v1);
                    glVertex2f(pos.x + width, pos.y + height);
                    glTexCoord2f(u, v1);
                    glVertex2f(pos.x, pos.y + height);
                }
        glEnd();
        glDisable(GL_TEXTURE_2D);
        p.UseProgram(0);
        ++drawCalls;
    }
    for (const auto& o : w.objects.slots) {
        if (!o.active || !o.alive || o.map != w.activeMap || o.kind == ObjectKind::Player ||
            o.kind == ObjectKind::Boss)
            continue;
        auto pos = camera.project(o.x, o.y);
        float width = o.width * pixelsPerUnit, height = o.height * pixelsPerUnit;
        if (pos.x + width < 0 || pos.y + height < 0 || pos.x > 320 || pos.y > 180)
            continue;
        if (p.animated(o.animation, camera, {o.x, o.y}, o.width, o.facing,
                       o.invuln > 0 && int(w.clock * 10) % 2 == 0 ? .45f : 1)) {
            ++drawCalls;
            continue;
        }
        const auto* t = findObjectTemplate(p.objects, o.templateId);
        auto texture = t ? p.layerTextures.find(t->sprite.c_str()) : p.layerTextures.end();
        if (texture != p.layerTextures.end()) {
            p.flush();
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture->second.id);
            p.beginImage(texture->second.width, texture->second.height);
            glColor4f(1, 1, 1, o.invuln > 0 && int(w.clock * 10) % 2 == 0 ? .45f : 1);
            glBegin(GL_QUADS);
            float left = o.facing < 0 ? 1.f : 0.f, right = 1 - left;
            glTexCoord2f(left, 0);
            glVertex2f(pos.x, pos.y);
            glTexCoord2f(right, 0);
            glVertex2f(pos.x + width, pos.y);
            glTexCoord2f(right, 1);
            glVertex2f(pos.x + width, pos.y + height);
            glTexCoord2f(left, 1);
            glVertex2f(pos.x, pos.y + height);
            glEnd();
            glDisable(GL_TEXTURE_2D);
            p.UseProgram(0);
            ++drawCalls;
        } else {
            int kind = int(o.kind);
            const int colors[7][3] = {{238, 207, 139}, {226, 139, 137}, {132, 172, 221}, {169, 153, 202},
                                      {235, 191, 103}, {155, 167, 183}, {163, 123, 190}};
            rect(pos.x, pos.y, width, height, colors[kind][0], colors[kind][1], colors[kind][2],
                 editing ? 180 : 255);
            if (o.kind == ObjectKind::Enemy || o.kind == ObjectKind::NPC)
                rect(pos.x + width * .65f, pos.y + 2, 1, 1, 246, 245, 238);
        }
    }
    for (const auto& f : w.level.features) {
        if (!f.enabled && !editing)
            continue;
        if (f.kind == PlatformKind::Falling && f.stage == 2 && !editing)
            continue;
        auto r = editing ? f.base : f.bounds;
        auto v = camera.project(r.x, r.y);
        float width = std::max(1.f, std::floor(r.w * pixelsPerUnit)),
              height = std::max(1.f, std::floor(r.h * pixelsPerUnit));
        if (f.kind == PlatformKind::Water)
            rect(v.x, v.y, width, height, 79, 146, 204, 90);
        else if (f.kind == PlatformKind::Ladder) {
            rect(v.x, v.y, 1, height, 211, 176, 126);
            rect(v.x + width - 1, v.y, 1, height, 211, 176, 126);
            for (float yy = 2; yy < height; yy += 4)
                rect(v.x, v.y + yy, width, 1, 211, 176, 126);
        } else {
            int colors[][3] = {{0, 0, 0},      {0, 0, 0},       {149, 168, 191},
                               {218, 164, 98}, {190, 145, 133}, {137, 157, 202}};
            auto color = colors[int(f.kind)];
            rect(v.x, v.y, width, std::max(2.f, height), color[0], color[1], color[2],
                 f.stage == 1 ? 150 : 255);
            for (float xx = 2; xx < width; xx += 4)
                rect(v.x + xx, v.y, 1, 1, 246, 235, 207);
        }
        if (editing && f.kind == PlatformKind::Lift) {
            auto endpoint = camera.project(f.base.x + f.travel.x, f.base.y + f.travel.y);
            float count = std::clamp(std::ceil(std::hypot(f.travel.x, f.travel.y) * pixelsPerUnit / 4),
                                     1.f, 1024.f);
            for (int i = 0; i <= int(count); ++i) {
                float t = i / count;
                rect(std::floor(v.x + width * .5f + (endpoint.x - v.x) * t),
                     std::floor(v.y + (endpoint.y - v.y) * t), 1, 1, 137, 157, 202, 160);
            }
            rect(endpoint.x, endpoint.y, width, 1, 137, 157, 202);
        }
    }
    auto kino = camera.project(w.kino.x, w.kino.y);
    auto boss = camera.project(w.aster.x, w.aster.y);
    bool playerAnimated =
        p.animated(w.kino.animation, camera, {w.kino.x, w.kino.y}, w.kino.width, w.kino.facing,
                   w.kino.invulnTimer > 0 && int(w.clock * 10) % 2 == 0 ? .45f : 1);
    bool bossAnimated = w.aster.enabled && p.animated(w.aster.animation, camera, {w.aster.x, w.aster.y},
                                                      w.aster.width, w.aster.walkDir, 1);
    if (!playerAnimated && !p.playerTexture)
        rect(kino.x, kino.y, w.kino.width * pixelsPerUnit, w.kino.height * pixelsPerUnit, 238, 207, 139);
    if (w.aster.enabled && !bossAnimated && !p.bossTexture)
        rect(boss.x, boss.y, w.aster.width * pixelsPerUnit, w.aster.height * pixelsPerUnit, 163, 123,
             190);
    p.flush();
    ++drawCalls;
    if (!playerAnimated)
        p.sprite(p.playerTexture, p.playerW, p.playerH, p.config.player, int(w.kino.animState),
                 w.kino.animTime, kino, w.kino.facing,
                 w.kino.invulnTimer > 0 && int(w.clock * 10) % 2 == 0 ? .45f : 1);
    ++drawCalls;
    if (w.aster.enabled && !bossAnimated) {
        p.sprite(p.bossTexture, p.bossW, p.bossH, p.config.boss, int(w.aster.animState),
                 w.aster.animTime, boss, w.aster.walkDir);
        ++drawCalls;
    }
    tiles(2);
    if (editing) {
        auto outline = [&](Rect r, int red, int green, int blue) {
            auto v = camera.project(r.x, r.y);
            float width = r.w * pixelsPerUnit, height = r.h * pixelsPerUnit;
            rect(v.x, v.y, width, 1, red, green, blue);
            rect(v.x, v.y + height - 1, width, 1, red, green, blue);
            rect(v.x, v.y, 1, height, red, green, blue);
            rect(v.x + width - 1, v.y, 1, height, red, green, blue);
        };
        if (emitters)
            for (const auto& e : emitters->emitters) {
                Vec2 point;
                if (e.map != w.activeMap || !barrageEditorOrigin(w.level, e, point))
                    continue;
                auto pos = camera.project(point.x, point.y);
                if (pos.x < -8 || pos.x > 328 || pos.y < -8 || pos.y > 188)
                    continue;
                bool selected = e.id == selectedEmitter;
                int red = selected ? 236 : e.enabled ? 132 : 130;
                int green = selected ? 187 : e.enabled ? 166 : 140;
                int blue = selected ? 105 : e.enabled ? 215 : 158;
                outline({point.x - 24, point.y - 24, 48, 48}, red, green, blue);
                rect(pos.x - 3, pos.y, 7, 1, red, green, blue);
                rect(pos.x, pos.y - 3, 1, 7, red, green, blue);
            }
        if (selectionVisible)
            outline(selectionRect, 84, 150, 234);
        for (auto& d : w.level.dialogues)
            outline(d.region, 185, 152, 222);
        for (auto& portal : w.level.portals)
            outline(portal.region, 100, 166, 230);
        for (const auto& event : w.level.events)
            outline(event.region, 159, 127, 204);
    }
    if (editing)
        for (auto& light : w.level.lights) {
            auto v = camera.project(light.pos.x, light.pos.y);
            rect(v.x - 2, v.y, 5, 1, 255, 218, 125);
            rect(v.x, v.y - 2, 1, 5, 255, 218, 125);
        }
    if (!editing && showHurt(w)) {
        p.flush();
        glColor4f(w.hurt.color[0], w.hurt.color[1], w.hurt.color[2], w.hurt.opacity);
        glLineWidth(std::clamp(w.hurt.outline * float(p.sceneFactor), 1.f, 8.f));
        auto count = w.hurt.source == HurtSource::Custom ? w.hurt.count : 1;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto region = effectiveHurt(w, i);
            if (!region.enabled || !region.mask || (region.focusOnly && !w.kino.focusActive))
                continue;
            auto center = hurtCenter(w.kino, w.hurt, region), pos = camera.project(center.x, center.y);
            auto points = hurtPolygon(w.kino, w.hurt, region);
            if (w.hurt.style != 1) {
                glBegin(w.hurt.style == 2 ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
                if (w.hurt.style == 2)
                    glVertex2f(pos.x, pos.y);
                for (auto point : points) {
                    auto v = camera.project(center.x + (point.x - center.x) * w.hurt.displayScale,
                                            center.y + (point.y - center.y) * w.hurt.displayScale);
                    glVertex2f(v.x, v.y);
                }
                if (w.hurt.style == 2 && points.count) {
                    auto point = points[0];
                    auto v = camera.project(center.x + (point.x - center.x) * w.hurt.displayScale,
                                            center.y + (point.y - center.y) * w.hurt.displayScale);
                    glVertex2f(v.x, v.y);
                }
                glEnd();
            }
            if (w.hurt.style == 1 || w.hurt.style == 3) {
                glBegin(GL_LINES);
                glVertex2f(pos.x - 2, pos.y);
                glVertex2f(pos.x + 2, pos.y);
                glVertex2f(pos.x, pos.y - 2);
                glVertex2f(pos.x, pos.y + 2);
                glEnd();
            }
        }
        glLineWidth(1);
        ++drawCalls;
    }
    if (!editing && w.focus.showIndicator && w.kino.focusActive) {
        rect(kino.x - 2, kino.y - 3, 2, 1, 172, 204, 244);
        rect(kino.x + 8, kino.y - 3, 2, 1, 172, 204, 244);
    }
    if (w.state == TimeState::Fast || w.state == TimeState::Trace) {
        rect(0, 0, 320, 180, 83, 132, 184, 20);
        rect(kino.x + 3, kino.y + 8, 1, 1, 255, 244, 212);
    }
    if (w.aster.enabled) {
        rect(9, 8, 100, 2, 28, 34, 46);
        rect(9, 8, 100 * float(w.aster.hp) / std::max(1, w.aster.maxHp), 2, 174, 109, 130);
    }
    if (w.state == TimeState::Hit) {
        rect(0, 0, 320, 2, 244, 111, 117);
        rect(0, 178, 320, 2, 244, 111, 117);
    }
    p.flush();
    ++drawCalls;
    GLuint sceneInput = p.scene.texture;
    if (p.sceneFactor > 1) {
        p.BindFramebuffer(GL_FRAMEBUFFER, p.reduced.fbo);
        p.ortho(320, 180);
        glDisable(GL_BLEND);
        p.UseProgram(p.reduction);
        p.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, p.scene.texture);
        p.i(p.reduction, "u_image", 0);
        p.i(p.reduction, "u_factor", p.sceneFactor);
        glColor4f(1, 1, 1, 1);
        p.quad(320, 180);
        p.UseProgram(0);
        sceneInput = p.reduced.texture;
        ++drawCalls;
    }
    p.BindFramebuffer(GL_FRAMEBUFFER, p.composite.fbo);
    p.ortho(320, 180);
    glDisable(GL_BLEND);
    p.UseProgram(p.lighting);
    p.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneInput);
    p.i(p.lighting, "u_scene", 0);
    p.xy(p.lighting, "u_resolution", 320, 180);
    p.xy(p.lighting, "u_camera", camera.renderX, camera.renderY);
    p.f(p.lighting, "u_time", w.clock);
    p.f(p.lighting, "u_wind", wind);
    p.i(p.lighting, "u_environment", environment);
    p.i(p.lighting, "u_sunlight", sunlight);
    p.i(p.lighting, "u_fireflies", fireflies);
    p.i(p.lighting, "u_light_count", w.level.lights.count);
    std::array<float, 128> lights{}, colors{};
    int index = 0;
    for (auto& lamp : w.level.lights) {
        auto pos = camera.project(lamp.pos.x, lamp.pos.y);
        int j = index++ * 4;
        lights[j] = pos.x;
        lights[j + 1] = 180 - pos.y;
        lights[j + 2] = lamp.radius * pixelsPerUnit;
        lights[j + 3] = lamp.intensity;
        colors[j] = lamp.warmColor.x;
        colors[j + 1] = lamp.warmColor.y;
        colors[j + 2] = lamp.blue;
        colors[j + 3] = lamp.flicker;
    }
    p.Uniform4fv(p.GetUniformLocation(p.lighting, "u_lights[0]"), 32, lights.data());
    p.Uniform4fv(p.GetUniformLocation(p.lighting, "u_colors[0]"), 32, colors.data());
    glColor4f(1, 1, 1, 1);
    p.quad(320, 180);
    p.UseProgram(0);
    glEnable(GL_BLEND);
    ++drawCalls;
    if (w.transition.phase != TransitionPhase::Idle &&
        w.transition.config.kind != TransitionKind::None) {
        const auto& t = w.transition;
        GLuint program = t.config.kind == TransitionKind::Custom && p.customTransition
                             ? p.customTransition
                             : p.transitionProgram;
        p.UseProgram(program);
        p.f(program, "u_cover", transitionCover(t));
        p.f(program, "u_detail", t.config.detail);
        p.i(program, "u_kind", int(t.config.kind));
        p.i(program, "u_direction", t.config.direction);
        p.i(program, "u_phase", int(t.phase));
        p.xy(program, "u_resolution", 320, 180);
        Vec2 center{160, 90};
        if (t.config.playerCenter)
            center = camera.project(w.kino.x + w.kino.width * .5f, w.kino.y + w.kino.height * .5f);
        p.xy(program, "u_center", std::clamp(center.x / 320, 0.0f, 1.0f),
             std::clamp(center.y / 180, 0.0f, 1.0f));
        float color[4]{t.config.color[0], t.config.color[1], t.config.color[2], 1};
        p.Uniform4fv(p.GetUniformLocation(program, "u_color"), 1, color);
        p.quad(320, 180);
        p.UseProgram(0);
        ++drawCalls;
    }
    GLuint presented = p.composite.texture;
    if (antiAliasing == 1 || antiAliasing == 2) {
        p.BindFramebuffer(GL_FRAMEBUFFER, p.filtered.fbo);
        p.ortho(320, 180);
        glDisable(GL_BLEND);
        p.UseProgram(p.fxaa);
        p.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, p.composite.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p.i(p.fxaa, "u_image", 0);
        p.i(p.fxaa, "u_quality", antiAliasing);
        glColor4f(1, 1, 1, 1);
        p.quad(320, 180);
        p.UseProgram(0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        presented = p.filtered.texture;
        glEnable(GL_BLEND);
        ++drawCalls;
    }
    p.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, p.width, p.height);
    if (editing)
        glClearColor(editorDark ? .145f : .91f, editorDark ? .16f : .925f, editorDark ? .21f : .945f, 1);
    else
        glClearColor(.016f, .023f, .034f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    p.ortho(320, 180);
    glViewport(offsetX, p.height - offsetY - 180 * scale, 320 * scale, 180 * scale);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, presented);
    p.UseProgram(p.sampler);
    p.i(p.sampler, "u_image", 0);
    p.i(p.sampler, "u_mode", upscaleFilter);
    p.xy(p.sampler, "u_size", 320, 180);
    p.xy(p.sampler, "u_cell", 0, 0);
    p.f(p.sampler, "u_sharpness", renderSharpness);
    float bounds[4]{.5f / 320, .5f / 180, 1 - .5f / 320, 1 - .5f / 180};
    p.Uniform4fv(p.GetUniformLocation(p.sampler, "u_bounds"), 1, bounds);
    glColor4f(1, 1, 1, 1);
    p.quad(320, 180);
    glDisable(GL_TEXTURE_2D);
    p.UseProgram(0);
    ++drawCalls;
    if (camera.snapMode == SnapMode::Smooth) {
        // Optional visual-only crossfade with the adjacent camera grid. Canvas and
        // window scale remain integer-nearest; WorldState is never interpolated.
        float residue = std::clamp((camera.logicalX - camera.renderX) * pixelsPerUnit, -1.0f, 1.0f);
        if (std::abs(residue) > .01f) {
            glEnable(GL_TEXTURE_2D);
            p.UseProgram(p.sampler);
            glColor4f(1, 1, 1, std::abs(residue) * .5f);
            glPushMatrix();
            glTranslatef(residue > 0 ? -1 : 1, 0, 0);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0);
            glVertex2f(0, 180);
            glTexCoord2f(1, 0);
            glVertex2f(320, 180);
            glTexCoord2f(1, 1);
            glVertex2f(320, 0);
            glTexCoord2f(0, 1);
            glVertex2f(0, 0);
            glEnd();
            glPopMatrix();
            glDisable(GL_TEXTURE_2D);
            p.UseProgram(0);
            ++drawCalls;
        }
    }
    if (editing && editorGrid) {
        // Editor grid is an overlay in device pixels; never quantize WorldState.
        glColor4ub(92, 112, 145, 110);
        glLineWidth(1);
        glBegin(GL_LINES);
        for (int x = 0; x <= mapWidth; ++x) {
            auto v = camera.project(x * tileSize, 0);
            glVertex2f(v.x, 0);
            glVertex2f(v.x, 180);
        }
        for (int y = 0; y <= mapHeight; ++y) {
            auto v = camera.project(0, y * tileSize);
            glVertex2f(0, v.y);
            glVertex2f(320, v.y);
        }
        glEnd();
        glColor4f(1, 1, 1, 1);
        ++drawCalls;
    }
    if (!editing && p.dialogueVisible) {
        // Dialogue and scene share ONE presentation; no child-window GDI repaint races.
        p.ortho(320 * scale, 180 * scale);
        glViewport(offsetX, p.height - offsetY - 180 * scale, 320 * scale, 180 * scale);
        glDisable(GL_BLEND); // GDI raster is opaque; its unused alpha channel is not gameplay.
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, p.dialogueTexture);
        glColor4f(1, 1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);
        glVertex2i(p.dialogueX, p.dialogueY);
        glTexCoord2f(1, 0);
        glVertex2i(p.dialogueX + p.dialogueW, p.dialogueY);
        glTexCoord2f(1, 1);
        glVertex2i(p.dialogueX + p.dialogueW, p.dialogueY + p.dialogueH);
        glTexCoord2f(0, 1);
        glVertex2i(p.dialogueX, p.dialogueY + p.dialogueH);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        ++drawCalls;
    }
    SwapBuffers(p.dc);
}
void Renderer::hideDialogueOverlay() {
    p_->dialogueVisible = false;
}
void Renderer::setDialogueOverlay(const void* bgra, int width, int height, int x, int y) {
    if (!bgra || width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
        std::uint64_t(width) * height * 4 > 64ull * 1024 * 1024)
        throw std::runtime_error("Dialogue raster exceeds visual memory budget");
    auto& p = *p_;
    if (!p.dialogueTexture)
        glGenTextures(1, &p.dialogueTexture);
    glBindTexture(GL_TEXTURE_2D, p.dialogueTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (p.dialogueW != width || p.dialogueH != height)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    p.dialogueW = width;
    p.dialogueH = height;
    p.dialogueX = x;
    p.dialogueY = y;
    p.dialogueVisible = true;
}
bool Renderer::setVsync(bool enabled) {
    auto raw = wglGetProcAddress("wglSwapIntervalEXT");
    auto address = std::bit_cast<std::uintptr_t>(raw);
    if (address <= 3 || address == UINTPTR_MAX)
        return false;
    auto setter = std::bit_cast<BOOL(WINAPI*)(int)>(raw);
    return setter(enabled ? 1 : 0);
}
void Renderer::setSampling(int texture, int upscale, int aa, float sharpness) {
    if (texture < 0 || texture > 2 || upscale < 0 || upscale > 5 || aa < 0 || aa > 4 ||
        !std::isfinite(sharpness) || sharpness < 0 || sharpness > 1)
        throw std::runtime_error("Invalid sampling settings");
    auto& p = *p_;
    int factor = aa == 3 ? 2 : aa == 4 ? 4 : 1;
    if (p.sceneFactor != factor) {
        auto replacement = p.target(320 * factor, 180 * factor);
        p.DeleteFramebuffers(1, &p.scene.fbo);
        glDeleteTextures(1, &p.scene.texture);
        p.scene = replacement;
        p.sceneFactor = factor;
    }
    textureFilter = p.imageFilter = texture;
    upscaleFilter = upscale;
    antiAliasing = aa;
    renderSharpness = sharpness;
    p.BindFramebuffer(GL_FRAMEBUFFER, 0);
}
void Renderer::captureDisplay(const std::filesystem::path& file) {
    auto& p = *p_;
    p.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_FRONT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    int width = 320 * scale, height = 180 * scale;
    std::vector<unsigned char> rgb(std::size_t(width) * height * 3);
    glReadPixels(offsetX, p.height - offsetY - height, width, height, GL_RGB, GL_UNSIGNED_BYTE,
                 rgb.data());
    glReadBuffer(GL_BACK);
    std::ofstream out(file, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y)
        out.write(reinterpret_cast<const char*>(rgb.data() + std::size_t(y) * width * 3),
                  std::size_t(width) * 3);
    if (!out)
        throw std::runtime_error("Display capture write failed");
}
void Renderer::capture(const std::filesystem::path& file) {
    auto& p = *p_;
    p.BindFramebuffer(GL_FRAMEBUFFER,
                      (antiAliasing == 1 || antiAliasing == 2) ? p.filtered.fbo : p.composite.fbo);
    std::vector<unsigned char> rgb(320 * 180 * 3);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 320, 180, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    std::ofstream out(file, std::ios::binary);
    out << "P6\n320 180\n255\n";
    for (int y = 179; y >= 0; --y)
        out.write(reinterpret_cast<char*>(rgb.data() + y * 320 * 3), 320 * 3);
    p.BindFramebuffer(GL_FRAMEBUFFER, 0);
}
} // namespace echo
