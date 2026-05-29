#include "app_sdcard.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include <errno.h>
#include <sys/stat.h>

const static char *TAG = "app_sdcard";

#define MOUNT_POINT "/sdcard"

static const char* get_format_ext(photo_format_t format);


esp_err_t app_sdcard_init() {
    
    esp_err_t ret;

    /* Включение питания через внутренний LDO */
    sd_pwr_ctrl_ldo_config_t ldo_config = { .ldo_chan_id = 4 };
    sd_pwr_ctrl_handle_t pwr_handle = NULL;

    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create LDO power control driver: %s", esp_err_to_name(ret));

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = 20000;
    host.pwr_ctrl_handle = pwr_handle;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 800 * 1024
    };

    sdmmc_card_t *card = NULL;
    ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    ESP_RETURN_ON_ERROR(ret, TAG, "Mount failed: %s", esp_err_to_name(ret));

    sdmmc_card_print_info(stdout, card);

    ret = app_sdcard_mkdir("/sdcard/images");
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create directory: %s", esp_err_to_name(ret));
    
    FILE *test = fopen("/sdcard/images/test.txt", "w");
    if (test) {
        fprintf(test, "SD card test file\n");
        fclose(test);
        ESP_LOGI(TAG, "Test file created: /sdcard/images/test.txt");
    } else {
        ESP_LOGW(TAG, "Failed to create test file");
    }
    
    ESP_LOGI(TAG, "SD card ready");

    return ESP_OK;
}

esp_err_t app_sdcard_mkdir(const char *path) {
    if (mkdir(path, 0777) == 0) {
        ESP_LOGI(TAG, "Directory created: %s", path);
    } else if (errno == EEXIST) {
        ESP_LOGI(TAG, "Directory already exists: %s", path);
    } else {
        ESP_LOGE(TAG, "Failed to create directory: %s", path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static const char* get_format_ext(photo_format_t format) {
    switch (format) {
        case FORMAT_RAW:
            return "bin";
        case FORMAT_BMP:
            return "bmp";
        default:
            return NULL;
    }
}

esp_err_t app_sdcard_save_photo(const char *filename, const 
                                uint8_t* data, 
                                size_t size,
                                photo_format_t format)
{
    // Получаем строку расширения для формата
    const char *ext = get_format_ext(format);
    if (!ext) {
        ESP_LOGE(TAG, "Unsupported photo format: %d", format);
        return ESP_ERR_INVALID_ARG;
    }
    // Формируем полный путь к файлу и сохраняем данные
    char path[128];
    snprintf(path, sizeof(path), "/sdcard/images/%s.%s", filename, ext);
    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to create file: %s", path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    if (written != size) {
        ESP_LOGE(TAG, "Failed to write all data to file: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Photo saved: %s", path);
    return ESP_OK;
}
