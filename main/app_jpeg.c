#include "driver/jpeg_encode.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *tag = "app_jpeg";
static jpeg_encoder_handle_t encoder_engine = NULL;

esp_err_t app_jpeg_init()
{
    jpeg_encode_engine_cfg_t encode_config = {
        .intr_priority = 0,
        .timeout_ms = 100,
    };

    esp_err_t ret = jpeg_new_encoder_engine(&encode_config, &encoder_engine);
    ESP_RETURN_ON_ERROR(ret, tag, "Failed to create encoder engine: %s", esp_err_to_name(ret));
    ESP_LOGI(tag, "JPEG encoder engine initialized successfully");

    return ESP_OK;
}

esp_err_t app_jpeg_raw2jpeg(const uint8_t *raw_data, size_t raw_size, uint32_t width, uint32_t height, size_t *jpeg_size)
{
    if (!encoder_engine) {
        ESP_LOGE(tag, "Encoder engine not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    size_t max_jpeg_size = raw_size / 2;
    if (max_jpeg_size < 1024) {
        max_jpeg_size = raw_size;
    }

    jpeg_encode_memory_alloc_cfg_t memory_config = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };

    size_t allocated_size = 0;
    uint8_t *jpeg_buffer = (uint8_t*)jpeg_alloc_encoder_mem(max_jpeg_size, &memory_config, &allocated_size);
    if (!jpeg_buffer) {
        ESP_LOGI(tag, "Failed to allocate JPEG buffer (requestd %u bytes)", max_jpeg_size);
        return ESP_ERR_NO_MEM;
    }

    jpeg_encode_cfg_t encode_config = {
        .width = width,
        .height = height,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = 85,
    };

    ESP_LOGI(tag, "Encodeing: %lux%lu RGB565 to JPEG, max output size %u bytes", width, height, max_jpeg_size);

    uint32_t jpeg_out_size = 0;
    esp_err_t ret = jpeg_encoder_process(encoder_engine, &encode_config, raw_data, raw_size, jpeg_buffer, allocated_size, &jpeg_out_size);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "JPEG encoding failed: %s", esp_err_to_name(ret));
        free(jpeg_buffer);
        return ret;
    }

    *jpeg_size = jpeg_out_size;
    // ESP_LOGI(tag, "Encoding successful, output JPEG size: %ul bytes", jpeg_out_size);
    return ESP_OK;
}
