#include "shader.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
const gpu::ShaderState *active;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void near(float value,float expected,const char *message){require(std::abs(value-expected)<.00001f,message);}
}
namespace gpu {
void bindShaderState(const ShaderState *state){active=state;}
std::array<int,2> shaderProjectionSize(bool scale){return scale?std::array{320,240}:std::array{640,480};}
}
int main(){
    try{
        ShaderSet shaders;
        shaders.sprite.bind();
        require(active==&shaders.sprite.gpuState(),"bind copied state instead of observing later setters");
        shaders.sprite.setOpacity(.6f);
        near(active->fragment.parameters[0],.6f,"post-bind opacity did not reach active state");
        shaders.sprite.applyViewportProj();
        near(active->vertex.projection[0],2.f/320,"hires logical projection lost");
        near(active->vertex.projection[5],-2.f/240,"native projection is not top-down");
        shaders.gray.applyViewportProj();
        near(shaders.gray.gpuState().vertex.projection[0],2.f/640,"gray incorrectly used logical hires dimensions");
        shaders.sprite.setTexSize(Vec2i(100,200));
        int a,b,c;
        TEX::ID first(reinterpret_cast<SDL_GPUTexture*>(&a)),second(reinterpret_cast<SDL_GPUTexture*>(&b)),third(reinterpret_cast<SDL_GPUTexture*>(&c));
        shaders.sprite.setPattern(first,Vec2(25,40));
        shaders.sprite.setPatternZoom(Vec2(2,4));
        shaders.sprite.setPatternScroll(Vec2(3,5));
        shaders.sprite.setPatternTile(true);
        const auto &effect=shaders.sprite.gpuState().effects;
        near(effect.patternScaleOffset[0],2,"tiled pattern UV x scale changed");
        near(effect.patternScaleOffset[1],1.25f,"tiled pattern UV y scale changed");
        near(effect.patternScaleOffset[2],-.48f,"tiled pattern scroll x changed");
        near(effect.patternScaleOffset[3],-.625f,"tiled pattern scroll y changed");
        shaders.sprite.setPatternTile(false);
        near(effect.patternScaleOffset[0],.5f,"non-tiled pattern zoom changed");
        near(effect.patternScaleOffset[2],-.12f,"non-tiled pattern scroll changed");
        shaders.sprite.setShouldRenderPattern(false);
        require(shaders.sprite.gpuState().textures[1].native()==nullptr,"disabled pattern retained old sampler handle");
        shaders.sprite.setBushDepth(true,false,.2f,.4f);
        shaders.sprite.setBushOpacity(.3f);
        near(effect.spriteFlags[3],1,"bush axis lost");near(effect.bush[3],0,"bush orientation lost");
        near(effect.bush[0],.2f,"bush slope lost");near(effect.bush[1],.4f,"bush intercept lost");
        shaders.trans.setCurrentScene(first);shaders.trans.setFrozenScene(second);shaders.trans.setTransMap(third);
        require(shaders.trans.gpuState().textures[0].native()==first.native()&&shaders.trans.gpuState().textures[1].native()==second.native(),"masked transition source ordering changed");
        shaders.simpleTrans.setCurrentScene(first);shaders.simpleTrans.setFrozenScene(second);
        require(shaders.simpleTrans.gpuState().textures[0].native()==second.native()&&shaders.simpleTrans.gpuState().textures[1].native()==first.native(),"simple transition old/new ordering changed");
        shaders.blur.pass1.setTexSize(Vec2i(10,20));shaders.blur.pass2.setTexSize(Vec2i(10,20));
        near(shaders.blur.pass1.gpuState().effects.blurDirection[0],.1f,"horizontal blur texel spacing lost");
        near(shaders.blur.pass2.gpuState().effects.blurDirection[1],.05f,"vertical blur texel spacing lost");
        shaders.bicubic.setSharpness(50);
        near(shaders.bicubic.gpuState().effects.filterParameters[0],.5f,"bicubic B mapping changed");
        near(shaders.bicubic.gpuState().effects.filterParameters[1],.25f,"bicubic C mapping changed");
        int frames[]={1,2,3,4,5,6,7};shaders.tilemap.setATFrames(frames);shaders.tilemap.setAniIndex(9);
        require(shaders.tilemap.gpuState().tilemap.autotileFrames1[2]==7&&shaders.tilemap.gpuState().tilemap.animation[0]==9,"autotile state packing changed");
        shaders.kglShadowV.setParams(1,2,3,true,true,40,50,20,25,.125,.25);
        const auto &shadow=shaders.kglShadowV.gpuState().effects;
        near(shadow.shadowBounds[3],1,"shadow wall flag lost");near(shadow.shadowSlopes[2],1,"shadow softness lost");
        near(shadow.shadowDimensions[2],20,"shadow center lost");
        Shader::unbind();require(active==nullptr,"shader unbind retained active state");
        std::cout<<"GPU ShaderSet adapter tests passed\n";return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
