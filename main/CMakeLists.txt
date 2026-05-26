set(srcs "main.c" "app_video.c" "app_lcd.c" "app_sdcard.c")

idf_component_register(SRCS "app_jpeg.c" "spi_test.c" "${srcs}"
                       PRIV_INCLUDE_DIRS .)
