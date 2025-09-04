#include "usb_msc_fat.h"

#include "esp_vfs_fat.h"
#include "tinyusb.h"
#include "class/msc/msc_device.h"
#include "wear_levelling.h"
#include <dirent.h>

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

esp_err_t usb_msc_fat_init(void)
{
    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false
    };
    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        return err;
    }

    // Mount FAT filesystem in internal flash
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };
    err = esp_vfs_fat_spiflash_mount("/usb", "storage", &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Initialize MSC to expose the mounted filesystem over USB
    const tinyusb_msc_storage_t msc_cfg = {
        .lun = 0,
        .vendor_id = "GlitchES",
        .product_id = "ROMFS",
        .product_rev = "1.0",
        .disk = {
            .pdrv = s_wl_handle,
            .block_count = 0, // auto detect
            .block_size = 512
        }
    };
    err = tinyusb_msc_storage_init(&msc_cfg);
    if (err != ESP_OK) {
        return err;
    }

    tinyusb_msc_set_ready(true);
    return ESP_OK;
}

bool fs_dir_exists(const char *path)
{
    DIR *dir = opendir(path);
    if (dir) {
        closedir(dir);
        return true;
    }
    return false;
}

