/*
DMD Page Loader
Author: Kevin Ahr
*/

#include <DMD2.h>
#include <SPI.h>
#include <SdFat.h>

#include <ArduinoQueue.h>
#include <Buzzer.h>
#include <EEPROMex.h>
#include <MemoryFree.h>

#include "Arial_Black_16.h"
#include "Droid_Sans_12.h"
#include "NumberFont3x5.h"
#include "SystemFont5x7.h"

#include "Bitmaps.h"

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 3

// Maximum file name length (with extention)
#define MAX_FILE_LEN 15

#define BUZZER_PIN 22

#define FX_BAUD 57600
#define FX_RX_Q 25
#define FX_MAX_CMD 32
#define FX_LINE_ENDING '\n'
#define FX_LINE_ENDING_STR "\n"

// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(50)

#if defined(HAS_TEENSY_SDIO)
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif defined(RP_CLK_GPIO) && defined(RP_CMD_GPIO) && defined(RP_DAT0_GPIO)
// See the Rp2040SdioSetup example for RP2040/RP2350 boards.
#define SD_CONFIG SdioConfig(RP_CLK_GPIO, RP_CMD_GPIO, RP_DAT0_GPIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else // HAS_TEENSY_SDIO
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif // HAS_TEENSY_SDIO

// SDCARD_SS_PIN is defined for the built-in SD on some boards.
#ifndef SDCARD_SS_PIN
const uint8_t SD_CS_PIN = SS;
#else  // SDCARD_SS_PIN
// Assume built-in SD is used.
const uint8_t SD_CS_PIN = SDCARD_SS_PIN;
#endif // SDCARD_SS_PIN

#if SD_FAT_TYPE == 0
SdFat sd;
File root;
File file;
#elif SD_FAT_TYPE == 1
SdFat32 sd;
File32 root;
File32 file;
#elif SD_FAT_TYPE == 2
SdExFat sd;
ExFile root;
ExFile file;
#elif SD_FAT_TYPE == 3
SdFs sd;
FsFile root;
FsFile framesDir;
FsFile file;
#else // SD_FAT_TYPE
#error invalid SD_FAT_TYPE
#endif // SD_FAT_TYPE

#define VERSION "v2.0.0"

SoftDMD dmd(1, 2); // DMD controls the entire display(s)
DMDFrame frame = DMDFrame(dmd.width, dmd.height);

ArduinoQueue<char *> fxQueue(FX_RX_Q);

uint8_t fileBuffer[1025];
char fileName[MAX_FILE_LEN];
unsigned int frames = 0;

unsigned long previousMillis = 0;

int timebarPos = 1;
int pageTime = 0;
int pageTimeMult = 1;
int brightness = 127;

void wipeAni();

void saveSettingInt(const char *key, uint8_t value);

void loadSettings();

void loadPic(const uint8_t *pic);

void delayBar(unsigned int time);

void dispError(uint8_t code);

void dispLoad(const String &pcnt);

int EndsWith(const char *str, const char *suffix);

int euclidean_modulo(int a, int b);

bool inRange(int val, int minimum, int maximum);

void displayBitmap();

void displayBitmap(const uint16_t image_frame[]) {
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      int index = (y * 32 + x) / 16; // Index in the uint16_t array
      int bit_pos = x % 16;          // Bit position in the 16-bit word

      uint16_t value =
          pgm_read_word(&image_frame[index]);     // Read word from PROGMEM
      bool pixel = (value >> (15 - bit_pos)) & 1; // Extract correct bit
      dmd.setPixel(x, y, pixel ? GRAPHICS_ON : GRAPHICS_OFF);
    }
  }
}

void setup() {
  Serial.begin(FX_BAUD);

  dmd.begin();
  dmd.setBrightness(brightness);
  dmd.selectFont(Arial_Black_16);

  pinMode(BUZZER_PIN, OUTPUT);

  // Initialize the SD.
  if (!sd.begin(SD_CONFIG)) {
    dispError(1);
    while (true) {
    }
  }

  loadSettings();

  // Open root directory
  if (!root.open("/")) {
    dispError(2);
    while (true) {
    }
  }
  if (!sd.exists("/frames")) {
    sd.mkdir("/frames");
  }
  if (!framesDir.open("/frames/")) {
    dispError(3);
    while (true) {
    }
  }

  // count the number of files ending in .DMD
  char *filesChar = (char *)malloc(8);
  while (true) {
    int rc = file.openNext(&framesDir, O_READ);
    file.getName(fileName, sizeof(fileName));
    if (EndsWith(fileName, ".DMD")) {
      frames++;
      itoa(frames, filesChar, 10);
      dispLoad(filesChar);
    }
    file.close();
    if (!rc) {
      break;
    }
  }

  displayBitmap(splash);
  delay(4000);
}

void loop() {
  if (Serial.availableForWrite()) {
    Serial.print("memfree=");
    Serial.println(freeMemory());

    Serial.print("uptime=");
    Serial.println(millis());

    Serial.print("maxq=");
    Serial.println(fxQueue.maxQueueSize());

    Serial.print("qitems=");
    Serial.println(fxQueue.itemCount());
  }

  if (!fxQueue.isEmpty()) {
    char *fxRaw = (char *)malloc(FX_MAX_CMD);
    fxRaw = fxQueue.dequeue();
    // parse command=value
    char *fxCmd = strtok(fxRaw, "=");
    char *fxVal = strtok(NULL, FX_LINE_ENDING_STR);
    Serial.print("fxcmd=");
    Serial.println(fxCmd);
    Serial.print("fxval=");
    Serial.println(fxVal);

    // switch case for commands
    if (strcmp(fxCmd, "rewind") == 0) {
      framesDir.rewind();
    } else if (strcmp(fxCmd, "V") == 0) {
      Serial.print("version=");
      Serial.println(VERSION);
    } else if (strcmp(fxCmd, "formatsd") == 0) {
      sd.format(&Serial);
    } else if (strcmp(fxCmd, "brightness") == 0) {
      brightness = atoi(fxVal);
      dmd.setBrightness(brightness);
    } else if (strcmp(fxCmd, "timebarpos") == 0) {
      timebarPos = atoi(fxVal);
    } else if (strcmp(fxCmd, "pagetime") == 0) {
      pageTime = atoi(fxVal);
    } else if (strcmp(fxCmd, "save") == 0) {
      saveSettingInt("brightness", brightness);
      saveSettingInt("timebarPos", timebarPos);
      saveSettingInt("pageTime", pageTime);
    }
  }

  // Attempt to open the next file
  int rc = file.openNext(&framesDir, FILE_READ);
  if (!rc) {
    Serial.println("state=rewind");
    framesDir.rewind(); // Reset directory reading position
  }

  // Print file name
  file.getName(fileName, sizeof(fileName));

  // Serial.print("file=");
  // Serial.println(fileName);

  // Serial.println("state=animate");

  if (EndsWith(fileName, ".DMD")) {
    if (true) {
      file.read(fileBuffer, 1025);
      loadPic(fileBuffer);
      if (pageTime > 0) {
        pageTimeMult = fileBuffer[0];
        delayBar(pageTime / 32 * pageTimeMult);
      }
    }
  }
  file.close();
}

void serialEvent() {
  if (Serial.available()) {
    char *fx = (char *)malloc(FX_MAX_CMD);
    int bytesRead = Serial.readBytesUntil(FX_LINE_ENDING, fx, FX_MAX_CMD - 1);
    fx[bytesRead] = '\0';
    fxQueue.enqueue(fx);
    return;
  }
}

void saveSettingInt(const char *name, uint8_t value) {
  if (!sd.exists("/settings")) {
    sd.mkdir("/settings");
  }

  File settingFile = sd.open("/settings/" + String(name), O_WRONLY | O_TRUNC);
  char *valueStr = (char *)malloc(4);
  itoa(value, valueStr, 10);
  settingFile.write(valueStr);
  settingFile.close();
}

void loadSettings() {
  if (!sd.exists("/settings")) {
    sd.mkdir("/settings");
  }

  if (!sd.exists("/settings/brightness")) {
    File brightnessFile = sd.open("/settings/brightness", FILE_WRITE);
    brightnessFile.print(brightness);
    brightnessFile.close();
  }
  if (!sd.exists("/settings/timebarPos")) {
    File brightnessFile = sd.open("/settings/timebarPos", FILE_WRITE);
    brightnessFile.print(timebarPos);
    brightnessFile.close();
  }
  if (!sd.exists("/settings/pageTime")) {
    File brightnessFile = sd.open("/settings/pageTime", FILE_WRITE);
    brightnessFile.print(pageTime);
    brightnessFile.close();
  }

  File brightnessFile = sd.open("/settings/brightness", FILE_READ);
  brightness = brightnessFile.parseInt();
  dmd.setBrightness(brightness);
  brightnessFile.close();

  File timebarPosFile = sd.open("/settings/timebarPos", FILE_READ);
  timebarPos = timebarPosFile.parseInt();
  timebarPosFile.close();

  File pageTimeFile = sd.open("/settings/pageTime", FILE_READ);
  pageTime = pageTimeFile.parseInt();
  pageTimeFile.close();
}

void loadPic(const uint8_t *pic) {
  const uint8_t *p = pic + 1; // Start from index 1

  for (unsigned int y = 0; y < 32; y++) {
    for (unsigned int x = 0; x < 32; x++) {
      frame.setPixel(x, y, (*p++) ? GRAPHICS_ON : GRAPHICS_OFF);
    }
  }

  dmd.copyFrame(frame, 0, 0);
}

void delayBar(unsigned int time) {
  // tone(BUZZER_PIN, 440);
  for (int i = 0; i < 32; i++) {
    if (timebarPos == 1) {
      dmd.setPixel(i, 31, GRAPHICS_XOR);
    } else if (timebarPos == 2) {
      dmd.setPixel(i, 0, GRAPHICS_XOR);
    }
    while (1) {
      unsigned long currentMillis = millis();

      if (currentMillis - previousMillis >= time) {
        previousMillis = currentMillis;
        break;
      }
    }
    // noTone(BUZZER_PIN);
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

void dispLoad(const String &pcnt) {
  dmd.selectFont(Droid_Sans_12);
  dmd.drawString(0, 1, "FILE");
  dmd.drawString(0, 17, pcnt);
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

bool inRange(int val, int minimum, int maximum) {
  return ((minimum <= val) && (val <= maximum));
}