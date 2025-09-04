#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize TinyUSB MSC interface and mount FAT filesystem at /usb
esp_err_t usb_msc_fat_init(void);

// Check if a directory exists
bool fs_dir_exists(const char *path);

#ifdef __cplusplus
}
#endif

