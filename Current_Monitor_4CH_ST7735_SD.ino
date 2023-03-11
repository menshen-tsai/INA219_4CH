/*
 Font draw speed and flicker test, draws all numbers 0-999 in each font
 (0-99 in font 8)
 Average time in milliseconds to draw a character is shown in red
 A total of 2890 characters are drawn in each font (190 in font 8)
 
 Needs fonts 2, 4, 6, 7 and 8

 Make sure all the display driver and pin connections are correct by
 editing the User_Setup.h file in the TFT_eSPI library folder.

 Note that yield() or delay(0) must be called in long duration for/while
 loops to stop the ESP8266 watchdog triggering.

 #########################################################################
 ###### DON'T FORGET TO UPDATE THE User_Setup.h FILE IN THE LIBRARY ######
 #########################################################################
 */
#define DEBUG_NTPClient

#include <TFT_eSPI.h> // Graphics and font library for ILI9341 driver chip
#include <SPI.h>
////#include "SD.h"
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "RTClib.h"
#include "SdFat.h"
#include "config.h"


////#include "ESPDateTime.h"

Adafruit_INA219 ina219_0(0x40);
Adafruit_INA219 ina219_1(0x41);
Adafruit_INA219 ina219_2(0x44);
Adafruit_INA219 ina219_3(0x45);

Adafruit_INA219 INA219[4] = {Adafruit_INA219(0x40), Adafruit_INA219(0x41), Adafruit_INA219(0x44), Adafruit_INA219(0x45)};
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h


const int chipSelect = 16; 

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

unsigned long drawTime = 0;

boolean ina219Status[4];
boolean sdStatus=false;


// Set RTC_TYPE for file timestamps.
// 0 - millis()
// 1 - DS1307
// 2 - DS3231
// 3 - PCF8523
#define RTC_TYPE 0

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 1
/*
  Change the value of SD_CS_PIN if you are using SPI and
  your hardware does not use the default value, SS.
  Common values are:
  Arduino Ethernet shield: pin 4
  Sparkfun SD shield: pin 8
  Adafruit SD shields and modules: pin 10
*/
#define SDCARD_SS_PIN 16
// SDCARD_SS_PIN is defined for the built-in SD on some boards.
#ifndef SDCARD_SS_PIN
const uint8_t SD_CS_PIN = SS;
#else  // SDCARD_SS_PIN
// Assume built-in SD is used.
const uint8_t SD_CS_PIN = SDCARD_SS_PIN;
#endif  // SDCARD_SS_PIN

// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(25)

#if 0
// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif  ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif  // HAS_SDIO_CLASS
#endif

#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)


#if SD_FAT_TYPE == 0
SdFat sd;
File file;
#elif SD_FAT_TYPE == 1
SdFat32 sd;
File32 file;
#elif SD_FAT_TYPE == 2
SdExFat sd;
ExFile file;
#elif SD_FAT_TYPE == 3
SdFs sd;
FsFile file;
#else  // SD_FAT_TYPE
#error Invalid SD_FAT_TYPE
#endif  // SD_FAT_TYPE


#if RTC_TYPE == 0
RTC_Millis rtc;
#elif RTC_TYPE == 1
RTC_DS1307 rtc;
#elif RTC_TYPE == 2
RTC_DS3231 rtc;
#elif RTC_TYPE == 3
RTC_PCF8523 rtc;
#else  // RTC_TYPE == type
#error RTC_TYPE type not implemented.
#endif  // RTC_TYPE == type




typedef struct 
{
  float shuntvoltage = 0;
  float busvoltage = 0;
  float current_mA = 0;
  float loadvoltage = 0;
  float power_mW = 0;
} INA219Measurement;



INA219Measurement ina219Measurement[4];

//------------------------------------------------------------------------------
// Call back for file timestamps.  Only called for file create and sync().
void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10) {
  DateTime now = rtc.now();
  // Return date using FS_DATE macro to format fields.
  *date = FS_DATE(now.year(), now.month(), now.day());

  // Return time using FS_TIME macro to format fields.
  *time = FS_TIME(now.hour(), now.minute(), now.second());

  // Return low time bits in units of 10 ms, 0 <= ms10 <= 199.
  *ms10 = now.second() & 1 ? 100 : 0;
}

//------------------------------------------------------------------------------
#define error(msg) (Serial.println(F("error " msg)), false)
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
void printField(Print* pr, char sep, uint8_t v) {
  if (sep) {
    pr->write(sep);
  }
  if (v < 10) {
    pr->write('0');
  }
  pr->print(v);
}
//------------------------------------------------------------------------------
void printNow(Print* pr) {
  DateTime now = rtc.now();
  pr->print(now.year());
  printField(pr, '-',now.month());
  printField(pr, '-',now.day());
  printField(pr, ' ',now.hour());
  printField(pr, ':',now.minute());
  printField(pr, ':',now.second());
}
 
void setup(void) {
  unsigned long epochTime;
  
  Serial.begin(115200);
  while (!Serial) {
  }

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  timeClient.begin();
  timeClient.setTimeOffset(3600*8);
  timeClient.update();
  epochTime = timeClient.getEpochTime();
  rtc.begin(epochTime);

  Serial.print(" seconds since 1970: ");
  Serial.println(rtc.now().unixtime());
    
  // Initialize the INA219.
  // By default the initialization will use the largest range (32V, 2A).  However
  // you can call a setCalibration function to change this range (see comments).
  if (! ina219_0.begin()) {
    Serial.println("Failed to find INA219 0x40 chip");
    ina219Status[0] = false;
  } else {
    ina219Status[0] = true;
  }

  if (! ina219_1.begin()) {
    Serial.println("Failed to find INA219 0x41 chip");
    ina219Status[1] = false;
  } else {
    ina219Status[1] = true;
  }
  if (! ina219_2.begin()) {
    Serial.println("Failed to find INA219 0x44 chip");
    ina219Status[2] = false;
  } else {
    ina219Status[2] = true;
  }
  if (! ina219_3.begin()) {
    Serial.println("Failed to find INA219 0x45 chip");
    ina219Status[3] = false;
  } else {
    ina219Status[3] = true;
  }
  

  // To use a slightly lower 32V, 1A range (higher precision on amps):
  //ina219.setCalibration_32V_1A();
  // Or to use a lower 16V, 400mA range (higher precision on volts and amps):
  //ina219.setCalibration_16V_400mA();

  Serial.println("Measuring voltage and current with INA219 ...");


  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  //tft.drawString("1234567890123456", 0, 0, 2);
  //tft.drawString("CH1 (0x40)", 0, 0, 1);
  tft.drawString("mV", 28, 0, 1); 
  tft.drawString("mA", 64, 0, 1); 
  tft.drawString("mW", 108, 0, 1); 
  
  tft.drawString("1", 0, 20, 2);
  tft.drawString("2", 0, 40, 2);
  tft.drawString("3", 0, 60, 2);
  tft.drawString("4", 0, 80, 2);



  // Set callback
  FsDateTime::setCallback(dateTime);

  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
    tft.drawString("SD Failed", 0,100, 2);
  } else {
    sdStatus = true;
    Serial.println("SD Initialized!");
  }

  if (sdStatus == true) {
    if (!file.open("RtcTest.txt", FILE_WRITE)) {
      Serial.println(F("file.open failed"));
      return;
    }
    // Print current date time to file.
    file.print(F("Test file at: "));
    printNow(&file);
    file.println();

    file.close();
    // List files in SD root.
    sd.ls(LS_DATE | LS_SIZE);
    Serial.println(F("Done"));
  }
}

static uint32_t count = 0;

void loop() {
  char buf[20];
  static uint32_t i;
  float shuntvoltage = 0;
  float busvoltage = 0;
  float current_mA = 0;
  float loadvoltage = 0;
  float power_mW = 0;
  String ina219_0S, ina219_1S, ina219_2S, ina219_3S;
  
  DateTime dt = rtc.now();

  String currentDate = String(dt.year()) + "," + String(dt.month()) + "," + String(dt.day());
  String fullstring = currentDate + "," +
         String(dt.hour()) + "," + String(dt.minute()) + "," + String(dt.second()) + "," ;  

//  Serial.print(" seconds since 1970: ");
//  Serial.print(dt.unixtime());
//  Serial.print("\tCurrent Date/Time "); Serial.println(fullstring);
  if (ina219Status[0] == true) {

    
    ina219Measurement[0].shuntvoltage = ina219_0.getShuntVoltage_mV();
    ina219Measurement[0].busvoltage = ina219_0.getBusVoltage_V();
    ina219Measurement[0].current_mA = ina219_0.getCurrent_mA();
    ina219Measurement[0].power_mW = ina219_0.getPower_mW();
    ina219Measurement[0].loadvoltage = ina219Measurement[0].busvoltage + (ina219Measurement[0].shuntvoltage / 1000);
    ina219_0S = String(ina219Measurement[0].shuntvoltage) + "," +
                      String(ina219Measurement[0].busvoltage) + "," +
                      String(ina219Measurement[0].current_mA) + "," +
                      String(ina219Measurement[0].power_mW);
    sprintf(buf, "%4d ", int(ina219Measurement[0].busvoltage*1000));
    tft.drawString(buf, 12, 20, 2);
    sprintf(buf, "%4d ", int(ina219Measurement[0].current_mA));
    tft.drawString(buf, 48, 20, 2);
  
    sprintf(buf, "%5d", int(ina219Measurement[0].power_mW));
    tft.drawString(buf, 84, 20, 2);
  } else {
    ina219Measurement[0].shuntvoltage = -9999;
    ina219Measurement[0].busvoltage = -9999;
    ina219Measurement[0].current_mA = -9999;
    ina219Measurement[0].power_mW = -9999;

    ina219_0S = String(ina219Measurement[0].shuntvoltage) + "," +
                      String(ina219Measurement[0].busvoltage) + "," +
                      String(ina219Measurement[0].current_mA) + "," +
                      String(ina219Measurement[0].power_mW);
    

    tft.drawString("N/A", 12, 20, 2);
    tft.drawString("N/A", 48, 20, 2);
    tft.drawString("N/A", 84, 20, 2);
  }

  if (ina219Status[1] == true) {
    ina219Measurement[1].shuntvoltage = ina219_1.getShuntVoltage_mV();
    ina219Measurement[1].busvoltage = ina219_1.getBusVoltage_V();
    ina219Measurement[1].current_mA = ina219_1.getCurrent_mA();
    ina219Measurement[1].power_mW = ina219_1.getPower_mW();
    ina219Measurement[1].loadvoltage = ina219Measurement[1].busvoltage + (ina219Measurement[1].shuntvoltage / 1000);

    ina219_1S = String(ina219Measurement[1].shuntvoltage) + "," +
                      String(ina219Measurement[1].busvoltage) + "," +
                      String(ina219Measurement[1].current_mA) + "," +
                      String(ina219Measurement[1].power_mW);
  
    sprintf(buf, "%4d ", int(ina219Measurement[1].busvoltage*1000));
    tft.drawString(buf, 12, 40, 2);
    sprintf(buf, "%4d ", int(ina219Measurement[1].current_mA));
    tft.drawString(buf, 48, 40, 2);
  
    sprintf(buf, "%5d", int(ina219Measurement[1].power_mW));
    tft.drawString(buf, 84, 40, 2);
  } else {
    ina219Measurement[1].shuntvoltage = -9999;
    ina219Measurement[1].busvoltage = -9999;
    ina219Measurement[1].current_mA = -9999;
    ina219Measurement[1].power_mW = -9999;
    
    ina219_1S = String(ina219Measurement[1].shuntvoltage) + "," +
                      String(ina219Measurement[1].busvoltage) + "," +
                      String(ina219Measurement[1].current_mA) + "," +
                      String(ina219Measurement[1].power_mW);

    tft.drawString("N/A", 12, 40, 2);
    tft.drawString("N/A", 48, 40, 2);
    tft.drawString("N/A", 84, 40, 2);
  }

  if (ina219Status[2] == true) {
    ina219Measurement[2].shuntvoltage = ina219_2.getShuntVoltage_mV();
    ina219Measurement[2].busvoltage = ina219_2.getBusVoltage_V();
    ina219Measurement[2].current_mA = ina219_2.getCurrent_mA();
    ina219Measurement[2].power_mW = ina219_2.getPower_mW();
    ina219Measurement[2].loadvoltage = ina219Measurement[2].busvoltage + (ina219Measurement[2].shuntvoltage / 1000);

    ina219_2S = String(ina219Measurement[2].shuntvoltage) + "," +
                      String(ina219Measurement[2].busvoltage) + "," +
                      String(ina219Measurement[2].current_mA) + "," +
                      String(ina219Measurement[2].power_mW);

    sprintf(buf, "%4d ", int(ina219Measurement[2].busvoltage*1000));
    tft.drawString(buf, 12, 60, 2);
    sprintf(buf, "%4d ", int(ina219Measurement[2].current_mA));
    tft.drawString(buf, 48, 60, 2);
  
    sprintf(buf, "%5d", int(ina219Measurement[2].power_mW));
    tft.drawString(buf, 84, 60, 2);
  } else {
    ina219Measurement[2].shuntvoltage = -9999;
    ina219Measurement[2].busvoltage = -9999;
    ina219Measurement[2].current_mA = -9999;
    ina219Measurement[2].power_mW = -9999;
    
    ina219_2S = String(ina219Measurement[2].shuntvoltage) + "," +
                      String(ina219Measurement[2].busvoltage) + "," +
                      String(ina219Measurement[2].current_mA) + "," +
                      String(ina219Measurement[2].power_mW);

    tft.drawString("N/A", 12, 60, 2);
    tft.drawString("N/A", 48, 60, 2);
    tft.drawString("N/A", 84, 60, 2);
  }

  if (ina219Status[3] == true) {
    ina219Measurement[3].shuntvoltage = ina219_3.getShuntVoltage_mV();
    ina219Measurement[3].busvoltage = ina219_3.getBusVoltage_V();
    ina219Measurement[3].current_mA = ina219_3.getCurrent_mA();
    ina219Measurement[3].power_mW = ina219_3.getPower_mW();
    loadvoltage = ina219Measurement[3].busvoltage + (ina219Measurement[3].shuntvoltage / 1000);

    ina219_3S = String(ina219Measurement[3].shuntvoltage) + "," +
                      String(ina219Measurement[3].busvoltage) + "," +
                      String(ina219Measurement[3].current_mA) + "," +
                      String(ina219Measurement[3].power_mW);

    sprintf(buf, "%4d ", int(ina219Measurement[3].busvoltage*1000));
    tft.drawString(buf, 12, 80, 2);
    sprintf(buf, "%4d ", int(ina219Measurement[3].current_mA));
    tft.drawString(buf, 48, 80, 2);
  
    sprintf(buf, "%5d", int(ina219Measurement[3].power_mW));
    tft.drawString(buf, 84, 80, 2);
  } else {
    ina219Measurement[3].shuntvoltage = -9999;
    ina219Measurement[3].busvoltage = -9999;
    ina219Measurement[3].current_mA = -9999;
    ina219Measurement[3].power_mW = -9999;

    ina219_3S = String(ina219Measurement[3].shuntvoltage) + "," +
                      String(ina219Measurement[3].busvoltage) + "," +
                      String(ina219Measurement[3].current_mA) + "," +
                      String(ina219Measurement[3].power_mW);
    
    tft.drawString("N/A", 12, 80, 2);
    tft.drawString("N/A", 48, 80, 2);
    tft.drawString("N/A", 84, 80, 2);
  }

  sprintf(buf, "%4d/%2d/%2d %2d:%02d", dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute());
  tft.drawString(buf, 0, 100, 2);
  timeClient.update();

  String dataString = fullstring+ina219_0S+","+
                                ina219_1S+","+
                                ina219_2S+","+
                                ina219_3S      ;



  if (sdStatus == true) {
    count ++;
    if (count > 9) {
      count = 0;
      Serial.println("File lists");
      sd.ls(LS_DATE|LS_SIZE);
    }
    // print to the serial port too:
    Serial.println(dataString);

    Serial.println("Open ina219.txt for writting");
    if (!file.open("ina219.txt", FILE_WRITE)) {
      Serial.println(F("file.open failed"));
      delay(1000);
      return;
    }
    file.print(dataString);
    file.println();
    file.close();

  }
  delay(1000);
}
