#include <stdlib.h>
#include <string.h>
#include "bmp.h"

static uint8_t* create_bmp_header(uint16_t width, uint16_t height, size_t* full_size) {
    // Умножаем на 3, 24 бит на пиксель, и выравниваем до 4 байт
    uint32_t row_size = (width * 3  + 3) & ~3;
    uint32_t img_size = row_size * height;
    *full_size = BMP_FILE_HEADER_SIZE + BMP_INFO_HEADER_SIZE + img_size;

    uint8_t* bmp = malloc(*full_size);
    if (!bmp)
        return NULL;
    
    memset(bmp, 0, *full_size);

    BmpFileHeader* file_header = (BmpFileHeader*)bmp;
    BmpInfoHeader* info_header = (BmpInfoHeader*)(bmp + BMP_FILE_HEADER_SIZE);

    file_header->bfType = BM_TYPE;
    file_header->bfSize = *full_size;
    file_header->bfReserved1 = 0;
    file_header->bfReserved2 = 0;
    file_header->bfOffBits = BMP_HEADER_SIZE;

    info_header->biSize = BMP_INFO_HEADER_SIZE;
    info_header->biWidth = width;
    info_header->biHeight = height;
    info_header->biPlanes = 1;
    info_header->biBitCount = 24;
    info_header->biCompression = 0;
    info_header->biSizeImage = img_size;
    info_header->biXPelsPerMeter = 0x130B; // 2835 пикселей на метр (72 DPI)
    info_header->biYPelsPerMeter = 0x130B; // 2835 пикселей на метр (72 DPI)
    info_header->biClrUsed = 0;
    info_header->biClrImportant = 0;

    return bmp;
}

static void rgb565_to_bgr24(const uint8_t* inp_data,
                     uint8_t *out_data,
                     uint16_t width,
                     uint16_t height) 
{
    uint32_t row_size = (width * 3 + 3) & ~3;

    for (uint16_t y = 0; y < height; y++) {
        uint8_t* row = out_data + (height - 1 - y) * row_size;
        const uint16_t* pixels = (const uint16_t*)inp_data;
        for (uint16_t x = 0; x < width; x++) {
            uint16_t pixel = pixels[y * width + x];
            // Выделяем компоненты RGB из RGB565
            uint8_t r = ((pixel >> 11) & 0x1F);
            uint8_t g = ((pixel >> 5) & 0x3F);
            uint8_t b = (pixel & 0x1F);
            // Преобразуем в 8 бит на канал (битовая репликация)
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            // Заолняем BMP в формате BGR [b][g][r],...
            row[x * 3 + 0] = r;
            row[x * 3 + 1] = b;
            row[x * 3 + 2] = g;
        }
    }
}

uint8_t* convert_rgb565_to_bmp(const uint8_t* inp_data,
                               uint16_t width,
                               uint16_t height,
                               size_t* bmp_size) 
{
    uint8_t *bmp = create_bmp_header(width, height, bmp_size);
    if (!bmp)
        return NULL;

    rgb565_to_bgr24(inp_data, bmp + BMP_HEADER_SIZE, width, height);
    return bmp;
}