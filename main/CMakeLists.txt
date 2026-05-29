set(srcs "main.c" 
    "app_sdcard.c"
    "app_video.c"
    "app_jpeg.c" 
    "app_lcd.c"
    "app/bmp.c"
)

idf_component_register(SRCS
    "${srcs}"
    PRIV_INCLUDE_DIRS
        .
        app
)
