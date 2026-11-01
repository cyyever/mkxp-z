#include "display/render/render-resources.h"
#include "display/gpu-device.h"
#include "display/gpu-frame.h"
#include "util/exception.h"
#include <SDL3/SDL.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
gpu::Device *activeDevice;
std::optional<gpu::TextureView> activeTarget;
TEX::ID primary;
bool useMipmaps = false;
int flushes = 0;
std::function<void()> onFlush;
SDL_FColor clearColor{0,0,1,1};
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F function) {
    try { function(); } catch (const Exception &error) {
        check(error.type == Exception::ArgumentError, "wrong resource exception type"); return;
    }
    throw std::runtime_error("invalid resource operation accepted");
}
}
namespace gpu {
Device &renderDevice() { return *activeDevice; }
void flushRenderCommands() { ++flushes;if(onFlush)onFlush(); }
void bindPrimaryTexture(TEX::ID value) { primary = std::move(value); }
void setRenderTarget(TextureView value) { activeTarget = value; }
void bindDefaultRenderTarget() { activeTarget.reset(); }
bool textureMipmapsEnabled() { return useMipmaps; }
void clearRenderTarget() {
    check(activeTarget.has_value(), "test target missing");
    CommandBuffer command(activeDevice->native());
    SDL_GPUColorTargetInfo target{};
    target.texture = activeTarget->native(); target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE; target.clear_color = clearColor;
    auto *pass = SDL_BeginGPURenderPass(command.native(), &target, 1, nullptr);
    check(pass != nullptr, "clear render pass");
    SDL_EndGPURenderPass(pass); command.submitAndWait();
}
}

static void cpuBuffers() {
    auto id = IBO::gen(); auto alias = id;
    check(id.valid() && !id.empty() && id.bytes().empty(), "buffer identity before allocation");
    IBO::bind(id);
    std::array<Uint16,3> indices{0,1,2};
    IBO::uploadData(std::as_bytes(std::span(indices)));
    check(alias.bytes().size()==sizeof(indices) && !std::memcmp(alias.bytes().data(),indices.data(),sizeof(indices)),
          "buffer upload through alias");
    const Uint16 replacement = 42;
    IBO::uploadSubData(sizeof(Uint16),sizeof(replacement),&replacement);
    indices[1]=42;
    check(!std::memcmp(alias.bytes().data(),indices.data(),sizeof(indices)),"buffer sub update");
    IBO::uploadSubData(sizeof(Uint16),sizeof(Uint16)*2,alias.bytes().data());
    indices={0,0,42};
    check(!std::memcmp(alias.bytes().data(),indices.data(),sizeof(indices)),"overlapping buffer update");
    rejects([&] { IBO::uploadSubData(std::numeric_limits<size_t>::max(),1,&replacement); });
    rejects([&] { IBO::uploadSubData(0,1,nullptr); });
    rejects([&] { IBO::allocEmpty(std::numeric_limits<size_t>::max()); });
    IBO::del(id);
    check(!alias.valid() && !alias.empty() && IBO::bound().empty(),"buffer aliases not invalidated");
    rejects([&] { (void)alias.bytes(); });
    rejects([&] { IBO::bind(alias); });
    IBO::del(alias);
    auto vertices=VBO::gen();VBO::bind(vertices);VBO::allocEmpty(0);
    check(vertices.valid()&&vertices.bytes().empty(),"empty CPU vertex buffer");
    VBO::del(vertices);
}
static void textures() {
    auto id=TEX::gen();auto alias=id;
    check(!id.empty() && bool(id) && !id.valid() && !id.native(),"texture identity before allocation");
    rejects([&] { (void)id.resource(); });
    TEX::bind(id);TEX::setRepeat(true);TEX::setSmooth(false);
    const std::vector<Uint8> original{1,2,3,255,17,33,65,127,91,15,43,0,
                                      255,0,9,255,0,254,3,255,7,8,9,1};
    std::array<Uint8,32> bgra;bgra.fill(99);
    for(size_t row=0;row<2;++row)for(size_t x=0;x<3;++x){
        const auto source=row*12+x*4,destination=row*16+x*4;
        bgra[destination]=original[source+2];bgra[destination+1]=original[source+1];
        bgra[destination+2]=original[source];bgra[destination+3]=original[source+3];
    }
    TEX::uploadImage(3,2,bgra.data(),SDL_PIXELFORMAT_BGRA32,16);
    check(alias.valid()&&alias.width()==3&&alias.height()==2&&alias.repeat()&&!alias.smooth(),"texture alias metadata");
    check(activeDevice->downloadRGBA(alias.resource())==original,"padded BGRA conversion changed RGBA/hidden RGB");
    const std::array<Uint8,8> rgb{200,100,50,88,25,35,45,66};
    TEX::uploadSubImage(1,0,1,2,rgb.data(),SDL_PIXELFORMAT_RGB24,4);
    auto expected=original;
    std::copy_n(rgb.begin(),3,expected.begin()+4);expected[7]=255;
    std::copy_n(rgb.begin()+4,3,expected.begin()+16);expected[19]=255;
    check(activeDevice->downloadRGBA(id.resource())==expected,"subregion upload changed untouched pixels");
    rejects([&] { TEX::uploadSubImage(2,1,2,1,rgb.data()); });
    rejects([&] { TEX::uploadSubImage(0,0,1,1,rgb.data(),SDL_PIXELFORMAT_RGB24,2); });
    check(activeDevice->downloadRGBA(id.resource())==expected,"rejected update changed texture");
    auto target=FBO::gen();auto targetAlias=target;FBO::bind(target);FBO::setTarget(id);
    check(activeTarget&&activeTarget->native()==id.native(),"FBO attachment mapping");
    auto *old=id.native();
    const std::array<Uint8,8> pixels{40,50,60,255,70,80,90,255};
    TEX::uploadImage(2,1,pixels.data());
    check(id==alias&&id.native()!=old&&alias.width()==2&&activeTarget->native()==id.native()&&activeTarget->width()==2,
          "reallocation did not update aliases and active target");
    check(activeDevice->downloadRGBA(id.resource())==std::vector<Uint8>(pixels.begin(),pixels.end()),"reallocated pixels");
    FBO::clear();
    check(activeDevice->downloadRGBA(id.resource())==std::vector<Uint8>({0,0,255,255,0,0,255,255}),"native FBO clear");
    // Upgrade a texture created before enabling mipmaps, retaining level zero.
    useMipmaps=true;TEX::setSmooth(true);
    check(alias.smooth()&&alias.mipmaps()&&alias.resource().mipLevels()==2,"mipmap allocation/generation");
    const auto low = activeDevice->downloadRGBA(id.resource(),1);
    if (low != std::vector<Uint8>({0,0,255,255})) {
        std::cerr << "mip actual:";for(auto value:low)std::cerr<<' '<<int(value);std::cerr<<'\n';
        const auto base=activeDevice->downloadRGBA(id.resource());std::cerr<<"base:";for(auto value:base)std::cerr<<' '<<int(value);std::cerr<<'\n';
    }
    check(low==std::vector<Uint8>({0,0,255,255}),"upgraded mip pixels");
    clearColor={0,1,0,1};FBO::clear();
    check(!alias.mipmaps(),"render target write did not mark mipmaps dirty");
    TEX::prepareSampling(alias);
    check(alias.mipmaps()&&activeDevice->downloadRGBA(id.resource(),1)==std::vector<Uint8>({0,255,0,255}),
          "mipmaps stale after render target clear");
    rejects([&] { FBO::setTarget(id,1); });
    auto secondary = FBO::gen();FBO::bind(secondary);FBO::setTarget(id);FBO::bind(target);
    const int beforeNonbound = flushes;FBO::del(secondary);
    check(flushes>beforeNonbound,"nonbound target release did not flush queued references");
    FBO::del(target);
    check(!targetAlias.valid()&&FBO::boundFramebufferID.empty(),"render target aliases not invalidated");
    const int before=flushes;
    onFlush=[&] {check(alias.native()!=nullptr,"texture invalidated before flushing queued commands");};
    TEX::del(id);onFlush={};
    check(flushes>before&&!alias.valid()&&!alias.native()&&!alias.empty()&&primary.empty(),"texture release did not flush/invalidate aliases");
    rejects([&] { TEX::bind(alias); });
    rejects([&] { (void)alias.resource(); });
    const int after=flushes;TEX::del(alias);check(flushes==after,"duplicate texture release");
    auto unallocated=TEX::gen();auto unallocatedAlias=unallocated;TEX::del(unallocated);
    check(!unallocatedAlias.valid()&&!bool(unallocatedAlias)&&!unallocatedAlias.empty(),"unallocated alias invalidation");
}
int main(){SDL_Window *window=nullptr;try{
    check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    window=SDL_CreateWindow("Native GPU resource adapter",64,64,SDL_WINDOW_HIDDEN);check(window,SDL_GetError());
    {gpu::Device device(window,SDL_GPU_SHADERFORMAT_DXBC|SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_MSL);
     struct BindingCleanup { ~BindingCleanup(){onFlush={};gpu_resources::resetContextBindings();primary={};activeTarget.reset();} } cleanup;
     activeDevice=&device;std::cout<<"GPU driver: "<<SDL_GetGPUDeviceDriver(device.native())<<'\n';
     cpuBuffers();textures();activeDevice=nullptr;}
    SDL_DestroyWindow(window);SDL_Quit();std::cout<<"Native texture/buffer/target resource tests passed\n";return 0;
}catch(const Exception&e){std::cerr<<e.msg.c_str()<<'\n';}catch(const std::exception&e){std::cerr<<e.what()<<'\n';}
if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;}
