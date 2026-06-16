/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief BSP Camera Example
 * @details Stream camera output to display (LVGL)
 * @example https://espressif.github.io/esp-launchpad/?flashConfigURL=https://espressif.github.io/esp-bsp/config.toml&app=display_camera_video
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#if SOC_PPA_SUPPORTED
#include "driver/ppa.h"
#endif
#include "esp_task_wdt.h"

#define ZXING_SCAN_DELAY_MS 50
#include "esp_private/esp_cache_private.h"
#include "app_video.h"
#include "zxing.h"
#include "esp_cam_sensor_detect.h"
#include "ov9281.h"

#ifdef CONFIG_CAMERA_OV9281
/* Define custom detections with both possible 7-bit addresses (0x30 and 0x60) */
ESP_CAM_SENSOR_DETECT_FN(ov9281_fix_30, ESP_CAM_SENSOR_MIPI_CSI, 0x30)
{
    ESP_LOGI("OV9281_FIX", "Custom detector called for 0x30");
    ((esp_cam_sensor_config_t *)config)->sensor_port = ESP_CAM_SENSOR_MIPI_CSI;
    return ov9281_detect(config);
}

ESP_CAM_SENSOR_DETECT_FN(ov9281_fix_60, ESP_CAM_SENSOR_MIPI_CSI, 0x60)
{
    ESP_LOGI("OV9281_FIX", "Custom detector called for 0x60");
    ((esp_cam_sensor_config_t *)config)->sensor_port = ESP_CAM_SENSOR_MIPI_CSI;
    return ov9281_detect(config);
}
#endif

#define NUM_BUFS 2
#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))

typedef struct {
    void  *start;
    size_t length;
} mmap_buf_t;

static const char *TAG = "main";

#if SOC_PPA_SUPPORTED
static ppa_client_handle_t ppa_srm_handle = NULL;
#endif
static size_t data_cache_line_size = 0;
static lv_obj_t *camera_canvas = NULL;
static uint8_t *cam_buff[NUM_BUFS];
static uint32_t cam_buff_size = 0;
static uint8_t *scan_buff = NULL;
static uint32_t scan_buff_size = 0;
static SemaphoreHandle_t zxing_sem = NULL;
static SemaphoreHandle_t display_sem = NULL;
static uint8_t *raw_display_buffs[2]; // Double buffering for display
static int current_raw_idx = 0;
static int ready_raw_idx = 0;

static void display_task(void *pvParameters)
{
    while (1) {
        if (xSemaphoreTake(display_sem, portMAX_DELAY) == pdTRUE) {
            uint32_t disp_w = 640;
            uint32_t disp_h = 400;
            uint16_t *display_buf = (uint16_t *)cam_buff[0];
            uint8_t *src_raw = raw_display_buffs[ready_raw_idx];
            
            // CONVERSION: RAW8 -> RGB565 (Grayscale)
            for (int i = 0; i < disp_w * disp_h; i++) {
                uint8_t gray = src_raw[i];
                uint16_t p = (gray >> 3);
                display_buf[i] = (p << 11) | ((gray >> 2) << 5) | p;
            }

            // Display update
            bsp_display_lock(0);
            lv_canvas_set_buffer(camera_canvas, display_buf, disp_w, disp_h, LV_COLOR_FORMAT_RGB565);
            lv_obj_center(camera_canvas);
            lv_obj_invalidate(camera_canvas);
            bsp_display_unlock();
        }
    }
}

static void zxing_task(void *pvParameters)
{
    while (1) {
        if (xSemaphoreTake(zxing_sem, portMAX_DELAY) == pdTRUE) {
            if (scan_buff) {
                process_frame(scan_buff, 160 * 100, 160, 100);
            }
            // Minimal yield
            vTaskDelay(1);
        }
    }
}
static int frame_count_dec = 0;

#if SOC_PPA_SUPPORTED
static void app_ppa_init(void)
{
    /* Initialize PPA */
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
    };

    esp_err_t ret = ppa_register_client(&ppa_srm_config, &ppa_srm_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA client: 0x%x", ret);
    }
}

static esp_err_t app_image_process_scale_crop(
    uint8_t *in_buf, uint32_t in_width, uint32_t in_height,
    uint8_t *out_buf, uint32_t out_width, uint32_t out_height, size_t out_buf_size,
    ppa_srm_rotation_angle_t rotation_angle)
{
    float scale_x = (float)out_width / in_width;
    float scale_y = (float)out_height / in_height;

    if (rotation_angle == PPA_SRM_ROTATION_ANGLE_90 || rotation_angle == PPA_SRM_ROTATION_ANGLE_270) {
        scale_x = (float)out_height / in_width;
        scale_y = (float)out_width / in_height;
    }

    ppa_srm_oper_config_t srm_config = {
        .in.buffer = in_buf,
        .in.pic_w = in_width,
        .in.pic_h = in_height,
        .in.block_w = in_width,
        .in.block_h = in_height,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = out_buf,
        .out.buffer_size = out_buf_size,
        .out.pic_w = out_width,
        .out.pic_h = out_height,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = rotation_angle,
        .scale_x = scale_x,
        .scale_y = scale_y,
        .rgb_swap = 0,
        .byte_swap = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    return ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config);
}

static void calc_aspect_fit(
    uint32_t src_w, uint32_t src_h,
    uint32_t dst_w, uint32_t dst_h,
    uint32_t *out_w, uint32_t *out_h)
{
    float src_aspect = (float)src_w / src_h;
    float dst_aspect = (float)dst_w / dst_h;

    if (src_aspect > dst_aspect) {
        *out_w = dst_w;
        *out_h = dst_w / src_aspect;
    } else {
        *out_h = dst_h;
        *out_w = dst_h * src_aspect;
    }
}
#endif
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes,
        uint32_t camera_buf_ves, size_t camera_buf_len)
{
    /* 1. Simple Downsample for ZXing (CPU 1) - Raw Grayscale */
    if (uxSemaphoreGetCount(zxing_sem) == 0) {
        for (int y = 0; y < 100; y++) {
            uint8_t *src_line = &camera_buf[(y * 4) * 640];
            uint8_t *dst_line = &scan_buff[y * 160];
            for (int x = 0; x < 160; x++) {
                dst_line[x] = src_line[x * 4];
            }
        }
        xSemaphoreGive(zxing_sem);
    }

    /* 2. Copy raw frame for Display Task using Double Buffering to avoid tearing */
    if (uxSemaphoreGetCount(display_sem) == 0) {
        ready_raw_idx = current_raw_idx;
        memcpy(raw_display_buffs[ready_raw_idx], camera_buf, 640 * 400);
        current_raw_idx = (current_raw_idx + 1) % 2;
        xSemaphoreGive(display_sem);
    }
}

void app_main(void)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGE("CRITICAL", "#############################################");
    ESP_LOGE("CRITICAL", "!!! FIRMWARE VERSION 45.0: STABLE 50 FIXED !!!");
    ESP_LOGE("CRITICAL", "#############################################");

    bsp_display_start();
    bsp_display_brightness_set(100);
    bsp_display_backlight_on(); 
    /* Removed blue background to improve contrast */

    /* Initialize Camera */
    bsp_camera_start(NULL);

#if SOC_PPA_SUPPORTED
    /* Initialize PPA for scaling */
    app_ppa_init();
#endif

    /* Get cache alignment */
    ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get cache alignment failed: 0x%x", ret);
        return;
    }

    /* Allocate canvas buffers - 640x400 RGB565 */
    cam_buff_size = ALIGN_UP(640 * 400 * 2, data_cache_line_size);
    for (int i = 0; i < NUM_BUFS; i++) {
        cam_buff[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, cam_buff_size, MALLOC_CAP_SPIRAM);
        if (cam_buff[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate camera buffer %d", i);
            return;
        }
    }

    /* Allocate Raw Display buffers (Double buffering) */
    raw_display_buffs[0] = heap_caps_malloc(640 * 400, MALLOC_CAP_SPIRAM);
    raw_display_buffs[1] = heap_caps_malloc(640 * 400, MALLOC_CAP_SPIRAM);

    /* Allocate Grayscale buffer for ZXing - 320x200 resolution */
    scan_buff_size = 320 * 200;
    scan_buff = heap_caps_malloc(scan_buff_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    /* Initialize ZXing and Display tasks - High priority for ZXing */
    zxing_sem = xSemaphoreCreateBinary();
    display_sem = xSemaphoreCreateBinary();
    
    xTaskCreatePinnedToCore(display_task, "display_task", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(zxing_task, "zxing_task", 64 * 1024, NULL, 15, NULL, 1);

    /* Create LVGL canvas for camera image - 640x400 */
    bsp_display_lock(0);
    camera_canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(camera_canvas, cam_buff[0], 640, 400, LV_COLOR_FORMAT_RGB565);
    assert(camera_canvas);
    lv_obj_center(camera_canvas);
    bsp_display_unlock();

    /* Open video device in RAW8 mode for OV9281 */
    int fd = app_video_open(BSP_CAMERA_DEVICE, APP_VIDEO_FMT_RAW8);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open video device");
        ESP_LOGW(TAG, "Please, try to select another camera sensor in menuconfig.");
        return;
    }

    /* ABSOLUTE MINIMUM Exposure: 1 line (~25-30us) at 100 FPS, High Gain */
    struct v4l2_control ctrl_exp = { .id = V4L2_CID_EXPOSURE, .value = 100 };
    struct v4l2_control ctrl_gain = { .id = V4L2_CID_GAIN, .value = 250 };
    
    ioctl(fd, VIDIOC_S_CTRL, &ctrl_exp);
    ioctl(fd, VIDIOC_S_CTRL, &ctrl_gain);

    /* Initialize video capture device */
    ret = app_video_set_bufs(fd, NUM_BUFS, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set video buffers: 0x%x", ret);
        return;
    }

    /* Test pattern is disabled for 720p attempt */
    /*
    struct v4l2_ext_control ctrl = {
        .id = V4L2_CID_TEST_PATTERN,
        .value = 1,
    };
    struct v4l2_ext_controls ctrls = {
        .count = 1,
        .controls = &ctrl,
    };
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) == 0) {
        ESP_LOGW(TAG, "Test pattern ENABLED");
    }
    */

    /* Register frame process callback */
    ret = app_video_register_frame_operation_cb(camera_video_frame_operation);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register frame operation callback: 0x%x", ret);
        return;
    }

    /* Start video stream task */
    ret = app_video_stream_task_start(fd, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start video stream task: 0x%x", ret);
        return;
    }

    ESP_LOGW(TAG, "ZXING SCANNER VERSION 2.0 STARTED");
}
