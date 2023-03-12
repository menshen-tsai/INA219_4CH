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
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <NTPClient.h>
#include <WiFiUdp.h>
#include "RTClib.h"
////#include "SdFat.h"
#include "config.h"

#define USE_SDFS
#define INCLUDE_FALLBACK_INDEX_HTM

#ifdef INCLUDE_FALLBACK_INDEX_HTM
#include "extras/index_htm.h"
#endif

#include <SDFS.h>
const char* fsName = "SDFS";
FS* fileSystem = &SDFS;
SDFSConfig fileSystemConfig = SDFSConfig(16);
// fileSystemConfig.setCSPin(chipSelectPin);

#define DBG_OUTPUT_PORT Serial

////#include "ESPDateTime.h"

const uint8_t SD_CS = 16; // SD chip select

Adafruit_INA219 ina219_0(0x40);
Adafruit_INA219 ina219_1(0x41);
Adafruit_INA219 ina219_2(0x44);
Adafruit_INA219 ina219_3(0x45);

Adafruit_INA219 INA219[4] = {Adafruit_INA219(0x40), Adafruit_INA219(0x41), Adafruit_INA219(0x44), Adafruit_INA219(0x45)};
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

ESP8266WebServer server(80);

static bool fsOK;
String unsupportedFiles = String();

File uploadFile;

static const char TEXT_PLAIN[] PROGMEM = "text/plain";
static const char FS_INIT_ERROR[] PROGMEM = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";
const char* host = "fsbrowser";

const int chipSelect = 16; 
File file;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

unsigned long drawTime = 0;

boolean ina219Status[4];
boolean sdStatus=false;
char logFilename[30];

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

////////////////////////////////
// Utils to return HTTP codes, and determine content-type

void replyOK() {
  server.send(200, FPSTR(TEXT_PLAIN), "");
}

void replyOKWithMsg(String msg) {
  server.send(200, FPSTR(TEXT_PLAIN), msg);
}

void replyNotFound(String msg) {
  server.send(404, FPSTR(TEXT_PLAIN), msg);
}

void replyBadRequest(String msg) {
  DBG_OUTPUT_PORT.println(msg);
  server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void replyServerError(String msg) {
  DBG_OUTPUT_PORT.println(msg);
  server.send(500, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

////////////////////////////////
// Request handlers

/*
   Return the FS type, status and size info
*/
void handleStatus() {
  DBG_OUTPUT_PORT.println("handleStatus");
  FSInfo fs_info;
  String json;
  json.reserve(128);

  json = "{\"type\":\"";
  json += fsName;
  json += "\", \"isOk\":";
  if (fsOK) {
    fileSystem->info(fs_info);
    json += F("\"true\", \"totalBytes\":\"");
    json += fs_info.totalBytes;
    json += F("\", \"usedBytes\":\"");
    json += fs_info.usedBytes;
    json += "\"";
  } else {
    json += "\"false\"";
  }
  json += F(",\"unsupportedFiles\":\"");
  json += unsupportedFiles;
  json += "\"}";

  server.send(200, "application/json", json);
}


/*
   Return the list of files in the directory specified by the "dir" query string parameter.
   Also demonstrates the use of chunked responses.
*/
void handleFileList() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  if (!server.hasArg("dir")) {
    return replyBadRequest(F("DIR ARG MISSING"));
  }

  String path = server.arg("dir");
  if (path != "/" && !fileSystem->exists(path)) {
    return replyBadRequest("BAD PATH");
  }

  DBG_OUTPUT_PORT.println(String("handleFileList: ") + path);
  Dir dir = fileSystem->openDir(path);
  path.clear();

  // use HTTP/1.1 Chunked response to avoid building a huge temporary string
  if (!server.chunkedResponseModeStart(200, "text/json")) {
    server.send(505, F("text/html"), F("HTTP1.1 required"));
    return;
  }

  // use the same string for every line
  String output;
  output.reserve(64);
  while (dir.next()) {
#ifdef USE_SPIFFS
    String error = checkForUnsupportedPath(dir.fileName());
    if (error.length() > 0) {
      DBG_OUTPUT_PORT.println(String("Ignoring ") + error + dir.fileName());
      continue;
    }
#endif
    if (output.length()) {
      // send string from previous iteration
      // as an HTTP chunk
      server.sendContent(output);
      output = ',';
    } else {
      output = '[';
    }

    output += "{\"type\":\"";
    if (dir.isDirectory()) {
      output += "dir";
    } else {
      output += F("file\",\"size\":\"");
      output += dir.fileSize();
    }

    output += F("\",\"name\":\"");
    // Always return names without leading "/"
    if (dir.fileName()[0] == '/') {
      output += &(dir.fileName()[1]);
    } else {
      output += dir.fileName();
    }

    output += "\"}";
  }

  // send last string
  output += "]";
  server.sendContent(output);
  server.chunkedResponseFinalize();
}


/*
   Read the given file from the filesystem and stream it back to the client
*/
bool handleFileRead(String path) {
  DBG_OUTPUT_PORT.println(String("handleFileRead: ") + path);
  if (!fsOK) {
    replyServerError(FPSTR(FS_INIT_ERROR));
    return true;
  }

  if (path.endsWith("/")) {
    path += "index.htm";
  }

  String contentType;
  if (server.hasArg("download")) {
    contentType = F("application/octet-stream");
  } else {
    contentType = mime::getContentType(path);
  }

  if (!fileSystem->exists(path)) {
    // File not found, try gzip version
    path = path + ".gz";
  }
  if (fileSystem->exists(path)) {
    File file = fileSystem->open(path, "r");
    if (server.streamFile(file, contentType) != file.size()) {
      DBG_OUTPUT_PORT.println("Sent less data than expected!");
    }
    file.close();
    return true;
  }

  return false;
}


/*
   As some FS (e.g. LittleFS) delete the parent folder when the last child has been removed,
   return the path of the closest parent still existing
*/
String lastExistingParent(String path) {
  while (!path.isEmpty() && !fileSystem->exists(path)) {
    if (path.lastIndexOf('/') > 0) {
      path = path.substring(0, path.lastIndexOf('/'));
    } else {
      path = String();  // No slash => the top folder does not exist
    }
  }
  DBG_OUTPUT_PORT.println(String("Last existing parent: ") + path);
  return path;
}

/*
   Handle the creation/rename of a new file
   Operation      | req.responseText
   ---------------+--------------------------------------------------------------
   Create file    | parent of created file
   Create folder  | parent of created folder
   Rename file    | parent of source file
   Move file      | parent of source file, or remaining ancestor
   Rename folder  | parent of source folder
   Move folder    | parent of source folder, or remaining ancestor
*/
void handleFileCreate() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  String path = server.arg("path");
  if (path.isEmpty()) {
    return replyBadRequest(F("PATH ARG MISSING"));
  }

#ifdef USE_SPIFFS
  if (checkForUnsupportedPath(path).length() > 0) {
    return replyServerError(F("INVALID FILENAME"));
  }
#endif

  if (path == "/") {
    return replyBadRequest("BAD PATH");
  }
  if (fileSystem->exists(path)) {
    return replyBadRequest(F("PATH FILE EXISTS"));
  }

  String src = server.arg("src");
  if (src.isEmpty()) {
    // No source specified: creation
    DBG_OUTPUT_PORT.println(String("handleFileCreate: ") + path);
    if (path.endsWith("/")) {
      // Create a folder
      path.remove(path.length() - 1);
      if (!fileSystem->mkdir(path)) {
        return replyServerError(F("MKDIR FAILED"));
      }
    } else {
      // Create a file
      File file = fileSystem->open(path, "w");
      if (file) {
        file.write((const char *)0);
        file.close();
      } else {
        return replyServerError(F("CREATE FAILED"));
      }
    }
    if (path.lastIndexOf('/') > -1) {
      path = path.substring(0, path.lastIndexOf('/'));
    }
    replyOKWithMsg(path);
  } else {
    // Source specified: rename
    if (src == "/") {
      return replyBadRequest("BAD SRC");
    }
    if (!fileSystem->exists(src)) {
      return replyBadRequest(F("SRC FILE NOT FOUND"));
    }

    DBG_OUTPUT_PORT.println(String("handleFileCreate: ") + path + " from " + src);

    if (path.endsWith("/")) {
      path.remove(path.length() - 1);
    }
    if (src.endsWith("/")) {
      src.remove(src.length() - 1);
    }
    if (!fileSystem->rename(src, path)) {
      return replyServerError(F("RENAME FAILED"));
    }
    replyOKWithMsg(lastExistingParent(src));
  }
}


/*
   Delete the file or folder designed by the given path.
   If it's a file, delete it.
   If it's a folder, delete all nested contents first then the folder itself

   IMPORTANT NOTE: using recursion is generally not recommended on embedded devices and can lead to crashes (stack overflow errors).
   This use is just for demonstration purpose, and FSBrowser might crash in case of deeply nested filesystems.
   Please don't do this on a production system.
*/
void deleteRecursive(String path) {
  File file = fileSystem->open(path, "r");
  bool isDir = file.isDirectory();
  file.close();

  // If it's a plain file, delete it
  if (!isDir) {
    fileSystem->remove(path);
    return;
  }

  // Otherwise delete its contents first
  Dir dir = fileSystem->openDir(path);

  while (dir.next()) {
    deleteRecursive(path + '/' + dir.fileName());
  }

  // Then delete the folder itself
  fileSystem->rmdir(path);
}


/*
   Handle a file deletion request
   Operation      | req.responseText
   ---------------+--------------------------------------------------------------
   Delete file    | parent of deleted file, or remaining ancestor
   Delete folder  | parent of deleted folder, or remaining ancestor
*/
void handleFileDelete() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  String path = server.arg(0);
  if (path.isEmpty() || path == "/") {
    return replyBadRequest("BAD PATH");
  }

  DBG_OUTPUT_PORT.println(String("handleFileDelete: ") + path);
  if (!fileSystem->exists(path)) {
    return replyNotFound(FPSTR(FILE_NOT_FOUND));
  }
  deleteRecursive(path);

  replyOKWithMsg(lastExistingParent(path));
}

/*
   Handle a file upload request
*/
void handleFileUpload() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }
  if (server.uri() != "/edit") {
    return;
  }
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    // Make sure paths always start with "/"
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    DBG_OUTPUT_PORT.println(String("handleFileUpload Name: ") + filename);
    uploadFile = fileSystem->open(filename, "w");
    if (!uploadFile) {
      return replyServerError(F("CREATE FAILED"));
    }
    DBG_OUTPUT_PORT.println(String("Upload: START, filename: ") + filename);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t bytesWritten = uploadFile.write(upload.buf, upload.currentSize);
      if (bytesWritten != upload.currentSize) {
        return replyServerError(F("WRITE FAILED"));
      }
    }
    DBG_OUTPUT_PORT.println(String("Upload: WRITE, Bytes: ") + upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
    DBG_OUTPUT_PORT.println(String("Upload: END, Size: ") + upload.totalSize);
  }
}


/*
   The "Not Found" handler catches all URI not explicitly declared in code
   First try to find and return the requested file from the filesystem,
   and if it fails, return a 404 page with debug information
*/
void handleNotFound() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  String uri = ESP8266WebServer::urlDecode(server.uri()); // required to read paths with blanks

  if (handleFileRead(uri)) {
    return;
  }

  // Dump debug data
  String message;
  message.reserve(100);
  message = F("Error: File not found\n\nURI: ");
  message += uri;
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += server.args();
  message += '\n';
  for (uint8_t i = 0; i < server.args(); i++) {
    message += F(" NAME:");
    message += server.argName(i);
    message += F("\n VALUE:");
    message += server.arg(i);
    message += '\n';
  }
  message += "path=";
  message += server.arg("path");
  message += '\n';
  DBG_OUTPUT_PORT.print(message);

  return replyNotFound(message);
}

/*
   This specific handler returns the index.htm (or a gzipped version) from the /edit folder.
   If the file is not present but the flag INCLUDE_FALLBACK_INDEX_HTM has been set, falls back to the version
   embedded in the program code.
   Otherwise, fails with a 404 page with debug information
*/
void handleGetEdit() {
  if (handleFileRead(F("/edit/index.htm"))) {
    return;
  }

#ifdef INCLUDE_FALLBACK_INDEX_HTM
  server.sendHeader(F("Content-Encoding"), "gzip");
  server.send(200, "text/html", index_htm_gz, index_htm_gz_len);
#else
  replyNotFound(FPSTR(FILE_NOT_FOUND));
#endif

}

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
  IPAddress ip;
  char bufIP[20];
  Serial.begin(115200);
  while (!Serial) {
  }

  Serial.print(__FILE__);
  Serial.print(" created at ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);


  ////////////////////////////////
  // FILESYSTEM INIT
////  fileSystemConfig.setCSPin(16);
  fileSystemConfig.setAutoFormat(false);
  fileSystem->setConfig(fileSystemConfig);
  fsOK = fileSystem->begin();
  DBG_OUTPUT_PORT.println(fsOK ? F("Filesystem initialized.") : F("Filesystem init failed!"));

  
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  ip = WiFi.localIP();
  Serial.print("IP: "); Serial.println(ip);
  sprintf(bufIP, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  Serial.println(bufIP);

  ////////////////////////////////
  // MDNS INIT
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print(F("Open http://"));
    Serial.print(host);
    Serial.println(F(".local/edit to open the FileSystem Browser"));
  }
  ////////////////////////////////
  // WEB SERVER INIT

  // Filesystem status
  server.on("/status", HTTP_GET, handleStatus);

  // List directory
  server.on("/list", HTTP_GET, handleFileList);

  // Load editor
  server.on("/edit", HTTP_GET, handleGetEdit);

  // Create file
  server.on("/edit",  HTTP_PUT, handleFileCreate);

  // Delete file
  server.on("/edit",  HTTP_DELETE, handleFileDelete);

  // Upload file
  // - first callback is called after the request has ended with all parsed arguments
  // - second callback handles file upload at that location
  server.on("/edit",  HTTP_POST, replyOK, handleFileUpload);

  // Default handler for all URIs not defined above
  // Use it to read files from filesystem
  server.onNotFound(handleNotFound);

  // Start server
  server.begin();
  Serial.println("HTTP server started");
  
  timeClient.begin();
  timeClient.setTimeOffset(3600*8);
  timeClient.update();
  epochTime = timeClient.getEpochTime();
  rtc.begin(epochTime);

  Serial.print(" seconds since 1970: ");
  Serial.println(rtc.now().unixtime());
  DateTime now = rtc.now();
  sprintf(logFilename, "INA219-%04d%02d%02d%02d%02d%02d.log",
                now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  printf("Filename: %s\n", logFilename);
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


  tft.drawString(bufIP, 0, 115, 1);
  
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
    file = SD.open(logFilename, FILE_WRITE);
    {
      Serial.print(F("file.opened with filename: "));
      Serial.println(logFilename);
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

  server.handleClient();
  MDNS.update();
  
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

    printf("Open %s for writting\n", logFilename);
    file = SD.open(logFilename, FILE_WRITE) ;
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
