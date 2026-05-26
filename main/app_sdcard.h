#ifndef APP_SDCARD_H
#define APP_SDCARD_H

#include "esp_log.h"
#include "esp_err.h"

esp_err_t app_sdcard_init();
esp_err_t app_sdcard_mkdir(const char *path);

#endif // APP_SDCARD_H

