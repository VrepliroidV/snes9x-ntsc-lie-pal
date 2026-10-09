// Generated diagnostic ROMs exercise the real Libretro rendering and audio pipeline.
#define main region_test_main
#include "region_test.cpp"
#undef main
#include <fstream>

static unsigned frame_width, frame_height;
static std::vector<uint16_t> pixels;
static bool RETRO_CALLCONV feature_environment(unsigned command, void *data)
{
    if (command == RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE) { *static_cast<int*>(data)=3; return true; }
    if (command == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2)
    {
        const auto *options=static_cast<retro_core_options_v2*>(data);
        for (auto *option=options->definitions; option->key; option++)
        {
            frontend->defaults[option->key]=option->default_value;
            if (std::strstr(option->key,"mode7")) require(std::string(option->category_key)=="video","Mode7 category");
            if (std::strstr(option->key,"msu1")) require(std::string(option->category_key)=="audio","MSU category");
            if (std::strcmp(option->key,"snes9x_superfx_timing")==0) require(std::string(option->category_key)=="hacks","GSU category");
        }
        return true;
    }
    return environment(command,data);
}
static void RETRO_CALLCONV capture(const void *data,unsigned width,unsigned height,size_t pitch)
{
    if (!data) return;
    frame_width=width;frame_height=height;pixels.resize(width*height);
    for (unsigned y=0;y<height;y++) std::memcpy(pixels.data()+y*width,static_cast<const uint8_t*>(data)+y*pitch,width*2);
}
static std::vector<uint8_t> mode7_rom()
{
    auto rom=diagnostic_rom(false);size_t pc=0;
    auto emit=[&](std::initializer_list<uint8_t> bytes){for(auto b:bytes)rom[pc++]=b;};
    auto write=[&](unsigned reg,unsigned val){emit({0xa9,(uint8_t)val,0x8d,(uint8_t)reg,(uint8_t)(reg>>8)});};
    emit({0x78,0xd8});write(0x2100,0x80);write(0x2105,7);write(0x212c,1);write(0x2115,0x80);
    write(0x2116,0);write(0x2117,0);
    for(unsigned i=0;i<64;i++)write(0x2119,1+(i%8));
    write(0x2121,0);
    for(unsigned i=0;i<256;i++){unsigned color=((i%8)*4)|(((i/8)%8)*4<<5)|((i%4)*8<<10);write(0x2122,color&255);write(0x2122,color>>8);}
    write(0x211b,0x80);write(0x211b,1); // A=384, genuine sub-pixel differences.
    write(0x211c,0);write(0x211c,0);write(0x211d,0);write(0x211d,0);
    write(0x211e,0);write(0x211e,1); // D=256.
    write(0x2100,15);emit({0x80,0xfe});
    return rom;
}
static std::vector<uint8_t> superfx_rom()
{
    auto rom=diagnostic_rom(false);rom[0x7fd6]=0x13;size_t pc=0;
    auto emit=[&](std::initializer_list<uint8_t> bytes){for(auto b:bytes)rom[pc++]=b;};
    auto write=[&](unsigned reg,unsigned val){emit({0xa9,(uint8_t)val,0x8d,(uint8_t)reg,(uint8_t)(reg>>8)});};
    emit({0x78,0xd8});write(0x2100,0x80);write(0x303a,0x18);write(0x3034,0x40);
    write(0x3039,1);write(0x301e,0);write(0x301f,0x10);
    // Read the running GSU accumulator through its CPU-visible register space.
    size_t loop=pc;emit({0xad,0x00,0x30,0x85,0x20,0xad,0x01,0x30,0x85,0x21,0x4c,(uint8_t)loop,(uint8_t)(0x80+(loop>>8))});
    rom[0x1000]=0xd0;rom[0x1001]=0x05;rom[0x1002]=0xfd;rom[0x1003]=0x01; // INC R0; BRA; NOP delay slot.
    return rom;
}
static unsigned gsu_counter(Core& core)
{
    auto* w=static_cast<uint8_t*>(core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));return w[0x20]|(unsigned(w[0x21])<<8);
}
int main(int argc,char**argv)
{
    if(argc!=2)return 2;
    try {
        Frontend current;current.options_version=2;current.language=RETRO_LANGUAGE_ENGLISH;frontend=&current;
        Core core(argv[1]);core.retro_set_environment(feature_environment);core.retro_set_video_refresh(capture);
        require(current.defaults.count("snes9x_mode7_hires") && current.defaults.count("snes9x_msu1_enhanced_audio") && current.defaults.count("snes9x_superfx_timing"),"Missing feature options");
        const auto rom=mode7_rom();retro_game_info game={"feature-mode7.sfc",rom.data(),rom.size(),nullptr};
        std::vector<uint16_t> native;
        for(const char*mode:{"disabled","2x","4x","2x_hv","4x_hv"})
            for(const char*filter:{"disabled","stable","smooth"})
            {
                current.defaults["snes9x_mode7_hires"]=mode;current.defaults["snes9x_mode7_hires_bilinear"]=filter;
                require(core.retro_load_game(&game),"M7 load");for(int i=0;i<4;i++)core.retro_run();
                unsigned scale=mode[0]=='4'?4:mode[0]=='2'?2:1;
                require(frame_width==256*scale,"M7 width "+std::string(mode));require(frame_height==(std::strstr(mode,"_hv")?448u:224u),"M7 height");
                bool visible=false;for(auto p:pixels)visible|=p!=0;require(visible,"Blank M7 frame");
                if(std::strcmp(mode,"disabled")==0 && std::strcmp(filter,"disabled")==0)native=pixels;
                if(scale>1 && std::strcmp(filter,"disabled")==0 && !std::strstr(mode,"_hv"))
                {bool differs=false;for(unsigned y=0;y<224;y++)for(unsigned x=0;x<256*scale;x++)differs|=pixels[y*256*scale+x]!=native[y*256+x/scale];require(differs,"M7 merely duplicates native pixels");}
                std::printf("PASS Mode7 %s filter=%s %ux%u\n",mode,filter,frame_width,frame_height);core.retro_unload_game();
            }
        current.defaults["snes9x_mode7_hires"]="4x_hv";
        require(core.retro_load_game(&game),"Live load");core.retro_run();
        current.defaults["snes9x_mode7_hires"]="disabled";current.updated=true;for(int i=0;i<3;i++)core.retro_run();require(frame_width==256 && frame_height==224,"Live M7 disable");core.retro_unload_game();
        // A real companion .msu makes detection travel through the normal file path.
        std::ofstream("feature-msu.msu",std::ios::binary).put('\0');game.path="feature-msu.sfc";
        for(const char*enhanced:{"enabled","disabled"})
        {
            current.defaults["snes9x_msu1_enhanced_audio"]=enhanced;require(core.retro_load_game(&game),"MSU load");
            retro_system_av_info av={};core.retro_get_system_av_info(&av);
            require(av.timing.sample_rate==(std::strcmp(enhanced,"enabled")==0?44100:32040),"MSU sample rate");
            for(int i=0;i<4;i++) { core.retro_run(); }
            std::printf("PASS MSU enhanced=%s rate=%.0f\n",enhanced,av.timing.sample_rate);core.retro_unload_game();
        }
        std::remove("feature-msu.msu");game.path="feature-mode7.sfc";current.defaults["snes9x_msu1_enhanced_audio"]="enabled";
        require(core.retro_load_game(&game),"Ordinary ROM reload");retro_system_av_info av={};core.retro_get_system_av_info(&av);require(av.timing.sample_rate==32040,"MSU rate leaked to ordinary content");core.retro_unload_game();
        const auto fx=superfx_rom();retro_game_info fx_game={"feature-superfx.sfc",fx.data(),fx.size(),nullptr};
        unsigned counts[2]={};int index=0;
        for(const char*timing:{"accurate","legacy"})
        {
            current.defaults["snes9x_superfx_timing"]=timing;
            require(core.retro_load_game(&fx_game),"GSU load");for(int i=0;i<3;i++)core.retro_run();
            unsigned start=gsu_counter(core);core.retro_run();counts[index++]=(gsu_counter(core)-start)&65535;
            require(counts[index-1]!=0,"GSU program did not execute");
            std::printf("PASS SuperFX timing=%s accumulator delta=%u\n",timing,counts[index-1]);core.retro_unload_game();
        }
        require(counts[0]!=counts[1],"GSU models have identical execution budgets");
        current.defaults["snes9x_superfx_timing"]="accurate";require(core.retro_load_game(&fx_game),"GSU live load");for(int i=0;i<3;i++)core.retro_run();
        current.defaults["snes9x_superfx_timing"]="legacy";current.updated=true;core.retro_run();
        unsigned before=gsu_counter(core);core.retro_run();require(((gsu_counter(core)-before)&65535)==counts[1],"Live GSU timing differs from legacy after reload");core.retro_unload_game();
        std::puts("PASS features: native/HD rendering, live changes, MSU rates and categories");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());std::remove("feature-msu.msu");return 1;}
}
