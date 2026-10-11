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

// Screens selected through WRAM $40 and applied during V-blank. Animated
// screens cycle palette colour 1 every frame, like a moving game picture.
//  0 full picture             1 letterbox of black tiles   2 black side columns
//  3 small block on black     4 HDMA forced-blank letterbox 5 forced blank
//  6 screen 2 in pseudo hi-res 7 screen 4 interlaced
//  8 still logo: large solid block on black (no motion)
//  9 animated logo whose top edge is a narrow tip (outline, not a straight edge)
// 10 animated medium block on black (picture mostly dark)
// 11 forced-blank letterbox around a small block (mostly dark between the bars)
// 12 screen 1 standing still (a paused game)
// 13 screen 1 with dark grey bars (backdrop level 3 of 31, like Batman Returns)
// 14 screen 13 at brightness 12, where the grey bars turn dark
// 15 screen 10 on a dark grey background (logo on grey)
static std::vector<uint8_t> autocrop_rom()
{
    auto rom=diagnostic_rom(false);size_t pc=0;
    auto emit=[&](std::initializer_list<uint8_t> bytes){for(auto b:bytes)rom[pc++]=b;};
    auto write=[&](unsigned reg,unsigned val){emit({0xa9,(uint8_t)val,0x8d,(uint8_t)reg,(uint8_t)(reg>>8)});};
    auto dma=[&](unsigned source,unsigned size){
        write(0x4300,0x01);write(0x4301,0x18);write(0x4302,source&255);write(0x4303,source>>8);write(0x4304,0);
        write(0x4305,size&255);write(0x4306,size>>8);write(0x420b,0x01);};
    auto branch=[&](uint8_t opcode,size_t destination){emit({opcode,(uint8_t)(destination-(pc+2))});};
    auto absolute_x=[&](uint8_t opcode,unsigned offset){emit({opcode,(uint8_t)(offset&255),(uint8_t)(0x80+(offset>>8))});};
    // Data: tiles at $9000, seven 32x32 maps at $A000, HDMA table at $E000,
    // scene tables at $E100 and palette animation colours at $E200.
    std::fill(rom.begin()+0x1000,rom.begin()+0x6300,0);
    for(unsigned row=0;row<8;row++)rom[0x1010+row*2]=0xff; // tile 1: colour 1, tile 0: transparent
    auto map=[&](unsigned index,bool (*content)(unsigned,unsigned)){
        for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)rom[0x2000+index*0x800+(y*32+x)*2]=content(x,y)?1:0;};
    map(0,[](unsigned,unsigned){return true;});
    map(1,[](unsigned,unsigned y){return y>=3 && y<24;});
    map(2,[](unsigned x,unsigned){return x>=2 && x<30;});
    map(3,[](unsigned x,unsigned y){return y>=12 && y<15 && x>=8 && x<24;});
    map(4,[](unsigned x,unsigned y){return y>=4 && y<24 && x>=4 && x<28;});
    map(5,[](unsigned x,unsigned y){return (y>=4 && y<24 && x>=3 && x<29) || ((y==3 || y==24) && (x==15 || x==16));});
    map(6,[](unsigned x,unsigned y){return y>=6 && y<22 && x>=6 && x<26;});
    const uint8_t hdma[]={0x20,0x80,0x7f,0x0f,0x21,0x0f,0x01,0x80,0x00}; // black lines 0-31 and from 192
    std::memcpy(rom.data()+0x6000,hdma,sizeof(hdma));
    //                          0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
    const uint8_t maps[]   ={   0,   1,   2,   3,   0,   0,   2,   0,   4,   5,   6,   3,   1,   1,   1,   6};
    const uint8_t hdmaen[] ={   0,   0,   0,   0,   2,   0,   0,   2,   0,   0,   0,   2,   0,   0,   0,   0};
    const uint8_t inidisp[]={0x0f,0x0f,0x0f,0x0f,0x0f,0x80,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x0c,0x0f};
    const uint8_t setini[] ={   0,   0,   0,   0,   0,   0,   8,   1,   0,   0,   0,   0,   0,   0,   0,   0};
    const uint8_t animate[]={   1,   1,   1,   1,   1,   0,   1,   1,   0,   1,   1,   1,   0,   1,   1,   1};
    const uint16_t grey=3|(3<<5)|(3<<10);
    for(unsigned i=0;i<sizeof(maps);i++)
    {rom[0x6100+i]=(uint8_t)(0x04+maps[i]*4);rom[0x6110+i]=hdmaen[i];rom[0x6120+i]=inidisp[i];rom[0x6130+i]=setini[i];rom[0x6140+i]=animate[i];
     const uint16_t backdrop=i>=13?grey:0;rom[0x6150+i]=backdrop&255;rom[0x6160+i]=backdrop>>8;}
    const uint16_t colours[]={0x03e0,0x7c00,0x001f,0x7fff,0x03ff,0x7c1f,0x7fe0,0x4210};
    for(unsigned i=0;i<8;i++){rom[0x6200+i*2]=colours[i]&255;rom[0x6201+i*2]=colours[i]>>8;}

    emit({0x78,0xd8});write(0x2100,0x80);write(0x2115,0x80);
    write(0x2116,0);write(0x2117,0);dma(0x9000,32);
    write(0x2116,0);write(0x2117,0x04);dma(0xa000,0x3800);
    write(0x2121,0);write(0x2122,0);write(0x2122,0);write(0x2122,0xe0);write(0x2122,0x03); // black, green
    write(0x2105,0);write(0x2107,0x04);write(0x210b,0);write(0x212c,1);write(0x212d,1);
    write(0x4310,0);write(0x4311,0);write(0x4312,0);write(0x4313,0xe0);write(0x4314,0);
    emit({0xa9,0xff,0x85,0x41,0x64,0x40,0x64,0x42});write(0x2100,0x0f);
    const size_t wait_vblank=pc;
    emit({0xad,0x12,0x42});branch(0x10,wait_vblank);            // LDA $4212; BPL
    emit({0xa5,0x40,0xc5,0x41});const size_t same=pc;emit({0xf0,0}); // LDA $40; CMP $41; BEQ
    emit({0x85,0x41,0xaa});                                      // STA $41; TAX
    for(auto reg:{std::make_pair(0x6100u,0x2107u),std::make_pair(0x6110u,0x420cu),std::make_pair(0x6120u,0x2100u),std::make_pair(0x6130u,0x2133u)})
    {absolute_x(0xbd,reg.first);emit({0x8d,(uint8_t)reg.second,(uint8_t)(reg.second>>8)});}
    write(0x2121,0);absolute_x(0xbd,0x6150);emit({0x8d,0x22,0x21});absolute_x(0xbd,0x6160);emit({0x8d,0x22,0x21}); // backdrop colour
    rom[same+1]=(uint8_t)(pc-(same+2));
    emit({0xa6,0x41});absolute_x(0xbd,0x6140);const size_t still=pc;emit({0xf0,0}); // LDX $41; LDA animate,X; BEQ
    emit({0xe6,0x42});write(0x2121,1);emit({0xa5,0x42,0x29,0x07,0x0a,0xaa});        // INC $42; CGADD=1; X=(n&7)*2
    absolute_x(0xbd,0x6200);emit({0x8d,0x22,0x21});absolute_x(0xbd,0x6201);emit({0x8d,0x22,0x21});
    const size_t wait_end=pc;rom[still+1]=(uint8_t)(wait_end-(still+2));
    emit({0xad,0x12,0x42});branch(0x30,wait_end);               // LDA $4212; BMI
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
    const char* memory_file="./feature-autocrop.autocrop";std::remove(memory_file);
    uint8_t* wram=nullptr;
    auto load=[&](const char* mode){current.defaults["snes9x_auto_crop"]=mode;require(core.retro_load_game(&game),"Autocrop load");
        wram=static_cast<uint8_t*>(core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));};
    auto show=[&](unsigned scene,int frames){for(int i=0;i<frames;i++){wram[0x40]=scene;core.retro_run();}};
    // Frames until the bordered screen is shown cropped; with "instant", any frame showing the border fails.
    auto frames_to_crop=[&](unsigned scene,unsigned cropped_height,unsigned cropped_width,bool instant){
        for(int frame=1;frame<=600;frame++){show(scene,1);
            if(frame_height==cropped_height && frame_width==cropped_width)return frame;
            require(!instant || (content_rows()==frame_height && content_columns()==frame_width),"Border shown before a remembered crop");}
        throw std::runtime_error("Border never cropped");};
    auto never_cropped=[&](unsigned scene,int frames,const std::string& what){
        for(int i=0;i<frames;i++){show(scene,1);require(frame_width==256 && frame_height==224,what+" was cropped");}};
    auto saved_layouts=[&](){std::vector<std::vector<int>> layouts;std::FILE* file=std::fopen(memory_file,"r");if(!file)return layouts;
        char header[64]={};require(std::fgets(header,sizeof(header),file) && std::strcmp(header,"snes9x-autocrop 2\n")==0,"Layout memory file header");
        int t,b,l,r;while(std::fscanf(file,"%d %d %d %d",&t,&b,&l,&r)==4)layouts.push_back({t,b,l,r});std::fclose(file);return layouts;};
    const double full_aspect=4.0/3.0;

    load("disabled");
    show(4,6);const unsigned letterbox=content_rows();
    show(1,6);const unsigned tiles=content_rows();
    show(2,6);const unsigned columns=content_columns();
    require(frame_width==256 && frame_height==224,"Disabled option cropped the picture");
    require(letterbox>=158 && letterbox<=162 && tiles>=166 && tiles<=170 && tiles!=letterbox && columns==224,
        "Unexpected test borders "+std::to_string(letterbox)+"/"+std::to_string(tiles)+"/"+std::to_string(columns));
    core.retro_unload_game();
    std::puts("PASS AutoCrop disabled: picture unchanged");

    load("fit");
    show(0,10);require(frame_width==256 && frame_height==224,"Full screen was cropped");
    require(near(geometry.aspect_ratio,full_aspect) && geometry.base_height==224,"Full screen geometry");

    int frames=frames_to_crop(4,letterbox,256,false);require(frames<=4,"Hardware letterbox took "+std::to_string(frames)+" frames");
    require_tight("Hardware letterbox");
    require(geometry.base_height==letterbox && near(geometry.aspect_ratio,full_aspect*224.0/letterbox),"Fit geometry for letterbox");
    std::printf("PASS AutoCrop hardware letterbox 256x224 -> %ux%u in %d frames, aspect %.4f\n",frame_width,frame_height,frames,geometry.aspect_ratio);

    show(0,1);require(frame_width==256 && frame_height==224,"Graphics in the cropped area were not shown at once");
    require(near(geometry.aspect_ratio,full_aspect),"Geometry not restored");
    std::puts("PASS AutoCrop graphics in the cropped area restore the full picture in the same frame");

    show(0,31);require(frames_to_crop(4,letterbox,256,false)>1,"Layout reused before it was shown long enough");
    require(saved_layouts().empty(),"Layout saved before it was shown long enough");
    show(4,300);require(saved_layouts().size()==1,"Layout not saved after 5 seconds");
    show(0,31);frames=frames_to_crop(4,letterbox,256,true);show(4,70);
    std::printf("PASS AutoCrop layout saved after 5 s on screen and then cropped in the first frame (%d)\n",frames);

    show(0,31);frames=frames_to_crop(13,tiles,256,false);require(frames>85 && frames<=95,"Grey bars took "+std::to_string(frames)+" frames");
    show(14,40);require(frame_height==tiles,"Grey bars crop undone when the brightness dropped");
    show(13,40);require(frame_height==tiles,"Grey bars crop undone when the brightness returned");
    show(0,31);never_cropped(15,200,"Animated logo on a dark grey screen");
    std::printf("PASS AutoCrop dark grey bars cropped in %d frames and kept through brightness changes; logo on grey left alone\n",frames);
    show(0,31);never_cropped(12,120,"Unknown border on a still screen");
    frames=120+frames_to_crop(12,tiles,256,false);require(frames>=475 && frames<=485,"Still border took "+std::to_string(frames)+" frames");
    std::printf("PASS AutoCrop unknown border on a still screen cropped after %d frames (8 s)\n",frames);
    show(0,31);frames=frames_to_crop(1,tiles,256,false);
    require(frames>85 && frames<=95,"Pixel-only letterbox took "+std::to_string(frames)+" frames");require_tight("Tile letterbox");
    std::printf("PASS AutoCrop black-tile letterbox 256x224 -> %ux%u in %d frames with motion\n",frame_width,frame_height,frames);
    show(1,300);require(saved_layouts().size()==2,"Tile letterbox not saved");

    show(5,20);require(frame_height==tiles,"Black screen changed the crop");
    show(1,5);require(frame_height==tiles,"Crop lost after black screen");
    show(3,10);require(frame_height==tiles && content_rows()==24,"Dark screen moved the picture or hid graphics");
    std::puts("PASS AutoCrop black and dark screens keep the crop without hiding graphics");

    show(1,100);show(0,31);frames=frames_to_crop(12,tiles,256,true);show(12,70);
    std::printf("PASS AutoCrop saved layouts crop at once on still screens (%d)\n",frames);

    show(0,31);never_cropped(8,200,"Still logo on black");
    show(0,31);never_cropped(9,200,"Animated logo with an outline edge");
    show(0,31);never_cropped(10,200,"Animated logo on a mostly black screen");
    show(0,31);never_cropped(11,200,"Logo between forced-blank bars");
    show(0,31);never_cropped(3,200,"Small content on black");
    require(saved_layouts().size()==2,"A logo layout was saved");
    std::puts("PASS AutoCrop logo screens are never cropped or saved");

    show(0,31);frames=frames_to_crop(2,224,columns,false);require(frames>85 && frames<=95,"Side borders took "+std::to_string(frames)+" frames");
    require_tight("Side borders");
    require(geometry.base_width==224 && near(geometry.aspect_ratio,full_aspect*224.0/256.0),"Fit geometry for side borders");
    show(6,3);require(frame_width==448 && frame_height==224,"Hi-res side crop "+std::to_string(frame_width));require_tight("Hi-res side borders");
    std::printf("PASS AutoCrop side borders 256 -> %u, hi-res 512 -> 448\n",columns);

    show(7,2);require(frame_width==256,"Interlaced screen kept the side crop");
    show(7,40);require(frame_height==448-2*(224-letterbox),"Interlaced letterbox crop "+std::to_string(frame_height));require_tight("Interlaced letterbox");
    std::printf("PASS AutoCrop interlaced letterbox 256x448 -> %ux%u\n",frame_width,frame_height);

    current.defaults["snes9x_auto_crop"]="stretch";current.updated=true;
    show(4,3);require(frame_height==letterbox,"Stretch did not crop");show(4,70);
    require(geometry.base_height==letterbox && near(geometry.aspect_ratio,full_aspect),"Stretch must keep the full-frame aspect");
    show(2,2);require(frame_width==256 && frame_height==224,"Stretch kept the letterbox crop with graphics present");
    show(2,100);require(frame_width==224,"Stretch side crop");
    current.defaults["snes9x_blargg"]="composite";current.updated=true;
    show(2,2);require(frame_width==602-2*38 && frame_height==224,"Blargg side crop "+std::to_string(frame_width));
    std::printf("PASS AutoCrop stretch keeps aspect %.4f; Blargg NTSC 602 -> %u\n",geometry.aspect_ratio,frame_width);

    current.defaults["snes9x_auto_crop"]="disabled";current.updated=true;
    show(2,1);require(frame_width==602 && frame_height==224,"Disabling did not restore the picture");
    require(near(geometry.aspect_ratio,full_aspect) && geometry.base_width==256 && geometry.base_height==224,"Disabling did not restore geometry");
    current.defaults["snes9x_blargg"]="disabled";current.updated=true;core.retro_run();core.retro_unload_game();

    const auto saved=saved_layouts();require(saved.size()==2,"Saved layouts: "+std::to_string(saved.size()));
    for(const auto& layout:saved)require(layout[2]==0 && layout[3]==0,"Unexpected saved layout");
    load("fit");show(0,5);
    frames_to_crop(4,letterbox,256,true);show(4,70);show(0,31);frames_to_crop(1,tiles,256,true);show(1,70);
    show(0,31);require(frames_to_crop(2,224,columns,false)>85,"Side layout reused although it was never shown for 5 seconds");
    std::puts("PASS AutoCrop saved layouts crop in the first frame after reloading the game");

    show(0,31);frames_to_crop(4,letterbox,256,true);show(0,1);
    show(0,31);frames_to_crop(4,letterbox,256,true);show(0,1);
    show(0,31);require(frames_to_crop(4,letterbox,256,false)>1,"Contradicted layout still used at once");
    core.retro_unload_game();require(saved_layouts().size()==1,"Contradicted layout still saved");
    std::puts("PASS AutoCrop a saved layout contradicted twice in a row is forgotten");

    current.defaults["snes9x_overscan"]="disabled";load("fit");
    show(0,1);require(frame_height==239,"Uncropped overscan height");
    show(0,4);require(frame_height==224 && frame_width==256,"Overscan padding not cropped "+std::to_string(frame_height));require_tight("Overscan padding");
    std::puts("PASS AutoCrop removes the padding added when Crop Overscan is disabled");
    current.defaults["snes9x_overscan"]="enabled";current.defaults["snes9x_auto_crop"]="disabled";core.retro_unload_game();
    std::remove(memory_file);
}
// Busy loop counting in WRAM $20-$21: more CPU cycles per frame count further.
static std::vector<uint8_t> busy_rom()
{
    auto rom=diagnostic_rom(false);size_t pc=0;
    auto emit=[&](std::initializer_list<uint8_t> bytes){for(auto b:bytes)rom[pc++]=b;};
    emit({0x78,0xd8,0xa9,0x80,0x8d,0x00,0x21,0x64,0x20,0x64,0x21}); // SEI; CLD; forced blank; STZ $20/$21
    const size_t loop=pc;
    emit({0xe6,0x20,0xd0,0x02,0xe6,0x21,0x4c,(uint8_t)loop,(uint8_t)(0x80+(loop>>8))}); // INC $20; BNE +2; INC $21; JMP loop
    return rom;
}
static unsigned busy_count(Core& core,Frontend& current,const char* overclock)
{
    current.defaults["snes9x_overclock_cycles"]=overclock;
    const auto rom=busy_rom();retro_game_info game={"feature-overclock.sfc",rom.data(),rom.size(),nullptr};
    require(core.retro_load_game(&game),"Overclock load");for(int i=0;i<3;i++)core.retro_run();
    const auto* w=static_cast<uint8_t*>(core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    const unsigned start=w[0x20]|(unsigned(w[0x21])<<8);for(int i=0;i<4;i++)core.retro_run(); // stays below 65536
    const unsigned count=((w[0x20]|(unsigned(w[0x21])<<8))-start)&0xffff;core.retro_unload_game();
    current.defaults["snes9x_overclock_cycles"]="disabled";return count;
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
        {
            const unsigned normal=busy_count(core,current,"disabled"),light=busy_count(core,current,"light");
            const unsigned compatible=busy_count(core,current,"compatible"),fastest=busy_count(core,current,"max");
            require(normal>0 && light>=normal && compatible>light && fastest>compatible && fastest*10>=normal*18,
                "Reduce Slowdown has no effect: "+std::to_string(normal)+"/"+std::to_string(light)+"/"+std::to_string(compatible)+"/"+std::to_string(fastest));
            std::printf("PASS Reduce Slowdown CPU work per 4 frames: disabled %u, light %u, compatible %u, max %u\n",normal,light,compatible,fastest);
        }
        require(current.defaults.count("snes9x_auto_crop") && current.defaults["snes9x_auto_crop"]=="disabled","Auto crop option missing or not disabled by default");
        autocrop_test(core,current);
        std::puts("PASS features: native/HD rendering, live changes, MSU rates, auto crop and categories");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());std::remove("feature-msu.msu");return 1;}
}
