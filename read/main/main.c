#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "read.h"   

// ====== USER CONFIG ======
#define PIN_NUM_MISO  2
#define PIN_NUM_MOSI  15
#define PIN_NUM_CLK   14
#define PIN_NUM_CS    13

#define MOUNT_POINT   "/sdcard/patterntable"
#define TAG           "LIGHTDANCE"

static void mount_sdcard(sdmmc_host_t *host, sdmmc_card_t **card, const char *mount_point) {
    esp_err_t ret;

    // Configure host
    *host = (sdmmc_host_t) SDSPI_HOST_DEFAULT();
    host->slot = SPI2_HOST;

    // Configure device (CS pin)
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host->slot;

    // SPI bus config
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // Init SPI bus
    ESP_ERROR_CHECK(spi_bus_initialize(host->slot, &bus_cfg, SDSPI_DEFAULT_DMA));

    // FATFS mount config
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // Mount SD card
    ret = esp_vfs_fat_sdspi_mount(mount_point, host, &slot_config, &mount_config, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card filesystem: %s", esp_err_to_name(ret));
        spi_bus_free(host->slot);
        *card = NULL;
        return;
    }

    sdmmc_card_print_info(stdout, *card);
    ESP_LOGI(TAG, "SD card mounted at %s", mount_point);
}

void app_main(void) {
    sdmmc_host_t host;
    sdmmc_card_t *card = NULL;

    mount_sdcard(&host, &card, MOUNT_POINT);

    if (card) {
        LightdanceReader reader;
        LightdanceReader_init(&reader, MOUNT_POINT);

        LightdanceReader_load_frame_times(&reader);
        LightdanceReader_index_frames(&reader);

        FrameData fd = LightdanceReader_read_frame_at(&reader, 0);
        ESP_LOGI(TAG, "First frame fade: %d", fd.fade);

        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        spi_bus_free(host.slot);
    }
}

