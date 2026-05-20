// board_build.partitions = huge_app.csv
// monitor_speed = 115200
// board_build.filesystem = littlefs
// --------------------------------------------------

#include "Arduino.h"
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

// BLE UUIDs
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_RX "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHARACTERISTIC_UUID_TX "1ccecade-7d72-46cb-8dc5-523e1e92ebdb"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

Preferences preferences;

WebServer server(80);

// Command queue (from BLE callback to main loop)
volatile char pendingCommand = 'Z';
volatile int pendingValue = 0;
volatile float pendingFloatValue = 0.0;
String pendingPassword = "";

// init PINs: assign any pin on ESP32
#define stp 17
#define dir 4
#define MS1 18
#define MS2 27
#define EN 25

// Parameters
char commande = '0';
char valuechar = '0';
int value = 0;
int currentPosition = 1000000;
int startPosition = currentPosition - 1;
int endPosition = currentPosition + 1;
float StepperMinDegree = 1.8; // pas mimimum du moteur en degree
int StepperAngleDiv = 8;      // 1 2 4 ou 8
int CurrentDriverResolution = StepperAngleDiv;
int thread_size = 700; // in um. M3 = 600 M4 = 700 M5 = 800
int CameraSteps = 20;  // in um, lenght between focal plane
int attente = 4000;    // Attente avant photo (en ms)
bool direction = 1;
unsigned long lastmillis;
float lensAperture = 3.5;
int progress = 0;
bool InvertSide = 1; // If motor is moving in the wrong direction
float Magnification = 10.0;

// Sony function
volatile int counter;
String connectedCameraSSID = "";
const char *ssid = "DIRECT-CeE0:ILCE-7RM2";
const char *ssid2 = "DIRECT-mgE0:ILCE-6300";
const char *password = "9E8EqQDV"; // your WPA2 password. Get it on Sony camera
                                   // (connect with password procedure)
char cameraPassword[64] = "qXb1X35h";
const char *host = "192.168.122.1"; // fixed IP of camera
const int httpPort = 8080;
char JSON_1[] =
    "{\"version\":\"1.0\",\"id\":1,\"method\":\"getVersions\",\"params\":[]}";
char JSON_2[] =
    "{\"version\":\"1.0\",\"id\":1,\"method\":\"startRecMode\",\"params\":[]}";
char JSON_3[] = "{\"version\":\"1.0\",\"id\":1,\"method\":"
                "\"startBulbShooting\",\"params\":[]}";
char JSON_4[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"stopBulbShooting\","
                "\"params\":[]}";
char JSON_5[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"actTakePicture\","
                "\"params\":[]}";
char JSON_10[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setShutterSpeed\","
                 "\"params\":[\"1/160\"]}";
char JSON_11[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setIsoSpeedRate\","
                 "\"params\":[\"100\"]}";
char JSON_13[] =
    "{\"version\":\"1.0\",\"id\":1,\"method\":\"startLiveview\",\"params\":[]}";
char JSON_14[] =
    "{\"version\":\"1.0\",\"id\":1,\"method\":\"stopLiveview\",\"params\":[]}";
char JSON_15[] = "{\"version\":\"1.0\",\"id\":1,\"method\":"
                 "\"getSupportedIsoSpeedRate\",\"params\":[]}";
char JSON_16[] = "{\"version\":\"1.0\",\"id\":1,\"method\":"
                 "\"setCameraFunction\",\"params\":[\"Remote Shooting\"]}";
char JSON_17[] = "{\"version\":\"1.0\",\"id\":1,\"method\":"
                 "\"getAvailableCameraFunction\",\"params\":[]}";
char JSON_18[] = "{\"version\":\"1.0\",\"id\":1,\"method\":"
                 "\"getAvailableApiList\",\"params\":[]}";

WiFiClient client;

// BLE Callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) { deviceConnected = true; };
  void onDisconnect(BLEServer *pServer) { deviceConnected = false; }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      Serial.print("Received BLE Value: ");
      for (int i = 0; i < rxValue.length(); i++) {
        Serial.print(rxValue[i]);
      }
      Serial.println();

      char cmd = rxValue[0];
      String payload = String(rxValue.c_str()).substring(1);

      if (cmd == 'P') {
        pendingPassword = payload;
        pendingCommand = 'P';
      } else if (cmd == 'Q') {
        pendingFloatValue = payload.toFloat();
        pendingCommand = cmd;
      } else {
        pendingValue = payload.toInt();
        pendingFloatValue = payload.toFloat();
        pendingCommand = cmd;
      }
    }
  }
};

int ConvDistStep(int distance); // Forward declaration

void SendParameter(int progress, int CameraSteps, int EstimatedTime,
                   int CurrentTime, int TotalPictures) {
  String stringToSend = String(progress) + "#" + String(CameraSteps) + "#" +
                        String(EstimatedTime) + "#" + String(CurrentTime) +
                        "#" + String(TotalPictures);
  // Send via BLE notification
  if (deviceConnected && pTxCharacteristic) {
    pTxCharacteristic->setValue((uint8_t *)stringToSend.c_str(),
                                stringToSend.length());
    pTxCharacteristic->notify();
  }
}

void SendLog(String message) {
  String stringToSend = "L#" + message;
  if (deviceConnected && pTxCharacteristic) {
    pTxCharacteristic->setValue((uint8_t *)stringToSend.c_str(),
                                stringToSend.length());
    pTxCharacteristic->notify();
  }
}

void SetMagnification(float magnification, float aperture) {
  CameraSteps =
      (int)roundf(2.2f * aperture * aperture * (magnification + 1.0f) *
                  (magnification + 1.0f) /
                  (3.0f * magnification *
                   magnification)); // https://www.zerenesystems.com/cms/stacker/docs/tables/macromicrodof
                                    // reduced by 3 to get better result
  int diff = abs(endPosition - startPosition);
  int TotalPictures = diff / ConvDistStep(CameraSteps) + 1;
  SendParameter(0, CameraSteps, 0, 0, TotalPictures);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Sony
//////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// stopSetupWifi removed to keep AP alive

String httpPost(const char *jString) {
  String response = "";
  if (!client.connect(host, httpPort)) {
    return response;
  }
  String url = "/sony/camera";

  client.print(String("POST " + url + " HTTP/1.1\r\n")); /// A6300
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(strlen(jString));
  client.println();
  client.println(jString);

  unsigned long startT = millis();
  while (!client.available() && millis() - startT < 8000) {
  } // wait 8s max for answer

  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.println(line);
    response += line;
  }
  client.stop();
  return response;
}

int ConnectCamera() {
  SendLog("Scanning for Sony camera...");
  String targetSSID = "";

  for (int retries = 0; retries < 6; retries++) {
    int n = WiFi.scanNetworks();
    if (n > 0) {
      for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i).indexOf("ILCE") >= 0) {
          targetSSID = WiFi.SSID(i);
          break;
        }
      }
    }
    if (targetSSID != "") break;
    
    SendLog("Not found, retrying scan...");
    delay(2000);
  }

  if (targetSSID == "") {
    Serial.println("No Sony ILCE camera found.");
    SendLog("No Sony camera found.");
    return 0;
  }

  Serial.println(WiFi.status());
  SendLog("Connecting to " + targetSSID + "...");
  connectedCameraSSID = targetSSID;
  WiFi.begin(targetSSID.c_str(), cameraPassword);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) { // wait 10s max
    delay(500);
    Serial.println(WiFi.status());
    timeout++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi! Wrong password?");
    SendLog("Failed to connect. Wrong password?");
    return 0;
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  SendLog("WiFi connected! IP: " + WiFi.localIP().toString());

  httpPost(JSON_1); // initial connect to camera
  httpPost(JSON_2); // startRecMode
  
  // Wait a bit for the camera to switch to record mode before requesting settings
  delay(1500);

  // Set ISO to minimum available (but at least 100)
  String isoResp = httpPost(JSON_15);
  int bracketIdx = isoResp.indexOf("[[");
  if (bracketIdx > 0) {
    int startIdx = bracketIdx + 2;
    int endIdx = isoResp.indexOf("]]", startIdx);
    if (endIdx > startIdx) {
      String arrayContent = isoResp.substring(startIdx, endIdx);
      int firstDigitIdx = -1;
      for (int i = 0; i < arrayContent.length(); i++) {
        if (isDigit(arrayContent[i])) {
          firstDigitIdx = i;
          break;
        }
      }
      if (firstDigitIdx >= 0) {
        int endNumIdx = arrayContent.indexOf("\"", firstDigitIdx);
        if (endNumIdx > firstDigitIdx) {
          String minIsoStr = arrayContent.substring(firstDigitIdx, endNumIdx);
          int minIso = minIsoStr.toInt();
          int targetIso = (minIso < 100) ? 100 : minIso;
          
          String setIsoCmd = "{\"version\":\"1.0\",\"id\":1,\"method\":"
                             "\"setIsoSpeedRate\",\"params\":[\"" +
                             String(targetIso) + "\"]}";
          httpPost(setIsoCmd.c_str());
          SendLog("ISO set to: " + String(targetIso));
        }
      }
    }
  }

  SendLog("Sony Camera Ready!");
  return 1;
}

int DisconnectCamera() {
  Serial.println(WiFi.status());
  WiFi.disconnect();
  SendLog("Disconnected from camera.");
  return 1;
}

int StopLiveView() {
  httpPost(JSON_14);
  return 1;
}

void TakePicture() {
  httpPost(JSON_5); // actTakePicture
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Motor
//////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void resetEDPins() {
  digitalWrite(stp, LOW);
  digitalWrite(dir, LOW);
  digitalWrite(MS1, LOW);
  digitalWrite(MS2, LOW);
  digitalWrite(EN, HIGH);
}

void ResolutionMoteur(int Resolution) {
  CurrentDriverResolution =
      Resolution; // Set the stepper driver resolution/divider

  if (Resolution == 8) {
    digitalWrite(MS1, HIGH); // Pull MS1, and MS2 high to set logic to 1/8th
                             // microstep resolution
    digitalWrite(MS2, HIGH);
  } else if (Resolution == 4) {
    digitalWrite(MS1, LOW);
    digitalWrite(MS2, HIGH);
  } else if (Resolution == 2) {
    digitalWrite(MS1, HIGH);
    digitalWrite(MS2, LOW);
  } else if (Resolution == 1) {
    digitalWrite(MS1, LOW);
    digitalWrite(MS2, LOW);
  }
}

int ConvDistStep(int distance) // Convert dist (1um unity) to steps
{
  int PasCalc = 0;
  PasCalc = (int)distance * 360 * CurrentDriverResolution /
            (thread_size * StepperMinDegree);
  if (PasCalc < 1) {
    PasCalc = 1;
  }
  Serial.println("ConvDistStep ");
  Serial.println(PasCalc);
  return PasCalc;
}

void TurnMotor(int Step) {
  int x;
  digitalWrite(EN, LOW);

  // Ramp parameters for smooth acceleration/deceleration
  int maxDelay = 3000; // Starting/ending speed in microseconds (slow, gentle)
  int minDelay = 700;  // Top speed in microseconds (fast)
  int delayChange = 20; // Change in delay per step (acceleration rate)
  
  int accelSteps = (maxDelay - minDelay) / delayChange;
  if (accelSteps > Step / 2) {
      accelSteps = Step / 2; // Cap acceleration steps if the movement is very short
  }

  for (x = 0; x < Step; x++) {
    if (pendingCommand == 'A' && pendingValue == 1) {
      SendLog("Motor movement stopped by user.");
      pendingCommand = 'Z';
      break;
    }
    
    // Calculate current speed (delay) based on where we are in the movement
    int currentDelay;
    if (x < accelSteps) {
        // Acceleration phase
        currentDelay = maxDelay - (x * delayChange);
    } else if (x >= Step - accelSteps) {
        // Deceleration phase
        int stepsFromEnd = Step - 1 - x;
        currentDelay = maxDelay - (stepsFromEnd * delayChange);
    } else {
        // Constant max speed phase
        currentDelay = maxDelay - (accelSteps * delayChange);
    }
    
    digitalWrite(stp, HIGH); // Trigger one step forward
    delayMicroseconds(currentDelay);
    digitalWrite(stp, LOW); // Pull step pin low so it can be triggered again
    delayMicroseconds(currentDelay);
    if (direction) {
      currentPosition = currentPosition - 8 / CurrentDriverResolution;
    } else {
      currentPosition = currentPosition + 8 / CurrentDriverResolution;
    }
  }
}

int DefinePos(int val) {
  if (val == 0) {
    startPosition = currentPosition;
  } else {
    endPosition = currentPosition;
  }
  // Recalculate and send new Total Photos to UI
  SetMagnification(Magnification, lensAperture);
  return 1;
}

int Avance(int val) // val is a distance
{
  direction = 0;
  digitalWrite(dir,
               HIGH ^ InvertSide); // Pull direction pin low to move "forward"
  TurnMotor(val);
  return 1;
}

int Recule(int val) {
  direction = 1;
  digitalWrite(dir,
               LOW ^ InvertSide); // Pull direction pin low to move "forward"
  TurnMotor(val);
  return 1;
}

int Move(int val) {
  Serial.print("Move val ");
  Serial.println(val);
  ResolutionMoteur(1);
  switch (val) {
  case 1:
    Avance(ConvDistStep(100)); // 0.1mm
    break;
  case 2:
    Avance(ConvDistStep(1000)); // 1mm
    break;
  case 3:
    Avance(ConvDistStep(10000)); // 10mm
    break;
  }
  ResolutionMoteur(StepperAngleDiv);
  return 1;
}

int MoveNeg(int val) {
  ResolutionMoteur(1);
  switch (val) {
  case 1:
    Recule(ConvDistStep(100)); // 0.1mm
    break;
  case 2:
    Recule(ConvDistStep(1000)); // 1mm
    break;
  case 3:
    Recule(ConvDistStep(10000)); // 10mm
    break;
  }
  ResolutionMoteur(StepperAngleDiv);
  return 1;
}

void GoTo(int val) {
  int diff = val - currentPosition;
  Serial.println("Diff ");
  Serial.println(diff);
  if (diff > 0) {
    Avance(diff);
  } else {
    Recule(abs(diff));
  }
}

void GoToStartEnd(int val) {
  if (val == 0) {
    GoTo(startPosition);
  } else {
    GoTo(endPosition);
  }
}

void GoToCamera(int val) {
  int diff = val - currentPosition; // number of steps to do
  if (diff < 0) {
    return;
  }
  int PictureNumber =
      (int)diff / ConvDistStep(CameraSteps); // calculate the picture number
  Serial.print("DiffCamera");
  Serial.println(diff);
  Serial.print("PictureNumber");
  Serial.println(PictureNumber);

  direction = 0; //
  digitalWrite(dir,
               HIGH ^ InvertSide); // Pull direction pin low to move "forward"

  unsigned long startTime = millis();
  unsigned long lastMotorMoveTime = millis(); // Track when the motor last stopped moving

  for (int x = 0; x <= PictureNumber; x++) {
    if (pendingCommand == 'A' && pendingValue == 1) {
      SendLog("Stack stopped by user.");
      pendingCommand = 'Z';
      return;
    }

    progress = x * 100 / PictureNumber;

    int CurrentTimeMs = millis() - startTime;
    int EstimatedTimeMs = 0;
    int photosRemaining = PictureNumber + 1 - x;
    if (x > 0) {
      unsigned long avgTimePerPhoto = CurrentTimeMs / x;
      EstimatedTimeMs = avgTimePerPhoto * photosRemaining;
    } else {
      EstimatedTimeMs = photosRemaining * (attente + 2000); // 2000ms added for typical camera processing
    }

    SendParameter(progress, CameraSteps, EstimatedTimeMs, CurrentTimeMs,
                  PictureNumber + 1);

    // Check connection during the stabilization wait (attente)
    // By calculating waitTarget based on lastMotorMoveTime, we overlap the 
    // stabilization time with the time the camera spent saving the previous photo to SD card!
    unsigned long waitTarget = lastMotorMoveTime + attente;
    
    // If the camera took longer to save the photo than the stabilization time, 
    // waitTarget will be in the past, and we won't wait at all (zero delay)!
    while (millis() < waitTarget) {
      if (pendingCommand == 'A' && pendingValue == 1) {
        SendLog("Stack stopped by user.");
        pendingCommand = 'Z';
        return;
      }
      
      if (WiFi.status() != WL_CONNECTED || WiFi.SSID() != connectedCameraSSID) {
        SendLog("Camera WiFi lost! Pausing stack...");
        unsigned long lostTime = millis();
        
        bool reconnected = false;
        while (!reconnected) {
          if (pendingCommand == 'A' && pendingValue == 1) {
            SendLog("Stack stopped by user.");
            pendingCommand = 'Z';
            return;
          }
          
          if (millis() - lostTime > 120000) { // 2 minutes timeout
            SendLog("Camera timeout. Aborting stack.");
            return;
          }
          
          if (ConnectCamera() == 1) {
            reconnected = true;
          } else {
            delay(2000);
          }
        }
        SendLog("Camera reconnected! Resuming...");
        // Reset stabilization timer since we probably manipulated the camera
        waitTarget = millis() + attente;
      }
      delay(10);
    }

    // Send request directly to avoid double-connection penalty
    bool requestSent = false;
    unsigned long waitStart = millis();
    while (!requestSent) {
      if (client.connect(host, httpPort)) {
        String url = "/sony/camera";
        client.print(String("POST " + url + " HTTP/1.1\r\n"));
        client.println("Content-Type: application/json");
        client.print("Content-Length: ");
        client.println(strlen(JSON_5));
        client.println();
        client.println(JSON_5);
        requestSent = true;
      } else {
        if (millis() - waitStart > 120000) { // 2 minutes timeout
          SendLog("Camera timeout. Aborting stack.");
          return;
        }
        SendLog("Camera not responding, waiting...");
        delay(2000);
      }
    }

    Serial.println("Take picture");
    
    // The camera triggers the shutter very quickly, but takes seconds to save to SD card.
    // Wait a fixed safe margin (800ms) to ensure the shutter has closed, 
    // then move the rail WHILE the camera is busy saving the file!
    delay(800); 

    if (x < PictureNumber) {
      Serial.println("Moving rail now...");
      SendLog("Moving rail...");
      unsigned long motorStart = millis();
      TurnMotor(ConvDistStep(CameraSteps));
      Serial.print("Motor moved in (ms): ");
      Serial.println(millis() - motorStart);
      lastMotorMoveTime = millis(); // Record the exact time the motor finished moving
    }

    // Wait for the camera to finish its processing and send the HTTP response
    unsigned long startT = millis();
    while (!client.available() && millis() - startT < 8000) {
      if (pendingCommand == 'A' && pendingValue == 1) {
        SendLog("Stack stopped by user.");
        pendingCommand = 'Z';
        client.stop();
        return;
      }
      delay(10);
    }

    // Clear the incoming buffer
    while (client.available()) {
      client.readStringUntil('\r');
    }
    client.stop();
  }
}

int Start() {
  Serial.println("GoToStart");
  GoTo(startPosition);
  Serial.println("GoCamera");
  GoToCamera(endPosition);
  return 1;
}

int Stop() {
  resetEDPins();
  DisconnectCamera();
  return 1;
}

int StartStop(int val) {
  if (val == 0) {
    int i = Start();
    resetEDPins();
    return i;
  } else {
    resetEDPins();
    return Stop();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Setup+Loop
//////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  pinMode(stp, OUTPUT);
  pinMode(dir, OUTPUT);
  pinMode(MS1, OUTPUT);
  pinMode(MS2, OUTPUT);
  pinMode(EN, OUTPUT);
  resetEDPins(); // Set step, direction, microstep and enable pins to default
                 // states
  ResolutionMoteur(StepperAngleDiv);
  SetMagnification(Magnification, lensAperture); // mag,aperture

  preferences.begin("rail_app", false);
  String savedPass = preferences.getString("sony_pass", "qXb1X35h");
  savedPass.toCharArray(cameraPassword, 64);

  Serial.begin(115200);
  Serial.println("Starting Macro Rail System");

  // Init LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("An Error has occurred while mounting LittleFS");
  }

  // Init WiFi AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP("ESP32_Rail_Setup");
  Serial.println("AP started: ESP32_Rail_Setup / 192.168.4.1");

  // Init WebServer
  server.serveStatic("/", LittleFS, "/index.html");

  // OTA Web Updater
  server.on("/update", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(
        200, "text/html",
        "<h2>ESP32 Firmware Update</h2><form method='POST' action='/update' "
        "enctype='multipart/form-data'><input type='file' "
        "name='update'><br><br><input type='submit' value='Update'></form>");
  });

  server.on(
      "/update", HTTP_POST,
      []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        delay(1000);
        ESP.restart();
      },
      []() {
        HTTPUpload &upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          Serial.printf("Update: %s\n", upload.filename.c_str());
          int cmd = (upload.filename.indexOf("littlefs") > -1 || upload.filename.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (Update.write(upload.buf, upload.currentSize) !=
              upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(true)) {
            Serial.printf("Update Success: %u\nRebooting...\n",
                          upload.totalSize);
          } else {
            Update.printError(Serial);
          }
        }
      });

  server.begin();
  Serial.println("Web Server Started");


  // Basic OTA (PlatformIO / Arduino IDE)
  ArduinoOTA.setHostname("ESP32_Rail");
  ArduinoOTA.setPort(3232);

  ArduinoOTA.onStart([]() {
    Serial.println("\n[OTA] Start");

    Serial.printf("[OTA] Sketch size: %u\n", ESP.getSketchSize());
    Serial.printf("[OTA] Free sketch space: %u\n", ESP.getFreeSketchSpace());

    // Stop anything that may disturb WiFi/flash during OTA
    client.stop();

    if (deviceConnected && pTxCharacteristic) {
      SendLog("OTA starting, BLE will stop");
      delay(100);
    }

    // Stop BLE before firmware upload
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(true);
    deviceConnected = false;
    oldDeviceConnected = false;

    delay(300);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\n[OTA] Error[%u]: ", error);

    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] Ready on port 3232");

  // Init NimBLE
  NimBLEDevice::init("ESP32_Rail");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // RX Characteristic
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  // TX Characteristic
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX,
                                                     NIMBLE_PROPERTY::NOTIFY);

  pService->start();

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
  Serial.println("BLE Advertising Started");
}

void processCommand(char cmd, int val, float floatVal) {
  if (cmd == 'P') {
    Serial.print("Saving new password: ");
    Serial.println(pendingPassword);
    pendingPassword.toCharArray(cameraPassword, 64);
    preferences.putString("sony_pass", pendingPassword);
    return;
  }

  if (cmd != 'Z') {
    Serial.print("commande ");
    Serial.print(cmd);
    Serial.print(" value ");
    Serial.println(val);
    if (cmd == 'Q') {
      Serial.print(" float value ");
      Serial.println(floatVal);
    }
  }
  switch (cmd) {
  case 'A':
    StartStop(val);
    break;
  case 'B':
    GoToStartEnd(val);
    break;
  case 'C':
    DefinePos(val);
    break;
  case 'D':
    Move(val);
    break;
  case 'E':
    MoveNeg(val);
    break;
  case 'F':
    ConnectCamera();
    break;
  case 'G':
    Magnification = floatVal;
    SetMagnification(Magnification, lensAperture);
    break;
  case 'Q':
    lensAperture = floatVal;
    SetMagnification(Magnification, lensAperture);
    break;
  case 'T':
    attente = val;
    Serial.print("Attente set to ");
    Serial.println(attente);
    break;
  case 'H':
    DisconnectCamera();
    break;
  case 'J':
    httpPost(JSON_5);
    break;
  case 'K':
    httpPost(JSON_18);
    break;
  }
}

void loop() {
  server.handleClient(); // Handle HTTP requests if active
  ArduinoOTA.handle();   // Handle OTA updates

  // Handle BLE connection state
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("BLE start advertising");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // Handle pending commands from BLE
  if (pendingCommand != 'Z') {
    processCommand(pendingCommand, pendingValue, pendingFloatValue);
    pendingCommand = 'Z';
  }
}