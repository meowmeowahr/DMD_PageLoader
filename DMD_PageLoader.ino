#include <DMD2.h>
#include <SPI.h>
#include <SdFat.h>

#include <Buzzer.h>
#include <Encoder.h>
#include <OneButton.h>

#include "Arial_Black_16.h"
#include "Droid_Sans_12.h"
#include "NumberFont3x5.h"
#include "SystemFont5x7.h"

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 1

// Maximum file name length (with extention)
#define MAX_FILE_LEN 18
#define MAX_PAGES 80

#define HIGH_BRIGHTNESS 255
#define MED_BRIGHTNESS 127
#define LOW_BRIGHTNESS 20

#define HIGH_PIN 22
#define LOW_PIN 24
#define BUZZER_PIN 4

#define ENC_BTN A1
#define ENC_A 2
#define ENC_B 3

// SDCARD_SS_PIN is defined for the built-in SD on some boards.
#ifndef SDCARD_SS_PIN
const uint8_t SD_CS_PIN = SS;
#else  // SDCARD_SS_PIN
// Assume built-in SD is used.
const uint8_t SD_CS_PIN = SDCARD_SS_PIN;
#endif // SDCARD_SS_PIN

// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(50)

// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif // HAS_SDIO_CLASS

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
#else // SD_FAT_TYPE
#error invalid SD_FAT_TYPE
#endif // SD_FAT_TYPE

#define VERSION "0.1.0"

SoftDMD dmd(1, 2); // DMD controls the entire display(s)
Buzzer buzzer(BUZZER_PIN, LED_BUILTIN);
OneButton btn(ENC_BTN);
Encoder enc(ENC_A, ENC_B);

char fileNames[MAX_FILE_LEN][MAX_PAGES];
uint8_t fileBuffer[1024];
int files = 0;

unsigned long previousMillis = 0;
unsigned long btnPress = 0;
unsigned long btnRelease = 0;

int timebarPos = 1;
int pageTime = 1000;

bool settingsLoaded = 0;
bool paused = 0;

unsigned int settingsSelectedItem = 0;
int settingsActiveItem = -1;
bool settingsScroll = true;
int settingsCurrentValue;

unsigned int currentPic = 1;

void setup() {
  pinMode(LOW_PIN, INPUT_PULLUP);
  pinMode(HIGH_PIN, INPUT_PULLUP);

  Serial.begin(9600);

  dmd.begin();
  dmd.setBrightness(MED_BRIGHTNESS);
  dmd.selectFont(Arial_Black_16);

  buzzer.begin(10);

  btn.attachClick(onClick);
  btn.attachLongPressStart(onLong);

  dispLoad(33);

  // Initialize the SD.
  if (!sd.begin(SD_CONFIG)) {
    dispError(1);
    while (true) {
    }
  }

  dispLoad(67);

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

  dispLoad(100);

  wipeAni();
}

void loop() {
  for (currentPic = 1; currentPic < files;) {
    if (settingsLoaded == 0) {
      if (EndsWith(fileNames[currentPic], ".DMD")) {
        if (file.open(fileNames[currentPic], FILE_READ)) {
          file.readBytes(fileBuffer, 1024);
          loadPic(fileBuffer);
          delayBar(pageTime / 32);
        } else {
          dispError(4);
          while (true) {
          }
        }
        file.close();
      } else {
        backgroundUpdate();
      }
      if (!paused) {
        currentPic++;
      }
    } else {
      backgroundUpdate();
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
    if (!paused and !settingsLoaded) {
      if (timebarPos == 1) {
        dmd.setPixel(i, 31, GRAPHICS_XOR);
      } else if (timebarPos == 2) {
        dmd.setPixel(i, 0, GRAPHICS_XOR);
      }
    }
    while (1) {
      backgroundUpdate();
      unsigned long currentMillis = millis();

      if (paused or settingsLoaded) {
        break;
      }

      if (currentMillis - previousMillis >= time) {
        previousMillis = currentMillis;
        break;
      }
    }
  }
}

void dispError(uint8_t code) {
  dmd.selectFont(Arial_Black_16);

  char codeBuf[2];

  dmd.clearScreen();
  dmd.drawString(0, 1, "ERR");
  itoa(code, codeBuf, 10);
  dmd.drawString(0, 17, codeBuf);
}

void dispLoad(uint8_t pcnt) {
  dmd.selectFont(Droid_Sans_12);

  char pcntBuf[2];

  dmd.clearScreen();
  dmd.drawString(0, 1, "LOAD");
  itoa(pcnt, pcntBuf, 10);
  dmd.drawString(0, 17, strcat(pcntBuf, "%"));
}

void backgroundUpdate() {
  if (!digitalRead(LOW_PIN)) {
    dmd.setBrightness(LOW_BRIGHTNESS);
  } else if (!digitalRead(HIGH_PIN)) {
    dmd.setBrightness(HIGH_BRIGHTNESS);
  } else {
    dmd.setBrightness(MED_BRIGHTNESS);
  }

  btn.tick();

  if (paused) {
    currentPic = euclidean_modulo(enc.read() / 4, files) + 1;
  } else if (settingsLoaded) {
    if (settingsScroll) {
      if (settingsSelectedItem != euclidean_modulo(enc.read() / 4, 2)) {
        settingsSelectedItem = euclidean_modulo(enc.read() / 4, 2);
        dmd.clearScreen();
        addSettingsItems();
        drawArrow(settingsSelectedItem);
      }
    } else if (settingsActiveItem == 0) {
      if (settingsCurrentValue != euclidean_modulo(enc.read() / 4, 1000)) {
        settingsCurrentValue = euclidean_modulo(enc.read() / 4, 1000);
        pageTime = settingsCurrentValue * 10;
        dmd.clearScreen();
        addSettingsItems();
        drawArrow(settingsSelectedItem);
      }
    }
  }
}

void onClick() {
  buzzer.sound(NOTE_C3, 10);
  if (!settingsLoaded) {
    paused = !paused;
  } else {
    if (settingsActiveItem == settingsSelectedItem) {
      enc.write(settingsSelectedItem * 4);
      settingsActiveItem = -1;
      settingsScroll = true;
    } else {
      settingsActiveItem = settingsSelectedItem;
      settingsScroll = false;

      if (settingsActiveItem == 0) {
        enc.write(pageTime / 10 * 4);
      }
    }
    dmd.clearScreen();
    addSettingsItems();
    drawArrow(settingsSelectedItem);
  }
}

void onLong() {
  if (!paused) {
    settingsLoaded = !settingsLoaded;
    dmd.clearScreen();
    buzzer.sound(NOTE_C3, 10);
    buzzer.sound(0, 5);
    buzzer.sound(NOTE_C3, 10);
  }

  if (settingsLoaded) {
    addSettingsItems();
    drawArrow(settingsSelectedItem);
  } else {
    settingsActiveItem = -1;
    settingsSelectedItem = 0;
    settingsScroll = true;
  }
}

void addSettingsItems() {
  dmd.selectFont(SystemFont5x7);
  dmd.drawString(0, 0, "SP");
  dmd.drawString(0, 9, "TB");

  dmd.selectFont(NumberFont3x5);
  dmd.drawString(14, 1, String(pageTime / 10));
}

void drawArrow(int pos) {
  dmd.selectFont(SystemFont5x7);
  if (pos == settingsActiveItem) {
    dmd.drawString(27, pos * 8 + pos, "<");
  } else {
    dmd.drawString(27, pos * 8 + pos, "(");
  }
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

int euclidean_modulo(int a, int b) {
  int m = a % b;
  if (m < 0) {
    // m += (b < 0) ? -b : b; // avoid this form: it is UB when b == INT_MIN
    m = (b < 0) ? m - b : m + b;
  }
  return m;
}