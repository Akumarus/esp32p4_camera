#ifndef APP_SDCARD_H
#define APP_SDCARD_H

#include "esp_log.h"
#include "esp_err.h"

typedef enum {
    FORMAT_RAW,
    FORMAT_BMP,
} photo_format_t;

esp_err_t app_sdcard_init();
esp_err_t app_sdcard_mkdir(const char *path);
esp_err_t app_sdcard_save_photo(const char *filename, const 
                                uint8_t* data, 
                                size_t size,
                                photo_format_t format);
#endif // APP_SDCARD_H

