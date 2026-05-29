#ifndef BMP_H
#include <stdint.h>
#define BMP_H

#define BMP_FILE_HEADER_SIZE 14
#define BMP_INFO_HEADER_SIZE 40
#define BMP_HEADER_SIZE (BMP_FILE_HEADER_SIZE + BMP_INFO_HEADER_SIZE)

#define BM_TYPE 0x4D42 // "BM"

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;            // "BM"
    uint32_t bfSize;            // Размер файла
    uint16_t bfReserved1;       // Зарезервировано
    uint16_t bfReserved2;       // Зарезервировано
    uint32_t bfOffBits;         // Смещение до данных пикселей
} BmpFileHeader;

typedef struct {
    uint32_t biSize;            // Размер структуры (40 байт)
    uint32_t biWidth;           // Ширина
    uint32_t biHeight;          // Высота
    uint16_t biPlanes;          // Количество плоскостей (1)
    uint16_t biBitCount;        // Бит на пиксель (24/32)
    uint32_t biCompression;     // Тип сжатия (0 - без сжатия)
    uint32_t biSizeImage;       // Размер данных пикселей
    uint32_t biXPelsPerMeter;   // Горизонтальное разрешение
    uint32_t biYPelsPerMeter;   // Вертикальное разрешение
    uint32_t biClrUsed;         // Количество используемых цветов (0 для всех)
    uint32_t biClrImportant;    // Важные цвета (0 для всех)
} BmpInfoHeader;
#pragma pack(pop)

uint8_t* convert_rgb565_to_bmp(const uint8_t* inp_data,
                               uint16_t width,
                               uint16_t height,
                               size_t* bmp_size);

#endif // BMP_H