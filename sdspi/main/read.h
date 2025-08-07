// main/lightdance_reader.c

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
    
#define MOUNT_POINT "/sdcard"
#define TAG "LIGHTDANCE"

// SPI Pins
#define PIN_NUM_MISO GPIO_NUM_2
#define PIN_NUM_MOSI GPIO_NUM_15
#define PIN_NUM_CLK  GPIO_NUM_14
#define PIN_NUM_CS   GPIO_NUM_13

#define MAX_PARTS 32
#define MAX_COLOR_VALUES 4096
#define MAX_FRAMES 2048

static FILE *data_fp = NULL;
static int total_parts;
static int part_lengths[MAX_PARTS];
static int total_frames = 0;
static int fps = 30;
static uint32_t frame_times[MAX_FRAMES];
static size_t frame_offsets[MAX_FRAMES];

typedef struct {
    bool fade;
    uint8_t colors[MAX_COLOR_VALUES][4]; // max 1024 parts * 1 RGBA
} FrameData;

void mount_sdcard() {
    esp_err_t ret;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA));
    sdmmc_card_t *card;
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card filesystem");
        return;
    }

    ESP_LOGI(TAG, "SD card mounted");
}

void load_frame_times() {
    char path[64];
    snprintf(path, sizeof(path), MOUNT_POINT"/frame_times.txt");
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open frame_times.txt");
        return;
    }

    char line[32];
    while (fgets(line, sizeof(line), f)) {
        frame_times[total_frames++] = atoi(line);
    }
    fclose(f);
    ESP_LOGI(TAG, "Loaded %d frame times", total_frames);
}

void index_lightdance_frames() {
    char path[64];
    snprintf(path, sizeof(path), MOUNT_POINT"/lightdance_data.txt");

    data_fp = fopen(path, "r");
    if (!data_fp) {
        ESP_LOGE(TAG, "Failed to open lightdance_data.txt");
        return;
    }

    char line[256];

    // Read total_parts
    fgets(line, sizeof(line), data_fp);
    total_parts = atoi(line);

    // Read part lengths
    fgets(line, sizeof(line), data_fp);
    char *token = strtok(line, " ");
    int i = 0;
    while (token && i < MAX_PARTS) {
        part_lengths[i++] = atoi(token);
        token = strtok(NULL, " ");
    }

    // Read FPS
    fgets(line, sizeof(line), data_fp);
    fps = atoi(line);

    // Index frame offsets
    int frame_idx = 0;
    while (!feof(data_fp) && frame_idx < MAX_FRAMES) {
        long offset = ftell(data_fp);  // save frame start
        frame_offsets[frame_idx++] = offset;

        // Read fade line
        if (!fgets(line, sizeof(line), data_fp)) break;

        // Skip all RGBA lines
        for (int p = 0; p < total_parts; ++p) {
            int len = part_lengths[p];
            for (int j = 0; j < len; ++j) {
                if (!fgets(line, sizeof(line), data_fp)) break;
            }
        }
    }

    total_frames = frame_idx;
    ESP_LOGI(TAG, "Indexed %d frames with offsets", total_frames);
}


FrameData read_frame_at(int frame_index) {
    FrameData fd = {0};
    if (!data_fp || frame_index >= total_frames) return fd;

    fseek(data_fp, frame_offsets[frame_index], SEEK_SET);

    char line[256];
    fgets(line, sizeof(line), data_fp);
    fd.fade = strstr(line, "true") != NULL;

    int c = 0;
    for (int p = 0; p < total_parts; p++) {
        int len = part_lengths[p];
        for (int j = 0; j < len; j++) {
            if (!fgets(line, sizeof(line), data_fp)) break;
            sscanf(line, "%hhu %hhu %hhu %hhu",
                   &fd.colors[c][0],
                   &fd.colors[c][1],
                   &fd.colors[c][2],
                   &fd.colors[c][3]);
            c++;
        }
    }

    return fd;
}

FrameData getNextFrameInfo(int currentIndex) {
    if (currentIndex + 1 >= total_frames) return (FrameData){0};
    return read_frame_at(currentIndex + 1);
}

FrameData getKthFrameOfLED(int led_index, int k) {
    int count = 0;
    for (int i = 0; i < total_frames; ++i) {
        FrameData fd = read_frame_at(i);
        if (fd.colors[led_index][3] > 0) { // alpha > 0
            if (count++ == k) return fd;
        }
    }
    return (FrameData){0};
}

int getFrameIndexAtTime(uint32_t t) {
    for (int i = 0; i < total_frames; ++i) {
        if (frame_times[i] > t) return i - 1;
    }
    return total_frames - 1;
}
 


