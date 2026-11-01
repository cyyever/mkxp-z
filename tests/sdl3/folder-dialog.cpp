#include "system/system.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
enum class Mode { Select, Cancel, Error, WorkerSelect, WorkerCancel, WorkerError, MainQueued, NoLabels };
Mode mode;
std::jthread worker;
SDL_Window *parent;
std::atomic<int> calls=0;
std::atomic<bool> queuedOnMain=false;
constexpr char title[]="选择游戏目录";
constexpr char accept[]="Load Game";
constexpr char path[]="C:/雨血/游戏";
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
void finish(SDL_DialogFileCallback callback,void *userdata,Mode which) {
    if(which==Mode::Error || which==Mode::WorkerError){SDL_SetError("callback-thread folder error");callback(userdata,nullptr,-1);}
    else if(which==Mode::Cancel || which==Mode::WorkerCancel || which==Mode::NoLabels){const char*files[]={nullptr};callback(userdata,files,-1);}
    else {char copy[sizeof(path)];std::memcpy(copy,path,sizeof(path));const char*files[]={copy,nullptr};callback(userdata,files,-1);std::memset(copy,'?',sizeof(copy));}
}
struct Pending { SDL_DialogFileCallback callback;void *userdata; };
void SDLCALL finishOnMain(void *data){auto*pending=static_cast<Pending*>(data);queuedOnMain=SDL_IsMainThread();finish(pending->callback,pending->userdata,Mode::Select);delete pending;}
}

// Only the test build remaps the SDL entrypoint to this provider. All properties,
// main-thread dispatch and event pumping below still use the actual SDL library.
extern "C" void SDLCALL TestShowFileDialogWithProperties(SDL_FileDialogType type,
    SDL_DialogFileCallback callback,void *userdata,SDL_PropertiesID properties) {
    ++calls;
    require(type==SDL_FILEDIALOG_OPENFOLDER,"not a native folder dialog request");
    require(SDL_GetPointerProperty(properties,SDL_PROP_FILE_DIALOG_WINDOW_POINTER,nullptr)==parent,"parent window lost");
    require(!SDL_GetBooleanProperty(properties,SDL_PROP_FILE_DIALOG_MANY_BOOLEAN,true),"multiple selection enabled");
    require(std::strcmp(SDL_GetStringProperty(properties,SDL_PROP_FILE_DIALOG_TITLE_STRING,"absent"),mode==Mode::NoLabels?"absent":title)==0,"title changed");
    require(std::strcmp(SDL_GetStringProperty(properties,SDL_PROP_FILE_DIALOG_ACCEPT_STRING,"absent"),mode==Mode::NoLabels?"absent":accept)==0,"accept label changed");
    if(mode==Mode::MainQueued){
        worker=std::jthread([=]{auto*p=new Pending{callback,userdata};if(!SDL_RunOnMainThread(finishOnMain,p,false)){delete p;callback(userdata,nullptr,-1);}});
    }else if(mode==Mode::WorkerSelect || mode==Mode::WorkerCancel || mode==Mode::WorkerError){
        worker=std::jthread([=,which=mode]{std::this_thread::sleep_for(std::chrono::milliseconds(1));finish(callback,userdata,which);});
    }else finish(callback,userdata,mode);
}

int main(){try{
    require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    parent=SDL_CreateWindow("Noninteractive folder dialog test",32,32,SDL_WINDOW_HIDDEN);
    require(parent!=nullptr,SDL_GetError());
    size_t completed=0;
    for(auto which:{Mode::Select,Mode::Cancel,Mode::Error,Mode::WorkerSelect,Mode::WorkerCancel,Mode::WorkerError,Mode::MainQueued,Mode::NoLabels}){
        mode=which;SDL_SetError("main-thread error must not replace callback error");
        const bool error=which==Mode::Error||which==Mode::WorkerError;
        const bool cancel=which==Mode::Cancel||which==Mode::WorkerCancel||which==Mode::NoLabels;
        bool failed=false;
        try {auto result=systemImpl::selectPath(parent,which==Mode::NoLabels?nullptr:title,which==Mode::NoLabels?nullptr:accept);require(!error,"dialog error was ignored");require(result==(cancel?"":path),"selection/cancellation changed UTF8 bytes");}
        catch(const std::runtime_error&e){require(error&&std::string(e.what())=="callback-thread folder error","wrong callback error or unexpected failure");failed=true;}
        require(failed==error,"unexpected error outcome");if(worker.joinable())worker.join();++completed;
    }
    require(queuedOnMain,"dialog completion never ran through real SDL main-thread event pump");
    const auto eventType=SDL_RegisterEvents(1);require(eventType!=Uint32(-1),SDL_GetError());SDL_Event event{};event.type=eventType;event.user.code=817;require(SDL_PushEvent(&event),SDL_GetError());mode=Mode::WorkerSelect;
    for(int i=0;i<64;i++){require(systemImpl::selectPath(parent,title,accept)==path,"repeated worker selection failed");worker.join();++completed;}
    SDL_Event retained{};require(SDL_PeepEvents(&retained,1,SDL_GETEVENT,eventType,eventType)==1&&retained.user.code==817,"startup event was consumed while waiting");
    const int before=calls;bool rejected=false;
    std::jthread other([&]{try{systemImpl::selectPath(parent,title,accept);}catch(const std::logic_error&){rejected=true;}});other.join();require(rejected&&calls==before,"worker-thread caller reached native dialog");
    SDL_DestroyWindow(parent);SDL_Quit();std::cout<<"PASS "<<completed<<" callback completions; UTF8/select/cancel/thread-errors/labels/main-pump/event-preservation/worker-rejection\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';if(worker.joinable())worker.join();SDL_Quit();return 1;}}
