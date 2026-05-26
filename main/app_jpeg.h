#ifndef APP_JPEG_H
#define APP_JPEG_H

#include "esp_err.h"

esp_err_t app_jpeg_init();
esp_err_t app_jpeg_raw2jpeg(const uint8_t *raw_data, size_t raw_size, uint32_t width, uint32_t height, size_t *jpeg_size);

#endif // APP_JPEG_H
