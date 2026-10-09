/* On-device loader check. Does not initialize the emulator or load a game. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include "libretro.h"

int main(int argc, char **argv)
{
    static const char *entry_points[] = {
        "retro_init", "retro_deinit", "retro_api_version", "retro_get_system_info",
        "retro_get_system_av_info", "retro_set_environment", "retro_set_video_refresh",
        "retro_set_audio_sample", "retro_set_audio_sample_batch", "retro_set_input_poll",
        "retro_set_input_state", "retro_set_controller_port_device", "retro_reset",
        "retro_run", "retro_serialize_size", "retro_serialize", "retro_unserialize",
        "retro_cheat_reset", "retro_cheat_set", "retro_load_game", "retro_load_game_special",
        "retro_unload_game", "retro_get_region", "retro_get_memory_data", "retro_get_memory_size"
    };
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /absolute/path/to/core.so\n", argv[0]);
        return EXIT_FAILURE;
    }
    void *core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!core) {
        fprintf(stderr, "Android dlopen failed: %s\n", dlerror());
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < sizeof(entry_points) / sizeof(entry_points[0]); ++i) {
        dlerror();
        void *entry = dlsym(core, entry_points[i]);
        const char *error = dlerror();
        if (!entry || error) {
            fprintf(stderr, "Missing %s: %s\n", entry_points[i], error ? error : "null symbol");
            dlclose(core);
            return EXIT_FAILURE;
        }
    }
    unsigned (*api_version)(void) = (unsigned (*)(void))dlsym(core, "retro_api_version");
    void (*system_info)(struct retro_system_info *) =
        (void (*)(struct retro_system_info *))dlsym(core, "retro_get_system_info");
    if (api_version() != RETRO_API_VERSION) {
        fprintf(stderr, "Incompatible Libretro API version: %u\n", api_version());
        dlclose(core);
        return EXIT_FAILURE;
    }
    struct retro_system_info info = {0};
    system_info(&info);
    printf("Android dlopen and 25 Libretro symbols passed: %s %s, API %u\n",
           info.library_name ? info.library_name : "(unknown)",
           info.library_version ? info.library_version : "(unknown)", api_version());
    dlclose(core);
    return EXIT_SUCCESS;
}
