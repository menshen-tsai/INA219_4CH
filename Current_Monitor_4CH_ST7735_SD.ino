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
#include <SD.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "RTClib.h"
////#include "SdFat.h"
#include "config.h"


////#include "ESPDateTime.h"

const uint8_t SD_CS = 16; // SD chip select

Adafruit_INA219 ina219_0(0x40);
Adafruit_INA219 ina219_1(0x41);
Adafruit_INA219 ina219_2(0x44);
Adafruit_INA219 ina219_3(0x45);

Adafruit_INA219 INA219[4] = {Adafruit_INA219(0x40), Adafruit_INA219(0x41), Adafruit_INA219(0x44), Adafruit_INA219(0x45)};
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h


const int chipSelect = 16; 
File file;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

unsigned long drawTime = 0;

boolean ina219Status[4];
boolean sdStatus=false;
char filename[30];

// Set RTC_TYPE for file timestamps.
// 0 - millis()
// 1 - DS1307
// 2 - DS3231
// 3 - PCF8523
#define RTC_TYPE 0



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
void dateTime(uint16_t* date, uint16_t* time) {
  DateTime now = rtc.now();
  // Return date using FS_DATE macro to format fields.
  *date = FS_DATE(now.year(), now.month(), now.day());

  // Return time using FS_TIME macro to format fields.
  *time = FS_TIME(now.hour(), now.minute(), now.second());
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
  File root;
  
  Serial.begin(115200);
  while (!Serial) {
  }

  Serial.print(__FILE__);
  Serial.print(" created at ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);
  
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
  DateTime now = rtc.now();
  sprintf(filename, "INA219-%04d%02d%02d%02d%02d%02d.log",
                now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  printf("Filename: %s\n", filename);
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
  SD.dateTimeCallback(dateTime);

 if (!SD.begin(SD_CS)) {
   tft.drawString("SD Failed", 0,100, 2);
   Serial.println("SD.begin failed");
   sdStatus = false;
 } else {
    sdStatus = true;
    Serial.println("SD Initialized!");
 }
 

  if (sdStatus == true) {
    file = SD.open(filename, FILE_WRITE);
    {
      Serial.print(F("file.opened with filename: "));
      Serial.println(filename);
      Serial.println(file);
    }
    // Print current date time to file.
    file.print(F("Test file at: "));
    printNow(&file);
    file.println();

    file.close();
    // List files in SD root.
//    sd.ls(LS_DATE | LS_SIZE);
    root = SD.open("/");
    printDirectory(root, 0);

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
  File root;
  
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
    ina219_0S = String(ina219Measurement[0].shuntvoltage*1000,2) + "," +
                      String(ina219Measurement[0].busvoltage*1000,2) + "," +
                      String(ina219Measurement[0].current_mA,3) + "," +
                      String(ina219Measurement[0].power_mW,3);
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

    ina219_1S = String(ina219Measurement[1].shuntvoltage*1000,2) + "," +
                      String(ina219Measurement[1].busvoltage*1000,2) + "," +
                      String(ina219Measurement[1].current_mA,3) + "," +
                      String(ina219Measurement[1].power_mW,3);
  
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

    ina219_2S = String(ina219Measurement[2].shuntvoltage*1000,2) + "," +
                      String(ina219Measurement[2].busvoltage*1000,2) + "," +
                      String(ina219Measurement[2].current_mA,3) + "," +
                      String(ina219Measurement[2].power_mW,3);

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

    ina219_3S = String(ina219Measurement[3].shuntvoltage*1000,2) + "," +
                      String(ina219Measurement[3].busvoltage*1000,2) + "," +
                      String(ina219Measurement[3].current_mA,3) + "," +
                      String(ina219Measurement[3].power_mW,3);

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

  sprintf(buf, "%4d/%2d/%2d %2d:%02d:%02d", dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  tft.drawString(buf, 0, 100, 1);
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
//      sd.ls(LS_DATE|LS_SIZE);
      root = SD.open("/");
      printDirectory(root, 0);
    }
    // print to the serial port too:
    Serial.println(dataString);

    printf("Open %s for writting\n", filename);
    file = SD.open(filename, FILE_WRITE) ;
    file.print(dataString);
    file.println();
    file.close();

  }
  delay(1000);
}




void printDirectory(File dir, int numTabs) {
  while (true) {

    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.print(entry.size(), DEC);
      time_t cr = entry.getCreationTime();
      time_t lw = entry.getLastWrite();
      struct tm * tmstruct = localtime(&cr);
      Serial.printf("\tCREATION: %d-%02d-%02d %02d:%02d:%02d", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
      tmstruct = localtime(&lw);
      Serial.printf("\tLAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
    }
    entry.close();
  }
}
