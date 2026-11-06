#pragma once
#include <storage/storage.h>
#include <string.h>

#define PMR_SETTINGS_DIR  "/ext/apps_data/pmr_radio"
#define PMR_SETTINGS_PATH PMR_SETTINGS_DIR "/settings.bin"

#include "pmr_preferences_codec.h"

static inline void pmr_preferences_load(Storage* storage, PmrPreferences* prefs) {
    *prefs = (PmrPreferences){9, 8, 1, 0};
    uint8_t data[9] = {0};
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, PMR_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    size_t size = opened ? storage_file_size(file) : 0;
    bool ok = (size == 8 || size == 9) && storage_file_read(file, data, size) == size;
    storage_file_close(file);
    storage_file_free(file);
    if(ok) pmr_preferences_decode(data, size, prefs);
}

static inline bool pmr_preferences_save(Storage* storage, const PmrPreferences* prefs) {
    storage_common_mkdir(storage, PMR_SETTINGS_DIR);
    uint8_t data[9];
    pmr_preferences_encode(prefs, data);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, PMR_SETTINGS_PATH ".tmp", FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              storage_file_write(file, data, sizeof(data)) == sizeof(data) &&
              storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    return ok &&
           storage_common_rename(storage, PMR_SETTINGS_PATH ".tmp", PMR_SETTINGS_PATH) == FSE_OK;
}
