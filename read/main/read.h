#ifndef READ_H
#define READ_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"

#define TAG "LIGHTDANCE"

#define MAX_PARTS 32
#define MAX_COLOR_VALUES 4096
#define MAX_FRAMES 2048

typedef struct {
    bool fade;
    uint8_t colors[MAX_COLOR_VALUES][4]; // RGBA
} FrameData;

typedef struct {
    FILE *data_fp;
    int total_parts;
    int part_lengths[MAX_PARTS];
    int total_frames;
    int fps;
    uint32_t frame_times[MAX_FRAMES];
    size_t frame_offsets[MAX_FRAMES];
    const char *mount_point;
} LightdanceReader;

// ===== Function Prototypes =====
// void LightdanceReader_init(LightdanceReader *self, const char *mount_point);
// void LightdanceReader_load_frame_times(LightdanceReader *self);
// void LightdanceReader_index_frames(LightdanceReader *self);
// FrameData LightdanceReader_read_frame_at(LightdanceReader *self, int frame_index);
// FrameData LightdanceReader_get_next_frame(LightdanceReader *self, int currentIndex);
// FrameData LightdanceReader_get_kth_frame_of_led(LightdanceReader *self, int led_index, int k);
// int LightdanceReader_get_frame_index_at_time(LightdanceReader *self, uint32_t t);

// ===== Implementation =====

void LightdanceReader_init(LightdanceReader *self, const char *mount_point) {
    self->data_fp = NULL;
    self->total_parts = 0;
    memset(self->part_lengths, 0, sizeof(self->part_lengths));
    self->total_frames = 0;
    self->fps = 30;
    memset(self->frame_times, 0, sizeof(self->frame_times));
    memset(self->frame_offsets, 0, sizeof(self->frame_offsets));
    self->mount_point = mount_point;
}

void LightdanceReader_load_frame_times(LightdanceReader *self) {
    char path[128];
    snprintf(path, sizeof(path), "%s/frame_times.txt", self->mount_point);
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open frame_times.txt");
        return;
    }
    char line[32];
    self->total_frames = 0;
    while (fgets(line, sizeof(line), f) && self->total_frames < MAX_FRAMES) {
        self->frame_times[self->total_frames++] = atoi(line);
    }
    fclose(f);
    ESP_LOGI(TAG, "Loaded %d frame times", self->total_frames);
}

void LightdanceReader_index_frames(LightdanceReader *self) {
    char path[128];
    snprintf(path, sizeof(path), "%s/patterntable/lightdance_data.txt", self->mount_point);

    self->data_fp = fopen(path, "r");
    if (!self->data_fp) {
        ESP_LOGE(TAG, "Failed to open lightdance_data.txt");
        return;
    }

    char line[256];

    // total_parts
    fgets(line, sizeof(line), self->data_fp);
    self->total_parts = atoi(line);

    // part lengths
    fgets(line, sizeof(line), self->data_fp);
    char *token = strtok(line, " ");
    int i = 0;
    while (token && i < MAX_PARTS) {
        self->part_lengths[i++] = atoi(token);
        token = strtok(NULL, " ");
    }

    // FPS
    fgets(line, sizeof(line), self->data_fp);
    self->fps = atoi(line);

    // frame offsets
    int frame_idx = 0;
    while (!feof(self->data_fp) && frame_idx < MAX_FRAMES) {
        long offset = ftell(self->data_fp);
        self->frame_offsets[frame_idx++] = offset;

        if (!fgets(line, sizeof(line), self->data_fp)) break;

        for (int p = 0; p < self->total_parts; ++p) {
            int len = self->part_lengths[p];
            for (int j = 0; j < len; ++j) {
                if (!fgets(line, sizeof(line), self->data_fp)) break;
            }
        }
    }
    self->total_frames = frame_idx;
    ESP_LOGI(TAG, "Indexed %d frames with offsets", self->total_frames);
}

FrameData LightdanceReader_read_frame_at(LightdanceReader *self, int frame_index) {
    FrameData fd = {0};
    if (!self->data_fp || frame_index >= self->total_frames) return fd;

    fseek(self->data_fp, self->frame_offsets[frame_index], SEEK_SET);

    char line[256];
    fgets(line, sizeof(line), self->data_fp);
    fd.fade = strstr(line, "true") != NULL;

    int c = 0;
    for (int p = 0; p < self->total_parts; p++) {
        int len = self->part_lengths[p];
        for (int j = 0; j < len; j++) {
            if (!fgets(line, sizeof(line), self->data_fp)) break;
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

FrameData LightdanceReader_get_next_frame(LightdanceReader *self, int currentIndex) {
    if (currentIndex + 1 >= self->total_frames) return (FrameData){0};
    return LightdanceReader_read_frame_at(self, currentIndex + 1);
}

FrameData LightdanceReader_get_kth_frame_of_led(LightdanceReader *self, int led_index, int k) {
    int count = 0;
    for (int i = 0; i < self->total_frames; ++i) {
        FrameData fd = LightdanceReader_read_frame_at(self, i);
        if (fd.colors[led_index][3] > 0) {
            if (count++ == k) return fd;
        }
    }
    return (FrameData){0};
}

int LightdanceReader_get_frame_index_at_time(LightdanceReader *self, uint32_t t) {
    for (int i = 0; i < self->total_frames; ++i) {
        if (self->frame_times[i] > t) return i - 1;
    }
    return self->total_frames - 1;
}

#endif // READ_H
