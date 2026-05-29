#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "app_sdcard.h"
#include "app_video.h"
#include "app_jpeg.h"
#include "app_lcd.h"
#include <stdbool.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <inttypes.h>
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "app/bmp.h"

static const char *TAG = "MAIN";

// UART настройки для команд
#define UART_CMD_NUM     UART_NUM_1
#define UART_CMD_TX      GPIO_NUM_14
#define UART_CMD_RX      GPIO_NUM_15
#define UART_CMD_BAUD    5000000

static uint8_t *raw_frame = NULL;
static bool frame_ready = false;
static uint32_t img_width = 0;
static uint32_t img_height = 0;
static size_t img_size = 0;
static size_t s_cache_line_size = 0;
static bool send_bmp_requested = false;

// Отправка BMP по UART с процентами
static void send_bmp_over_uart(uint8_t *bmp_data, size_t bmp_size)
{
    ESP_LOGI(TAG, "Sending BMP, size: %u bytes", (unsigned int)bmp_size);
    
    // Отправляем команду начала
    char start_cmd[32];
    snprintf(start_cmd, sizeof(start_cmd), "BMP_START:%u\n", (unsigned int)bmp_size);
    uart_write_bytes(UART_CMD_NUM, start_cmd, strlen(start_cmd));
    uart_wait_tx_done(UART_CMD_NUM, portMAX_DELAY);  // Ждем отправки
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Отправляем данные с процентами
    size_t sent = 0;
    const size_t CHUNK_SIZE = 4096;
    int last_percent = -1;
    
    while (sent < bmp_size) {
        size_t to_send = (bmp_size - sent) > CHUNK_SIZE ? CHUNK_SIZE : (bmp_size - sent);
        uart_write_bytes(UART_CMD_NUM, bmp_data + sent, to_send);
        sent += to_send;
        
        // Вычисляем процент
        int percent = (sent * 100) / bmp_size;
        if (percent != last_percent && percent % 10 == 0) {
            ESP_LOGI(TAG, "Progress: %d%% (%u/%u bytes)", percent, (unsigned int)sent, (unsigned int)bmp_size);
            
            // Отправляем процент на ПК
            char percent_msg[32];
            snprintf(percent_msg, sizeof(percent_msg), "PROGRESS:%d\n", percent);
            uart_write_bytes(UART_CMD_NUM, percent_msg, strlen(percent_msg));
            last_percent = percent;
        }
        
        vTaskDelay(pdMS_TO_TICKS(2));  // Увеличил задержку
    }
    
    // Ждем завершения отправки всех данных - ВАЖНО!
    uart_wait_tx_done(UART_CMD_NUM, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(50));  // Дополнительная задержка
    
    // Отправляем команду завершения
    uart_write_bytes(UART_CMD_NUM, "BMP_END\n", 8);
    uart_wait_tx_done(UART_CMD_NUM, portMAX_DELAY);
    
    ESP_LOGI(TAG, "BMP sent successfully! 100%%");
}
// Задача обработки команд с ПК
static void uart_command_task(void *pvParameters)
{
    char buffer[64];
    int idx = 0;
    uint8_t byte;
    
    uart_config_t uart_config = {
        .baud_rate = UART_CMD_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(UART_CMD_NUM, 32768, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_CMD_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_CMD_NUM, UART_CMD_TX, UART_CMD_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    ESP_LOGI(TAG, "UART command ready. Send 'get_bmp' or 'photo'");
    
    while (1) {
        if (uart_read_bytes(UART_CMD_NUM, &byte, 1, pdMS_TO_TICKS(100)) == 1) {
            if (byte == '\n' || byte == '\r') {
                if (idx > 0) {
                    buffer[idx] = '\0';
                    ESP_LOGI(TAG, "Command: %s", buffer);
                    
                    if (strcmp(buffer, "get_bmp") == 0 || strcmp(buffer, "photo") == 0) {
                        // Очищаем UART перед отправкой
                        uart_flush(UART_CMD_NUM);
                        vTaskDelay(pdMS_TO_TICKS(10));
                        
                        send_bmp_requested = true;
                        uart_write_bytes(UART_CMD_NUM, "OK\n", 3);
                        uart_wait_tx_done(UART_CMD_NUM, portMAX_DELAY);
                    } else if (strcmp(buffer, "help") == 0) {
                        uart_write_bytes(UART_CMD_NUM, "Commands: get_bmp, photo, help\n", 30);
                    }
                    idx = 0;
                }
            } else if (idx < sizeof(buffer) - 1) {
                buffer[idx++] = byte;
            }
        }
    }
}

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
    static int frame_count = 0;

    // if (++frame_count < 30) {
    //     return;
    // }

    frame_count = 0;

    if (!frame_ready && send_bmp_requested) {

        raw_frame = malloc(buf_len);

        if (raw_frame) {

            memcpy(raw_frame, camera_buf, buf_len);

            img_width = width;
            img_height = height;
            img_size = buf_len;

            frame_ready = true;

            ESP_LOGI("MAIN", "Frame captured: %lux%lu size=%u",
                     (unsigned long)width,
                     (unsigned long)height,
                     (unsigned int)buf_len);
        }
    }
}

// static void debug_pixel_formats(const uint8_t *rgb565_data,
//                                 int width,
//                                 int height)
// {
//     const uint16_t *pixels = (const uint16_t *)rgb565_data;

//     int cx = width / 2;
//     int cy = height / 2;

//     // Берем центральный пиксель
//     uint16_t p = pixels[cy * width + cx];

//     // Вариант со swap
//     uint16_t ps = (p >> 8) | (p << 8);

//     ESP_LOGI("PIXDBG", "========================================");
//     ESP_LOGI("PIXDBG", "RAW PIXEL:");
//     ESP_LOGI("PIXDBG", "p  = 0x%04X", p);
//     ESP_LOGI("PIXDBG", "ps = 0x%04X (byte swap)", ps);

//     // =========================
//     // RGB565 normal
//     // =========================

//     uint8_t r1 = ((p >> 11) & 0x1F) * 255 / 31;
//     uint8_t g1 = ((p >> 5)  & 0x3F) * 255 / 63;
//     uint8_t b1 = (p & 0x1F) * 255 / 31;

//     ESP_LOGI("PIXDBG", "RGB565:");
//     ESP_LOGI("PIXDBG", "R=%3u G=%3u B=%3u", r1, g1, b1);

//     // =========================
//     // BGR565
//     // =========================

//     uint8_t r2 = (p & 0x1F) * 255 / 31;
//     uint8_t g2 = ((p >> 5) & 0x3F) * 255 / 63;
//     uint8_t b2 = ((p >> 11) & 0x1F) * 255 / 31;

//     ESP_LOGI("PIXDBG", "BGR565:");
//     ESP_LOGI("PIXDBG", "R=%3u G=%3u B=%3u", r2, g2, b2);

//     // =========================
//     // RGB565 + SWAP
//     // =========================

//     uint8_t r3 = ((ps >> 11) & 0x1F) * 255 / 31;
//     uint8_t g3 = ((ps >> 5)  & 0x3F) * 255 / 63;
//     uint8_t b3 = (ps & 0x1F) * 255 / 31;

//     ESP_LOGI("PIXDBG", "RGB565 SWAP:");
//     ESP_LOGI("PIXDBG", "R=%3u G=%3u B=%3u", r3, g3, b3);

//     // =========================
//     // BGR565 + SWAP
//     // =========================

//     uint8_t r4 = (ps & 0x1F) * 255 / 31;
//     uint8_t g4 = ((ps >> 5) & 0x3F) * 255 / 63;
//     uint8_t b4 = ((ps >> 11) & 0x1F) * 255 / 31;

//     ESP_LOGI("PIXDBG", "BGR565 SWAP:");
//     ESP_LOGI("PIXDBG", "R=%3u G=%3u B=%3u", r4, g4, b4);

//     ESP_LOGI("PIXDBG", "========================================");
// }


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
    
    // Запуск UART команды
    xTaskCreate(uart_command_task, "uart_cmd", 4096, NULL, 5, NULL);
    
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
    
    ESP_LOGI(TAG, "Camera ready. Send 'get_bmp' or 'photo' over UART");
    
    // Main loop
    while (1) {
        if (send_bmp_requested && frame_ready && raw_frame) {
            // Save raw image
            if (raw_frame) {
                // debug_pixel_formats(raw_frame, img_width, img_height);
                ESP_LOGI("MAIN", "Saving raw image, size=%u bytes", (unsigned int)img_size);
                // app_sdcard_save_photo("frame", raw_frame, img_size, FORMAT_RAW);
                // app_sdcard_save_photo("frame", bmp_data, bmp_size, FORMAT_BMP);
                ESP_LOGI("MAIN", "Converting Raw --> BMP for UART: %u bytes", (unsigned int)img_size);

                size_t bmp_size = 0;
                uint8_t *bmp_data = convert_rgb565_to_bmp(raw_frame, img_width, img_height, &bmp_size);
                
                if (bmp_data && bmp_size > 0) {
                    ESP_LOGI("MAIN", "BMP size: %u bytes", (unsigned int)bmp_size);
                    send_bmp_over_uart(bmp_data, bmp_size);
                    free(bmp_data);
                } else {
                    ESP_LOGE("MAIN", "BMP conversion failed");
                    uart_write_bytes(UART_CMD_NUM, "ERROR: BMP conversion failed\n", 30);
                }
                
                free(raw_frame);
                raw_frame = NULL;
                frame_ready = false;
                send_bmp_requested = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}