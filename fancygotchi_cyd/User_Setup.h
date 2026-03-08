// ================================================================
//  TFT_eSPI User Setup — ESP32-2432S028R (Cheap Yellow Display)
//  Copy this file to:
//  Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
// ================================================================

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// HSPI pins for display
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1
#define TFT_MISO  12
#define TFT_BL    21

#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000
