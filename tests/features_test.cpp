// Generated diagnostic ROMs exercise the real Libretro rendering and audio pipeline.
#define main region_test_main
#include "region_test.cpp"
#undef main
#include <algorithm>
#include <fstream>
#include <utility>

static unsigned frame_width, frame_height;
static std::vector<uint16_t> pixels;
static retro_game_geometry geometry;
static unsigned geometry_calls;
static bool RETRO_CALLCONV feature_environment(unsigned command, void *data)
{
    if (command == RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE) { *static_cast<int*>(data)=3; return true; }
    if (command == RETRO_ENVIRONMENT_SET_GEOMETRY) { geometry=*static_cast<const retro_game_geometry*>(data); geometry_calls++; return true; }
    if (command == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2)
    {
        const auto *options=static_cast<retro_core_options_v2*>(data);
        for (auto *option=options->definitions; option->key; option++)
        {
            frontend->defaults[option->key]=option->default_value;
            if (std::strstr(option->key,"mode7")) require(std::string(option->category_key)=="video","Mode7 category");
            if (std::strstr(option->key,"msu1")) require(std::string(option->category_key)=="audio","MSU category");
            if (std::strcmp(option->key,"snes9x_superfx_timing")==0) require(std::string(option->category_key)=="hacks","GSU category");
            if (std::strcmp(option->key,"snes9x_auto_crop")==0) require(std::string(option->category_key)=="video","Auto crop category");
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

// Eight screens selected through WRAM $40, applied during V-blank:
// 0 full picture, 1 letterbox drawn with black tiles, 2 black side columns,
// 3 small block on black, 4 letterbox made by HDMA forced blank, 5 forced
// blank (all black), 6 screen 2 in pseudo hi-res (512 pixels wide),
// 7 screen 4 interlaced (448 lines).
static std::vector<uint8_t> autocrop_rom()
{
    auto rom=diagnostic_rom(false);size_t pc=0;
    auto emit=[&](std::initializer_list<uint8_t> bytes){for(auto b:bytes)rom[pc++]=b;};
    auto write=[&](unsigned reg,unsigned val){emit({0xa9,(uint8_t)val,0x8d,(uint8_t)reg,(uint8_t)(reg>>8)});};
    auto dma=[&](unsigned source,unsigned size){
        write(0x4300,0x01);write(0x4301,0x18);write(0x4302,source&255);write(0x4303,source>>8);write(0x4304,0);
        write(0x4305,size&255);write(0x4306,size>>8);write(0x420b,0x01);};
    auto branch=[&](uint8_t opcode,size_t destination){emit({opcode,(uint8_t)(destination-(pc+2))});};
    // Data: tiles at $9000, four 32x32 maps at $A000, HDMA table at $C000, scene tables at $C100.
    std::fill(rom.begin()+0x1000,rom.begin()+0x4200,0);
    for(unsigned row=0;row<8;row++)rom[0x1010+row*2]=0xff; // tile 1: colour 1, tile 0: transparent
    auto map=[&](unsigned index,bool (*content)(unsigned,unsigned)){
        for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)rom[0x2000+index*0x800+(y*32+x)*2]=content(x,y)?1:0;};
    map(0,[](unsigned,unsigned){return true;});
    map(1,[](unsigned,unsigned y){return y>=4 && y<24;});
    map(2,[](unsigned x,unsigned){return x>=2 && x<30;});
    map(3,[](unsigned x,unsigned y){return y>=12 && y<15 && x>=8 && x<24;});
    const uint8_t hdma[]={0x20,0x80,0x7f,0x0f,0x21,0x0f,0x01,0x80,0x00}; // black lines 0-31 and from 192
    std::memcpy(rom.data()+0x4000,hdma,sizeof(hdma));
    const uint8_t bg1sc[]={0x04,0x08,0x0c,0x10,0x04,0x04,0x0c,0x04},hdmaen[]={0,0,0,0,0x02,0,0,0x02};
    const uint8_t inidisp[]={0x0f,0x0f,0x0f,0x0f,0x0f,0x80,0x0f,0x0f},setini[]={0,0,0,0,0,0,0x08,0x01};
    for(unsigned i=0;i<8;i++){rom[0x4100+i]=bg1sc[i];rom[0x4110+i]=hdmaen[i];rom[0x4120+i]=inidisp[i];rom[0x4130+i]=setini[i];}

    emit({0x78,0xd8});write(0x2100,0x80);write(0x2115,0x80);
    write(0x2116,0);write(0x2117,0);dma(0x9000,32);
    write(0x2116,0);write(0x2117,0x04);dma(0xa000,0x2000);
    write(0x2121,0);write(0x2122,0);write(0x2122,0);write(0x2122,0xe0);write(0x2122,0x03); // black, green
    write(0x2105,0);write(0x2107,0x04);write(0x210b,0);write(0x212c,1);write(0x212d,1);
    write(0x4310,0);write(0x4311,0);write(0x4312,0);write(0x4313,0xc0);write(0x4314,0);
    emit({0xa9,0xff,0x85,0x41,0x64,0x40});write(0x2100,0x0f);
    const size_t wait_vblank=pc;
    emit({0xad,0x12,0x42});branch(0x10,wait_vblank);           // LDA $4212; BPL
    emit({0xa5,0x40,0xc5,0x41});const size_t skip=pc;emit({0xf0,0}); // LDA $40; CMP $41; BEQ
    emit({0x85,0x41,0xaa});                                     // STA $41; TAX
    for(auto reg:{std::make_pair(0x4100u,0x2107u),std::make_pair(0x4110u,0x420cu),std::make_pair(0x4120u,0x2100u),std::make_pair(0x4130u,0x2133u)})
        emit({0xbd,(uint8_t)(reg.first&255),(uint8_t)(0x80+(reg.first>>8)),0x8d,(uint8_t)reg.second,(uint8_t)(reg.second>>8)});
    const size_t wait_end=pc;rom[skip+1]=(uint8_t)(wait_end-(skip+2));
    emit({0xad,0x12,0x42});branch(0x30,wait_end);              // LDA $4212; BMI
    branch(0x80,wait_vblank);
    return rom;
}
static bool dark(uint16_t p){return ((p>>11)&31)<=2 && ((p>>6)&31)<=2 && (p&31)<=2;}
static bool row_content(unsigned y){for(unsigned x=0;x<frame_width;x++)if(!dark(pixels[y*frame_width+x]))return true;return false;}
static bool column_content(unsigned x){for(unsigned y=0;y<frame_height;y++)if(!dark(pixels[y*frame_width+x]))return true;return false;}
static unsigned content_rows(){unsigned n=0;for(unsigned y=0;y<frame_height;y++)n+=row_content(y);return n;}
static unsigned content_columns(){unsigned n=0;for(unsigned x=0;x<frame_width;x++)n+=column_content(x);return n;}
static void require_tight(const std::string& what)
{
    require(row_content(0) && row_content(frame_height-1),what+": border rows left in the picture");
    require(column_content(0) && column_content(frame_width-1),what+": border columns left in the picture");
}
static bool near(double a,double b){return std::fabs(a-b)<1e-4;}
static void autocrop_test(Core& core,Frontend& current)
{
    const auto rom=autocrop_rom();retro_game_info game={"feature-autocrop.sfc",rom.data(),rom.size(),nullptr};
    uint8_t* wram=nullptr;
    auto show=[&](unsigned scene,int frames){for(int i=0;i<frames;i++){wram[0x40]=scene;core.retro_run();}};
    const double full_aspect=4.0/3.0;

    current.defaults["snes9x_auto_crop"]="disabled";
    require(core.retro_load_game(&game),"Autocrop load");wram=static_cast<uint8_t*>(core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    show(4,20);require(frame_width==256 && frame_height==224,"Disabled option cropped the picture");
    require(content_rows()<224,"HDMA letterbox did not draw black lines");core.retro_unload_game();
    std::puts("PASS AutoCrop disabled: picture unchanged");

    current.defaults["snes9x_auto_crop"]="fit";
    require(core.retro_load_game(&game),"Autocrop load");wram=static_cast<uint8_t*>(core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    show(0,10);require(frame_width==256 && frame_height==224,"Full screen was cropped");
    require(near(geometry.aspect_ratio,full_aspect) && geometry.base_height==224,"Full screen geometry");

    show(4,2);require(frame_height==224,"Hardware letterbox cropped too early");
    const unsigned letterbox=content_rows();require(letterbox>=158 && letterbox<=162,"Unexpected HDMA letterbox size "+std::to_string(letterbox));
    show(4,3);require(frame_height==224,"Hardware letterbox cropped before it was stable");
    show(4,5);require(frame_width==256 && frame_height==letterbox,"Hardware letterbox not cropped: "+std::to_string(frame_height));
    require_tight("Hardware letterbox");
    require(geometry.base_height==letterbox && near(geometry.aspect_ratio,full_aspect*224.0/letterbox),"Fit geometry for letterbox");
    std::printf("PASS AutoCrop hardware letterbox 256x224 -> %ux%u aspect %.4f\n",frame_width,frame_height,geometry.aspect_ratio);

    show(0,2);require(frame_width==256 && frame_height==224,"Picture returning to the border was not shown at once");
    require(near(geometry.aspect_ratio,full_aspect),"Geometry not restored");
    std::puts("PASS AutoCrop graphics in the cropped area restore the full picture immediately");

    show(1,2);const unsigned tiles=content_rows();require(frame_height==224 && tiles>=158 && tiles<=162,"Tile letterbox size");
    show(1,40);require(frame_height==224,"Pixel-only border cropped before it was stable");
    show(1,30);require(frame_height==tiles,"Tile letterbox not cropped");require_tight("Tile letterbox");
    std::printf("PASS AutoCrop black-tile letterbox 256x224 -> %ux%u after the stability period\n",frame_width,frame_height);

    show(5,20);require(frame_height==tiles,"Black screen changed the crop");
    show(1,5);require(frame_height==tiles,"Crop lost after black screen");
    std::puts("PASS AutoCrop black screens keep the current crop");

    show(3,2);require(frame_width==256 && frame_height==224,"Sparse screen kept a crop");
    show(3,90);require(frame_width==256 && frame_height==224,"Sparse screen was cropped");
    std::puts("PASS AutoCrop small content on black is never cropped");

    show(2,2);const unsigned columns=content_columns();require(frame_width==256 && columns==224,"Side border size "+std::to_string(columns));
    show(2,70);require(frame_width==columns && frame_height==224,"Side borders not cropped");require_tight("Side borders");
    require(geometry.base_width==224 && near(geometry.aspect_ratio,full_aspect*224.0/256.0),"Fit geometry for side borders");
    show(6,3);require(frame_width==448 && frame_height==224,"Hi-res side crop "+std::to_string(frame_width));require_tight("Hi-res side borders");
    std::printf("PASS AutoCrop side borders 256 -> %u, hi-res 512 -> 448\n",columns);

    show(7,2);require(frame_width==256,"Interlaced screen kept the side crop");
    show(7,10);require(frame_height==448-2*(224-letterbox),"Interlaced letterbox crop "+std::to_string(frame_height));require_tight("Interlaced letterbox");
    std::printf("PASS AutoCrop interlaced letterbox 256x448 -> %ux%u\n",frame_width,frame_height);

    current.defaults["snes9x_auto_crop"]="stretch";current.updated=true;
    show(4,12);require(frame_height==letterbox,"Stretch did not crop");
    require(geometry.base_height==letterbox && near(geometry.aspect_ratio,full_aspect),"Stretch must keep the full-frame aspect");
    show(2,3);require(frame_width==256,"Stretch kept side crop with graphics present");
    show(2,70);require(frame_width==224,"Stretch side crop");
    current.defaults["snes9x_blargg"]="composite";current.updated=true;
    show(2,2);require(frame_width==602-2*38 && frame_height==224,"Blargg side crop "+std::to_string(frame_width));
    std::printf("PASS AutoCrop stretch keeps aspect %.4f; Blargg NTSC 602 -> %u\n",geometry.aspect_ratio,frame_width);

    current.defaults["snes9x_auto_crop"]="disabled";current.updated=true;
    show(2,1);require(frame_width==602 && frame_height==224,"Disabling did not restore the picture");
    require(near(geometry.aspect_ratio,full_aspect) && geometry.base_width==256 && geometry.base_height==224,"Disabling did not restore geometry");
    current.defaults["snes9x_blargg"]="disabled";current.updated=true;core.retro_run();core.retro_unload_game();

    current.defaults["snes9x_auto_crop"]="fit";current.defaults["snes9x_overscan"]="disabled";
    require(core.retro_load_game(&game),"Overscan load");wram=static_cast<uint8_t*>(core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    show(0,2);require(frame_height==239,"Uncropped overscan height");
    show(0,10);require(frame_height==224 && frame_width==256,"Overscan padding not cropped "+std::to_string(frame_height));require_tight("Overscan padding");
    std::puts("PASS AutoCrop removes the padding added when Crop Overscan is disabled");
    current.defaults["snes9x_overscan"]="enabled";current.defaults["snes9x_auto_crop"]="disabled";core.retro_unload_game();
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
        require(current.defaults.count("snes9x_auto_crop") && current.defaults["snes9x_auto_crop"]=="disabled","Auto crop option missing or not disabled by default");
        autocrop_test(core,current);
        std::puts("PASS features: native/HD rendering, live changes, MSU rates, auto crop and categories");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());std::remove("feature-msu.msu");return 1;}
}
