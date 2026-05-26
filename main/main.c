#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "app_sdcard.h"
#include "app_video.h"
#include "app_jpeg.h"
#include "app_lcd.h"
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <inttypes.h>
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
static uint8_t *raw_frame = NULL;
static bool frame_ready = false;
static uint32_t img_width = 0;
static uint32_t img_height = 0;
static size_t img_size = 0;
static size_t s_cache_line_size = 0;

static void save_raw_image(const uint8_t *data, size_t size, uint32_t width, uint32_t height)
{
    char path[64];
    snprintf(path, sizeof(path), "/sdcard/images/frame.bin");
    
    ESP_LOGI("MAIN", "Saving: %s, size=%u bytes", path, (unsigned int)size);
    
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE("MAIN", "Failed to open %s, errno=%d", path, errno);
        return;
    }
    
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    
    if (written == size) {
        ESP_LOGI("MAIN", "Saved: %s (%u bytes)", path, (unsigned int)size);
    } else {
        ESP_LOGE("MAIN", "Write error: wrote %u of %u bytes", (unsigned int)written, (unsigned int)size);
    }
}

static void frame_callback(uint8_t *camera_buf, uint8_t buf_idx, 
                          uint32_t width, uint32_t height, 
                          size_t buf_len, void *user_data) 
{
    if (!frame_ready) {
        raw_frame = malloc(buf_len);
        if (raw_frame) {
            memcpy(raw_frame, camera_buf, buf_len);
            img_width = width;
            img_height = height;
            img_size = buf_len;
            frame_ready = true;
            
            ESP_LOGI("MAIN", "Frame captured: %lux%lu, size=%u bytes", 
                     (unsigned long)width, (unsigned long)height, (unsigned int)buf_len);
            ESP_LOG_BUFFER_HEX("MAIN", camera_buf, 32);
        }
    }
}

void app_main(void)
{
    // openocd -f interface/jlink.cfg -c "transport select jtag" -c "adapter speed 5000" -c "set ESP_RTOS hwthread" -f target/esp32p4.cfg
    // Получаем выравнивание кеша
    esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);
    if (s_cache_line_size == 0) s_cache_line_size = 64;
    
    // Initialize SD card
    app_sdcard_init();
    // Initialize JPEG encoder
    app_jpeg_init();
    // Initialize camera
    app_video_main(NULL);
    int fd = app_video_open(EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT);
    if (fd < 0) return;
    
    // Get buffer size
    size_t buf_size = app_video_get_buf_size();
    ESP_LOGI("MAIN", "Buffer size: %u bytes", (unsigned int)buf_size);       
    
    // Allocate aligned buffers for camera
    void *camera_buf[EXAMPLE_CAM_BUF_NUM];
    for (int i = 0; i < EXAMPLE_CAM_BUF_NUM; i++) {
        camera_buf[i] = heap_caps_aligned_calloc(s_cache_line_size, 1, buf_size, MALLOC_CAP_SPIRAM);
        if (!camera_buf[i]) {
            ESP_LOGE("MAIN", "Failed to allocate buffer %d", i);
            return;
        }
        ESP_LOGI("MAIN", "Camera buffer %d allocated at %p", i, camera_buf[i]);
    }
    
    // Set video buffers
    esp_err_t ret = app_video_set_bufs(fd, EXAMPLE_CAM_BUF_NUM, (void *)camera_buf);
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Failed to set buffers: %s", esp_err_to_name(ret));
        return;
    }
    
    // Register callback
    app_video_register_frame_operation_cb(frame_callback);
    
    // Start stream
    ret = app_video_stream_task_start(fd, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Failed to start stream: %s", esp_err_to_name(ret));
        return;
    }
    
    // Wait for frame (with timeout)
    int timeout = 0;
    while (!frame_ready && timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout++;
    }
    app_video_stream_task_stop(fd);
    
    if (!frame_ready) {
        ESP_LOGE("MAIN", "Timeout: No frame captured");
        return;
    }
    
    // Save raw image
    if (raw_frame) {
        ESP_LOGI("MAIN", "Saving raw image, size=%u bytes", (unsigned int)img_size);
        save_raw_image(raw_frame, img_size, img_width, img_height);
        ESP_LOGI("MAIN", "Converting Raw --> JPEG: %u bytes", (unsigned int)img_size);
        size_t jpeg_size = 0;
        ret = app_jpeg_raw2jpeg(raw_frame, img_size, img_width, img_height, &jpeg_size);
        if (jpeg_size > 0 && ret == ESP_OK) {
            ESP_LOGI("MAIN", "JPEG size: %u bytes", (unsigned int)jpeg_size);
        } else {
            ESP_LOGE("MAIN", "JPEG conversion failed: %s", esp_err_to_name(ret));
        }
        
        free(raw_frame);
    }
    

        

}