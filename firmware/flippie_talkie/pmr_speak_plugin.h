#pragma once
#include <stdint.h>

// Unpacked from the FAP by the loader, so an install is still a single file.
#define PMR_SPEAK_PLUGIN_ID   "flippie_speak"
#define PMR_SPEAK_PLUGIN_API  1U
#define PMR_SPEAK_PLUGIN_PATH APP_ASSETS_PATH("plugins/flippie_speak.fal")

typedef struct {
    int32_t (*run)(void* args);
} PmrSpeakPlugin;
