#include "SdFat.h"
#include "SystemFont5x7.h"
#include <SPI.h>
#include <DMD2.h>

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 1

// Maximum file name length (with extention)
#define MAX_FILE_LEN 18
#define MAX_PAGES 80
#define PAGE_TIME 200

#define BRIGHTNESS 200

// SDCARD_SS_PIN is defined for the built-in SD on some boards.
#ifndef SDCARD_SS_PIN
const uint8_t SD_CS_PIN = SS;
#else   // SDCARD_SS_PIN
// Assume built-in SD is used.
const uint8_t SD_CS_PIN = SDCARD_SS_PIN;
#endif  // SDCARD_SS_PIN

// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(50)

// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif  // HAS_SDIO_CLASS

#if SD_FAT_TYPE == 0
SdFat sd;
File dir;
File file;
#elif SD_FAT_TYPE == 1
SdFat32 sd;
File32 dir;
File32 file;
#elif SD_FAT_TYPE == 2
SdExFat sd;
ExFile dir;
ExFile file;
#elif SD_FAT_TYPE == 3
SdFs sd;
FsFile dir;
FsFile file;
#else  // SD_FAT_TYPE
#error invalid SD_FAT_TYPE
#endif  // SD_FAT_TYPE

#define VERSION "0.1.0"

SoftDMD dmd(1, 2);  // DMD controls the entire display(s)

char fileNames[MAX_FILE_LEN][MAX_PAGES];
uint8_t fileBuffer[1024];
int files = 0;

unsigned long previousMillis = 0;

void setup() {
  Serial.begin(9600);

  dmd.begin();
  dmd.setBrightness(BRIGHTNESS);
  dmd.selectFont(SystemFont5x7);

  // Initialize the SD.
  if (!sd.begin(SD_CONFIG)) {
    dispError(1);
    while (true) {
    }
  }
  // Open root directory
  if (!dir.open("/")) {
    dispError(2);
    while (true) {
    }
  }

  // Loop through files and add names to fileNames
  while (file.openNext(&dir, O_RDONLY)) {
    if (!file.isDir()) {
      file.getName(fileNames[files], MAX_FILE_LEN);
    }
    file.close();
    files++;
  }
  if (dir.getError()) {
    dispError(3);
    while (true) {
    }
  }

  wipeAni();
}

void loop() {
  for (int i = 1; i < files; i++) {
    if (EndsWith(fileNames[i], ".DMD")) {
      if (file.open(fileNames[i], FILE_READ)) {
        file.readBytes(fileBuffer, 1024);
        loadPic(fileBuffer);
        delayBar(PAGE_TIME / 32);
      } else {
        dispError(4);
        while (true) {
        }
      }
      file.close();
    }
  }
}

void wipeAni() {
  for (int i = 0; i < 32; i++) {
    dmd.drawLine(0, i, 31, i);
    delay(10);
  }
}

void loadPic(const uint8_t *pic) {
  int p = 0;
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      if (pic[p] == 1) {
        dmd.setPixel(x, y, GRAPHICS_ON);
      } else {
        dmd.setPixel(x, y, GRAPHICS_OFF);
      }
      p++;
    }
  }
}

void delayBar(int time) {
  for (int i = 0; i < 32; i++) {
    dmd.setPixel(i, 31, GRAPHICS_XOR);
    while (1) {
      lcdUpdate();
      unsigned long currentMillis = millis();

      if (currentMillis - previousMillis >= time) {
        previousMillis = currentMillis;
        break;
      }
    }
  }
}

void dispError(uint8_t code) {
  char codeBuf[2];

  dmd.clearScreen();
  dmd.drawString(0, 1, "Error");
  itoa(code, codeBuf, 10);
  dmd.drawString(0, 9, codeBuf);
}

void lcdUpdate() {
  // TODO: Add LCD Features
}

int EndsWith(const char *str, const char *suffix) {
  if (!str || !suffix)
    return 0;
  size_t lenstr = strlen(str);
  size_t lensuffix = strlen(suffix);
  if (lensuffix > lenstr)
    return 0;
  return strncasecmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}