/*
 * Libretro integration test using an original, generated 65816 diagnostic ROM.
 * No game dump, IPS patch, or emulator-private symbol is used.
 */
#include "libretro/libretro.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct Frontend
{
    unsigned options_version;
    unsigned language;
    unsigned registration = 0;
    bool updated = false;
    std::string region = "auto";
    std::map<std::string, std::string> defaults;
    std::vector<std::string> regions;
    std::vector<std::string> local_regions;
    std::string lie_label;
};

static Frontend *frontend;

static void read_options(const retro_core_option_definition *definitions, bool local)
{
    if (!definitions)
        return;
    for (const retro_core_option_definition *option = definitions; option->key; ++option)
    {
        if (!local && option->default_value)
            frontend->defaults[option->key] = option->default_value;
        if (std::strcmp(option->key, "snes9x_region") != 0)
            continue;
        std::vector<std::string> &values = local ? frontend->local_regions : frontend->regions;
        for (const retro_core_option_value *value = option->values; value->value; ++value)
        {
            values.push_back(value->value);
            if (!local && std::strcmp(value->value, "ntsc_lie_pal") == 0 && value->label)
                frontend->lie_label = value->label;
        }
    }
}

static bool RETRO_CALLCONV environment(unsigned command, void *data)
{
    switch (command)
    {
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            *static_cast<unsigned *>(data) = frontend->options_version;
            return true;
        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            *static_cast<unsigned *>(data) = frontend->language;
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        {
            const retro_core_options_intl *options = static_cast<retro_core_options_intl *>(data);
            frontend->registration = command;
            read_options(options->us, false);
            read_options(options->local, true);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
            frontend->registration = command;
            read_options(static_cast<retro_core_option_definition *>(data), false);
            return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        {
            frontend->registration = command;
            for (const retro_variable *variable = static_cast<retro_variable *>(data); variable->key; ++variable)
            {
                const std::string text(variable->value);
                const size_t separator = text.find("; ");
                require(separator != std::string::npos, "Invalid legacy option definition");
                const std::string values = text.substr(separator + 2);
                frontend->defaults[variable->key] = values.substr(0, values.find('|'));
                if (std::strcmp(variable->key, "snes9x_region") == 0)
                {
                    size_t start = 0;
                    for (;;)
                    {
                        const size_t end = values.find('|', start);
                        frontend->regions.push_back(values.substr(start, end - start));
                        if (end == std::string::npos)
                            break;
                        start = end + 1;
                    }
                }
            }
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            retro_variable *variable = static_cast<retro_variable *>(data);
            if (std::strcmp(variable->key, "snes9x_region") == 0)
                variable->value = frontend->region.c_str();
            else
            {
                const auto found = frontend->defaults.find(variable->key);
                if (found == frontend->defaults.end())
                    return false;
                variable->value = found->second.c_str();
            }
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool *>(data) = frontend->updated;
            frontend->updated = false;
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *static_cast<const char **>(data) = ".";
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return *static_cast<retro_pixel_format *>(data) == RETRO_PIXEL_FORMAT_RGB565;
        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
            *static_cast<int *>(data) = 0;
            return true;
        default:
            return false;
    }
}

static void RETRO_CALLCONV video(const void *, unsigned, unsigned, size_t) {}
static void RETRO_CALLCONV audio_sample(int16_t, int16_t) {}
static size_t RETRO_CALLCONV audio_batch(const int16_t *, size_t frames) { return frames; }
static void RETRO_CALLCONV input_poll() {}
static int16_t RETRO_CALLCONV input_state(unsigned, unsigned, unsigned, unsigned) { return 0; }

struct Core
{
    void *library;
#define CORE_FUNCTION(name) decltype(&::name) name = nullptr
    CORE_FUNCTION(retro_set_environment);
    CORE_FUNCTION(retro_set_video_refresh);
    CORE_FUNCTION(retro_set_audio_sample);
    CORE_FUNCTION(retro_set_audio_sample_batch);
    CORE_FUNCTION(retro_set_input_poll);
    CORE_FUNCTION(retro_set_input_state);
    CORE_FUNCTION(retro_api_version);
    CORE_FUNCTION(retro_init);
    CORE_FUNCTION(retro_deinit);
    CORE_FUNCTION(retro_load_game);
    CORE_FUNCTION(retro_unload_game);
    CORE_FUNCTION(retro_run);
    CORE_FUNCTION(retro_reset);
    CORE_FUNCTION(retro_get_region);
    CORE_FUNCTION(retro_get_system_av_info);
    CORE_FUNCTION(retro_get_memory_data);
    CORE_FUNCTION(retro_get_memory_size);
    CORE_FUNCTION(retro_serialize_size);
    CORE_FUNCTION(retro_serialize);
    CORE_FUNCTION(retro_unserialize);
#undef CORE_FUNCTION

    explicit Core(const char *path)
    {
        library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!library)
            throw std::runtime_error(std::string("dlopen failed: ") + dlerror());
#define LOAD_FUNCTION(name) \
        name = reinterpret_cast<decltype(name)>(dlsym(library, #name)); \
        require(name != nullptr, "Missing Libretro export: " #name)
        LOAD_FUNCTION(retro_set_environment);
        LOAD_FUNCTION(retro_set_video_refresh);
        LOAD_FUNCTION(retro_set_audio_sample);
        LOAD_FUNCTION(retro_set_audio_sample_batch);
        LOAD_FUNCTION(retro_set_input_poll);
        LOAD_FUNCTION(retro_set_input_state);
        LOAD_FUNCTION(retro_api_version);
        LOAD_FUNCTION(retro_init);
        LOAD_FUNCTION(retro_deinit);
        LOAD_FUNCTION(retro_load_game);
        LOAD_FUNCTION(retro_unload_game);
        LOAD_FUNCTION(retro_run);
        LOAD_FUNCTION(retro_reset);
        LOAD_FUNCTION(retro_get_region);
        LOAD_FUNCTION(retro_get_system_av_info);
        LOAD_FUNCTION(retro_get_memory_data);
        LOAD_FUNCTION(retro_get_memory_size);
        LOAD_FUNCTION(retro_serialize_size);
        LOAD_FUNCTION(retro_serialize);
        LOAD_FUNCTION(retro_unserialize);
#undef LOAD_FUNCTION
        require(retro_api_version() == RETRO_API_VERSION, "Unexpected Libretro ABI version");
        retro_set_environment(environment);
        retro_set_video_refresh(video);
        retro_set_audio_sample(audio_sample);
        retro_set_audio_sample_batch(audio_batch);
        retro_set_input_poll(input_poll);
        retro_set_input_state(input_state);
        retro_init();
    }

    ~Core()
    {
        retro_deinit();
        dlclose(library);
    }
};

static std::vector<uint8_t> diagnostic_rom(bool pal_header)
{
    // 128 KiB LoROM, CPU starts at $00:8000 in emulation mode.
    std::vector<uint8_t> rom(0x20000, 0xea);
    size_t pc = 0;
    const auto emit = [&](std::initializer_list<uint8_t> bytes) {
        for (const uint8_t byte : bytes)
            rom[pc++] = byte;
    };
    const auto branch = [&](uint8_t opcode) {
        emit({opcode, 0});
        return pc - 1;
    };
    const auto target = [&](size_t operand, size_t destination) {
        const ptrdiff_t offset = static_cast<ptrdiff_t>(destination) - static_cast<ptrdiff_t>(operand + 1);
        require(offset >= -128 && offset <= 127, "Diagnostic branch outside 8-bit range");
        rom[operand] = static_cast<uint8_t>(offset);
    };
    emit({0x78, 0xd8});                          // SEI; CLD
    emit({0xa9, 0x80, 0x8d, 0x00, 0x21});        // LDA #$80; STA $2100 (forced blank)
    emit({0xa9, 0xff, 0x8d, 0x01, 0x42});        // Enable counter latching via WRIO bit 7.
    emit({0x64, 0x12, 0x64, 0x13});              // STZ max V-counter low/high in mirrored WRAM.
    const size_t loop = pc;
    emit({0xad, 0x3f, 0x21, 0x8d, 0x00, 0x00}); // LDA $213F; STA $0000 (observed STAT78).
    emit({0xa9, 0xa5, 0x8d, 0x01, 0x00});        // Program-running marker.
    emit({0xad, 0x37, 0x21});                    // Latch actual emulated H/V counters.
    emit({0xad, 0x3d, 0x21, 0x85, 0x10});        // Read V-counter low byte.
    emit({0xad, 0x3d, 0x21, 0x29, 0x01, 0x85, 0x11}); // Read/mask high byte.
    emit({0xc5, 0x13});                          // CMP max high
    const size_t smaller_high = branch(0x90);    // BCC loop
    const size_t larger_high = branch(0xd0);     // BNE update
    emit({0xa5, 0x10, 0xc5, 0x12});              // LDA current low; CMP max low
    const size_t smaller_low = branch(0x90);     // BCC loop
    const size_t update = pc;
    emit({0xa5, 0x10, 0x85, 0x12, 0xa5, 0x11, 0x85, 0x13});
    const size_t again = branch(0x80);           // BRA loop
    target(smaller_high, loop);
    target(larger_high, update);
    target(smaller_low, loop);
    target(again, loop);

    std::memset(rom.data() + 0x7fb0, ' ', 0x30);
    const char title[] = "REGION DIAGNOSTIC";
    std::memcpy(rom.data() + 0x7fc0, title, sizeof(title) - 1);
    rom[0x7fd5] = 0x20;                          // Slow LoROM.
    rom[0x7fd6] = 0x00;                          // ROM only.
    rom[0x7fd7] = 0x07;                          // 128 KiB (smallest safe size for header scoring).
    rom[0x7fd8] = 0x00;                          // No SRAM.
    rom[0x7fd9] = pal_header ? 0x02 : 0x01;      // Europe / USA region header.
    rom[0x7fda] = 0x01;
    rom[0x7fdb] = 0x00;
    // All interrupt/reset vectors point at the diagnostic entry point.
    for (size_t address = 0x7fe0; address < 0x8000; address += 2)
    {
        rom[address] = 0x00;
        rom[address + 1] = 0x80;
    }
    uint32_t checksum = 0x01fe;
    for (size_t address = 0; address < rom.size(); ++address)
        if (address < 0x7fdc || address > 0x7fdf)
            checksum += rom[address];
    const uint16_t sum = static_cast<uint16_t>(checksum);
    const uint16_t complement = static_cast<uint16_t>(~sum);
    rom[0x7fdc] = complement & 0xff;
    rom[0x7fdd] = complement >> 8;
    rom[0x7fde] = sum & 0xff;
    rom[0x7fdf] = sum >> 8;
    return rom;
}

struct Case
{
    const char *option;
    bool pal_header;
    bool pal_timing;
    bool report_pal;
};

static void check_execution(Core &core, const Case &test, const std::string &phase)
{
    for (unsigned frame = 0; frame < 3; ++frame)
        core.retro_run();
    const uint8_t *wram = static_cast<uint8_t *>(core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    require(wram && core.retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM) == 0x20000, phase + ": missing WRAM");
    require(wram[1] == 0xa5, phase + ": diagnostic ROM did not execute");
    require(bool(wram[0] & 0x10) == test.report_pal, phase + ": STAT78 PAL bit mismatch");
    const unsigned max_v = wram[0x12] | (unsigned(wram[0x13]) << 8);
    require(max_v == (test.pal_timing ? 311u : 261u), phase + ": actual V-counter timing mismatch (" + std::to_string(max_v) + ")");
    require(core.retro_get_region() == (test.pal_timing ? RETRO_REGION_PAL : RETRO_REGION_NTSC), phase + ": retro_get_region mismatch");
    retro_system_av_info av = {};
    core.retro_get_system_av_info(&av);
    const double expected_fps = test.pal_timing ? 21281370.0 / 425568.0 : 21477272.0 / 357366.0;
    require(std::abs(av.timing.fps - expected_fps) < 1e-8, phase + ": frontend FPS mismatch");
}

static void run_case(Core &core, const Case &test)
{
    frontend->region = test.option;
    const std::vector<uint8_t> rom = diagnostic_rom(test.pal_header);
    const retro_game_info game = {test.pal_header ? "region-pal.sfc" : "region-ntsc.sfc", rom.data(), rom.size(), nullptr};
    const std::string name = std::string(test.option) + " / " + (test.pal_header ? "PAL" : "NTSC") + " header";
    require(core.retro_load_game(&game), name + ": ROM load failed");
    check_execution(core, test, name + " initial load");
    core.retro_reset();
    check_execution(core, test, name + " reset");
    std::vector<uint8_t> state(core.retro_serialize_size());
    require(!state.empty() && core.retro_serialize(state.data(), state.size()), name + ": serialize failed");
    uint8_t *wram = static_cast<uint8_t *>(core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    wram[1] = 0;
    require(core.retro_unserialize(state.data(), state.size()), name + ": unserialize failed");
    require(wram[1] == 0xa5, name + ": save state did not restore WRAM");
    check_execution(core, test, name + " save state restore");
    const Case switched = {std::strcmp(test.option, "ntsc_lie_pal") == 0 ? "ntsc" : "ntsc_lie_pal",
        test.pal_header, false, std::strcmp(test.option, "ntsc_lie_pal") != 0};
    frontend->region = switched.option;
    frontend->updated = true;
    check_execution(core, test, name + " live option change must wait for content reload");
    require(!frontend->updated, name + ": core did not consume variable update");
    core.retro_unload_game();
    require(core.retro_load_game(&game), name + ": reload after option change failed");
    check_execution(core, switched, name + " changed option applies on content reload");
    core.retro_unload_game();
    std::printf("PASS API=%u language=%u %-28s timing=%s STAT78=%s reset/state/reload\n",
        frontend->options_version, frontend->language, name.c_str(), test.pal_timing ? "PAL" : "NTSC", test.report_pal ? "PAL" : "NTSC");
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: %s /absolute/path/to/snes9x_libretro.so\n", argv[0]);
        return EXIT_FAILURE;
    }
    try
    {
        const std::vector<std::string> expected_options = {"auto", "ntsc", "pal", "ntsc_lie_pal"};
        // Deliberately alternate modes on the same loaded library: catches stale flag state.
        const Case cases[] = {
            {"auto", false, false, false}, {"auto", true, true, true},
            {"ntsc_lie_pal", true, false, true}, {"ntsc", true, false, false},
            {"ntsc_lie_pal", false, false, true}, {"pal", false, true, true},
            {"ntsc", false, false, false}, {"pal", true, true, true},
            {"ntsc_lie_pal", true, false, true}, {"auto", false, false, false},
            {"ntsc_lie_pal", false, false, true}, {"auto", true, true, true},
        };
        for (const unsigned api : {2u, 1u, 0u})
        {
            for (const unsigned language : {unsigned(RETRO_LANGUAGE_ENGLISH), unsigned(RETRO_LANGUAGE_TURKISH)})
            {
                Frontend current;
                current.options_version = api;
                current.language = language;
                frontend = &current;
                Core core(argv[1]);
                require(current.regions == expected_options, "Registered region choices/order mismatch");
                require(current.defaults["snes9x_region"] == "auto", "Region default changed");
                if (api >= 1)
                {
                    // This upstream revision serves the v1 interface even to v2 frontends.
                    require(current.registration == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL ||
                        current.registration == RETRO_ENVIRONMENT_SET_CORE_OPTIONS, "Unexpected option API registration");
                    require(current.lie_label == "NTSC (Lie to PAL)", "New option display label mismatch");
                    if (language == RETRO_LANGUAGE_TURKISH && current.registration == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL)
                        require(current.local_regions == expected_options, "Turkish region values differ from English");
                }
                else
                    require(current.registration == RETRO_ENVIRONMENT_SET_VARIABLES, "Legacy options were not registered");
                for (const Case &test : cases)
                    run_case(core, test);
            }
        }
        std::puts("PASS: 144 ROM loads, STAT78 bit 4, actual NTSC/PAL scanline timing, FPS, reset, save-state restore, live changes deferred until reload; frontend option APIs 2/1/legacy and English/Turkish.");
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
