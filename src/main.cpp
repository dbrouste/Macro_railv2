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
long currentPositionBaseSteps = 1000000L;
long startPositionBaseSteps = currentPositionBaseSteps - 1;
long endPositionBaseSteps = currentPositionBaseSteps + 1;
float StepperMinDegree = 1.8f; // pas mimimum du moteur en degree
int StepperAngleDiv = 8;       // 1 2 4 ou 8
int CurrentDriverResolution = StepperAngleDiv;
int thread_size = 700;   // in um. M3 = 600 M4 = 700 M5 = 800
long CameraStepsUm = 20; // in um, length between focal plane
int attente = 4000;      // Attente avant photo (en ms)
bool direction = 1;
unsigned long lastmillis;
float lensAperture = 3.5f;
float numericalAperture = 0.14f;
bool useMicroscopeObjective = false;
int progress = 0;
bool InvertSide = 1; // If motor is moving in the wrong direction
float Magnification = 10.0f;

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
      String rxString = String(rxValue.c_str());
      rxString.trim(); // removes \n, \r, trailing spaces

      Serial.print("Received BLE Value: ");
      Serial.println(rxString);

      int colonIndex = rxString.indexOf(':');
      if (colonIndex > 0) {
        char cmd = rxString.charAt(0);
        String payload = rxString.substring(colonIndex + 1);

        if (cmd == 'P') {
          pendingPassword = payload;
          pendingCommand = 'P';
        } else if (cmd == 'Q') {
          pendingFloatValue = payload.toFloat();
          pendingCommand = cmd;
        } else if (cmd == 'G') {
          pendingFloatValue = payload.toFloat();
          pendingCommand = cmd;
        } else {
          pendingValue = payload.toInt();
          pendingFloatValue = payload.toFloat();
          pendingCommand = cmd;
        }
      } else {
        Serial.println("Invalid BLE command format. Expected CMD:VALUE");
      }
    }
  }
};

long UmToBaseSteps(float um);
float BaseStepsToUm(long baseSteps);

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

void SaveUISettings() {
  preferences.putBool("ui_mic", useMicroscopeObjective);
  preferences.putFloat("lens_ap", lensAperture);
  preferences.putFloat("na", numericalAperture);
  preferences.putFloat("mag", Magnification);
  preferences.putInt("wait", attente);
}

void SetMagnification(float magnification, float aperture) {
  if (magnification <= 0.0f || aperture <= 0.0f) {
    CameraStepsUm = 1;
  } else {
    const float safetyFactor = 3.0f;
    if (useMicroscopeObjective) {
      CameraStepsUm = lroundf((0.55f / (aperture * aperture)) / safetyFactor);
    } else {
      CameraStepsUm = lroundf(2.2f * aperture * aperture *
                              (magnification + 1.0f) * (magnification + 1.0f) /
                              (safetyFactor * magnification * magnification));
    }
    if (CameraStepsUm < 1)
      CameraStepsUm = 1;
  }

  long diffSteps = labs(endPositionBaseSteps - startPositionBaseSteps);
  long stepPerPhoto = UmToBaseSteps(CameraStepsUm);
  if (stepPerPhoto < 1)
    stepPerPhoto = 1;
  int TotalPictures = diffSteps / stepPerPhoto + 1;

  SendParameter(0, (int)CameraStepsUm, 0, 0, TotalPictures);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Sony
//////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// stopSetupWifi removed to keep AP alive

bool httpPost(const char *jString) {
  if (!client.connect(host, httpPort)) {
    return false;
  }
  String url = "/sony/camera";

  client.print(String("POST " + url + " HTTP/1.1\r\n")); /// A6300
  client.println("Host: " + String(host));
  client.println("Connection: close");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(strlen(jString));
  client.println();
  client.println(jString);

  unsigned long startT = millis();
  while (!client.available() && millis() - startT < 8000) {
    delay(1); // Yield to Watchdog
  } // wait 8s max for answer

  bool ok = false;
  while (client.available()) {
    String line = client.readStringUntil('\r');
    if (line.indexOf("200 OK") >= 0)
      ok = true;
    Serial.println(line);
  }
  client.stop();
  return ok; // We return true since the command was successfully dispatched
}

String httpPostWithResponse(const char *jString) {
  String response = "";
  if (!client.connect(host, httpPort)) {
    return response;
  }
  String url = "/sony/camera";

  client.print(String("POST " + url + " HTTP/1.1\r\n")); /// A6300
  client.println("Host: " + String(host));
  client.println("Connection: close");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(strlen(jString));
  client.println();
  client.println(jString);

  unsigned long startT = millis();
  while (!client.available() && millis() - startT < 8000) {
    delay(1);
  }

  while (client.available()) {
    String line = client.readStringUntil('\r');
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
    if (targetSSID != "")
      break;

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

  // Wait a bit for the camera to switch to record mode before requesting
  // settings
  delay(1500);

  // Set ISO to minimum available (but at least 100)
  String isoResp = httpPostWithResponse(JSON_15);
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

long UmToBaseSteps(float um) {
  float stepsPerRev = (360.0f / StepperMinDegree) * StepperAngleDiv;
  return lroundf(um * stepsPerRev / (float)thread_size);
}

float BaseStepsToUm(long baseSteps) {
  float stepsPerRev = (360.0f / StepperMinDegree) * StepperAngleDiv;
  return (float)baseSteps * (float)thread_size / stepsPerRev;
}

bool TurnMotorSteps(long Step) {
  long x;
  digitalWrite(EN, LOW);
  Serial.print("TurnMotorSteps called with steps=");
  Serial.print(Step);
  Serial.print(" direction=");
  Serial.println(direction ? "backward" : "forward");

  // Ramp parameters for smooth acceleration/deceleration
  int maxDelay = 3000;  // Starting/ending speed in microseconds (slow, gentle)
  int minDelay = 700;   // Top speed in microseconds (fast)
  int delayChange = 20; // Change in delay per step (acceleration rate)

  long accelSteps = (maxDelay - minDelay) / delayChange;
  if (accelSteps > Step / 2) {
    accelSteps =
        Step / 2; // Cap acceleration steps if the movement is very short
  }

  for (x = 0; x < Step; x++) {
    if (pendingCommand == 'A' && pendingValue == 1) {
      SendLog("Motor movement stopped by user.");
      pendingCommand = 'Z';
      return false;
    }

    // Calculate current speed (delay) based on where we are in the movement
    int currentDelay;
    if (x < accelSteps) {
      // Acceleration phase
      currentDelay = maxDelay - (x * delayChange);
    } else if (x >= Step - accelSteps) {
      // Deceleration phase
      long stepsFromEnd = Step - 1 - x;
      currentDelay = maxDelay - (stepsFromEnd * delayChange);
    } else {
      // Constant max speed phase
      currentDelay = maxDelay - (accelSteps * delayChange);
    }

    digitalWrite(stp, HIGH); // Trigger one step forward
    delayMicroseconds(currentDelay);
    digitalWrite(stp, LOW); // Pull step pin low so it can be triggered again
    delayMicroseconds(currentDelay);

    // Maintain absolute position in base microsteps
    int stepMultiplier = StepperAngleDiv / CurrentDriverResolution;
    if (stepMultiplier < 1)
      stepMultiplier = 1;

    if (direction) {
      currentPositionBaseSteps = currentPositionBaseSteps - stepMultiplier;
    } else {
      currentPositionBaseSteps = currentPositionBaseSteps + stepMultiplier;
    }
  }
  return true;
}

int DefinePos(int val) {
  if (val == 0) {
    startPositionBaseSteps = currentPositionBaseSteps;
  } else {
    endPositionBaseSteps = currentPositionBaseSteps;
  }
  // Recalculate and send new Total Photos to UI
  SetMagnification(Magnification, lensAperture);
  return 1;
}

int AvanceUm(float distanceUm) {
  direction = 0;
  digitalWrite(dir,
               HIGH ^ InvertSide); // Pull direction pin low to move "forward"
  long baseSteps = UmToBaseSteps(distanceUm);

  int originalResolution = CurrentDriverResolution;
  ResolutionMoteur(StepperAngleDiv);
  bool ok = TurnMotorSteps(baseSteps);
  ResolutionMoteur(originalResolution);
  return ok ? 1 : 0;
}

int ReculeUm(float distanceUm) {
  direction = 1;
  digitalWrite(dir, LOW ^ InvertSide);
  long baseSteps = UmToBaseSteps(distanceUm);

  int originalResolution = CurrentDriverResolution;
  ResolutionMoteur(StepperAngleDiv);
  bool ok = TurnMotorSteps(baseSteps);
  ResolutionMoteur(originalResolution);
  return ok ? 1 : 0;
}

int Move(int val) {
  Serial.print("Move val ");
  Serial.println(val);
  switch (val) {
  case 1:
    Serial.println("Move -> forward 0.1 mm");
    AvanceUm(100.0f); // 0.1mm
    break;
  case 2:
    Serial.println("Move -> forward 1.0 mm");
    AvanceUm(1000.0f); // 1mm
    break;
  case 3:
    Serial.println("Move -> forward 10.0 mm");
    AvanceUm(10000.0f); // 10mm
    break;
  }
  return 1;
}

int MoveNeg(int val) {
  Serial.print("MoveNeg val ");
  Serial.println(val);
  switch (val) {
  case 1:
    Serial.println("MoveNeg -> backward 0.1 mm");
    ReculeUm(100.0f); // 0.1mm
    break;
  case 2:
    Serial.println("MoveNeg -> backward 1.0 mm");
    ReculeUm(1000.0f); // 1mm
    break;
  case 3:
    Serial.println("MoveNeg -> backward 10.0 mm");
    ReculeUm(10000.0f); // 10mm
    break;
  }
  return 1;
}

bool GoToBaseSteps(long targetBaseSteps) {
  int originalResolution = CurrentDriverResolution;
  ResolutionMoteur(StepperAngleDiv);

  long diff = targetBaseSteps - currentPositionBaseSteps;
  Serial.println("Diff steps ");
  Serial.println(diff);

  bool success = true;
  if (diff > 0) {
    direction = 0;
    digitalWrite(dir, HIGH ^ InvertSide);
    success = TurnMotorSteps(diff);
  } else if (diff < 0) {
    direction = 1;
    digitalWrite(dir, LOW ^ InvertSide);
    success = TurnMotorSteps(labs(diff));
  }

  ResolutionMoteur(originalResolution);
  return success;
}

void GoToStartEnd(int val) {
  if (val == 0) {
    GoToBaseSteps(startPositionBaseSteps);
  } else {
    GoToBaseSteps(endPositionBaseSteps);
  }
}

bool sendSonyPostNoWait(const char *jString) {
  if (!client.connect(host, httpPort)) {
    return false;
  }
  String url = "/sony/camera";
  client.print(String("POST " + url + " HTTP/1.1\r\n"));
  client.println("Host: " + String(host));
  client.println("Connection: close");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(strlen(jString));
  client.println();
  client.println(jString);
  return true;
}

const int shutterSafetyDelayMs =
    800; // Valide pour exposition courte. A augmenter pour pose longue.

bool GoToCameraSteps(long targetBaseSteps) {
  long diff = targetBaseSteps - currentPositionBaseSteps;

  long stepPerPhoto = UmToBaseSteps(CameraStepsUm);
  if (stepPerPhoto < 1)
    stepPerPhoto = 1;

  int PictureNumber = (int)(labs(diff) / stepPerPhoto);
  if (PictureNumber < 0)
    return false;

  Serial.print("DiffCamera steps: ");
  Serial.println(diff);
  Serial.print("PictureNumber: ");
  Serial.println(PictureNumber);

  if (diff < 0) {
    direction = 1;
    digitalWrite(dir, LOW ^ InvertSide); // Move backward
  } else {
    direction = 0;
    digitalWrite(dir, HIGH ^ InvertSide); // Move forward
  }

  int originalResolution = CurrentDriverResolution;
  ResolutionMoteur(StepperAngleDiv);

  bool success = true;
  unsigned long startTime = millis();
  unsigned long lastMotorMoveTime =
      millis(); // Track when the motor last stopped moving

  for (int x = 0; x <= PictureNumber; x++) {
    if (pendingCommand == 'A' && pendingValue == 1) {
      SendLog("Stack stopped by user.");
      pendingCommand = 'Z';
      success = false;
      goto cleanup;
    }

    if (PictureNumber > 0) {
      progress = x * 100 / PictureNumber;
    } else {
      progress = 100;
    }

    int CurrentTimeMs = millis() - startTime;
    int EstimatedTimeMs = 0;
    int photosRemaining = PictureNumber + 1 - x;
    if (x > 0) {
      unsigned long avgTimePerPhoto = CurrentTimeMs / x;
      EstimatedTimeMs = avgTimePerPhoto * photosRemaining;
    } else {
      EstimatedTimeMs =
          photosRemaining *
          (attente + 2000); // 2000ms added for typical camera processing
    }

    SendParameter(progress, (int)CameraStepsUm, EstimatedTimeMs, CurrentTimeMs,
                  PictureNumber + 1);

    // Check connection during the stabilization wait (attente)
    // By calculating waitTarget based on lastMotorMoveTime, we overlap the
    // stabilization time with the time the camera spent saving the previous
    // photo to SD card!
    unsigned long waitTarget = lastMotorMoveTime + attente;

    // If the camera took longer to save the photo than the stabilization time,
    // waitTarget will be in the past, and we won't wait at all (zero delay)!
    while (millis() < waitTarget) {
      if (pendingCommand == 'A' && pendingValue == 1) {
        SendLog("Stack stopped by user.");
        pendingCommand = 'Z';
        return false;
      }

      if (WiFi.status() != WL_CONNECTED || WiFi.SSID() != connectedCameraSSID) {
        SendLog("Camera WiFi lost! Pausing stack...");
        unsigned long lostTime = millis();

        bool reconnected = false;
        while (!reconnected) {
          if (pendingCommand == 'A' && pendingValue == 1) {
            SendLog("Stack stopped by user.");
            pendingCommand = 'Z';
            success = false;
            goto cleanup;
          }

          if (millis() - lostTime > 120000) { // 2 minutes timeout
            SendLog("Camera timeout. Aborting stack.");
            success = false;
            goto cleanup;
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
      if (sendSonyPostNoWait(JSON_5)) {
        requestSent = true;
      } else {
        if (millis() - waitStart > 120000) { // 2 minutes timeout
          SendLog("Camera timeout. Aborting stack.");
          success = false;
          goto cleanup;
        }
        SendLog("Camera not responding, waiting...");
        delay(2000);
      }
    }

    Serial.println("Take picture");

    // The camera triggers the shutter very quickly, but takes seconds to save
    // to SD card. Wait a fixed safe margin to ensure the shutter has closed,
    // then move the rail WHILE the camera is busy saving the file!
    delay(shutterSafetyDelayMs);

    if (x < PictureNumber) {
      Serial.println("Moving rail now...");
      SendLog("Moving rail...");
      unsigned long motorStart = millis();

      if (!TurnMotorSteps(stepPerPhoto)) {
        success = false;
        goto cleanup; // Aborted by user
      }
      Serial.print("Motor moved in (ms): ");
      Serial.println(millis() - motorStart);
      lastMotorMoveTime =
          millis(); // Record the exact time the motor finished moving
    }

    // Wait for the camera to finish its processing and send the HTTP response
    unsigned long startT = millis();
    while (!client.available() && millis() - startT < 8000) {
      if (pendingCommand == 'A' && pendingValue == 1) {
        SendLog("Stack stopped by user.");
        pendingCommand = 'Z';
        success = false;
        goto cleanup;
      }
      delay(10);
    }

    // Clear the incoming buffer
    while (client.available()) {
      client.readStringUntil('\r');
    }
  }

cleanup:
  client.stop();
  ResolutionMoteur(originalResolution);
  return success;
}

int Start() {
  Serial.println("GoToStart");
  if (!GoToBaseSteps(startPositionBaseSteps)) {
    return 0; // User stopped during initialization
  }
  Serial.println("GoCamera");
  if (!GoToCameraSteps(endPositionBaseSteps)) {
    return 0;
  }
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

  preferences.begin("rail_app", false);
  bool savedMicroscopeMode = preferences.getBool("ui_mic", false);
  useMicroscopeObjective = savedMicroscopeMode;
  lensAperture = preferences.getFloat("lens_ap", 3.5f);
  numericalAperture = preferences.getFloat("na", 0.14f);
  Magnification = preferences.getFloat("mag", 10.0f);
  attente = preferences.getInt("wait", 4000);

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

  server.on("/settings", HTTP_GET, []() {
    String json = "{";
    json += "\"mode\":\"" + String(useMicroscopeObjective ? "microscope" : "macro") + "\"";
    json += ",\"aperture\":" + String(lensAperture, 2);
    json += ",\"na\":" + String(numericalAperture, 3);
    json += ",\"magnification\":" + String(Magnification, 2);
    json += ",\"attente\":" + String(attente);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/settings", HTTP_POST, []() {
    if (server.hasArg("objectiveMode")) {
      String mode = server.arg("objectiveMode");
      useMicroscopeObjective = (mode == "microscope");
      preferences.putBool("ui_mic", useMicroscopeObjective);
    }
    if (server.hasArg("aperture")) {
      float ap = server.arg("aperture").toFloat();
      if (ap > 0) {
        lensAperture = ap;
        preferences.putFloat("lens_ap", lensAperture);
      }
    }
    if (server.hasArg("na")) {
      float na = server.arg("na").toFloat();
      if (na > 0) {
        numericalAperture = na;
        preferences.putFloat("na", numericalAperture);
      }
    }
    if (server.hasArg("magnification")) {
      Magnification = server.arg("magnification").toFloat();
      preferences.putFloat("mag", Magnification);
    }
    if (server.hasArg("attente")) {
      attente = server.arg("attente").toInt();
      preferences.putInt("wait", attente);
    }
    server.send(200, "text/plain", "OK");
  });

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
          int cmd = (upload.filename.indexOf("littlefs") > -1 ||
                     upload.filename.indexOf("spiffs") > -1)
                        ? U_SPIFFS
                        : U_FLASH;
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

  ArduinoOTA.onEnd([]() { Serial.println("\n[OTA] End"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\n[OTA] Error[%u]: ", error);

    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
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

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(
      0x06); // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Characteristic defined! Now you can read it in your phone!");

  // Now that BLE is initialized, we can safely call SetMagnification
  // which will try to send the initial parameters via BLE notification.
  SetMagnification(Magnification, lensAperture); // mag,aperture
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
    Serial.print("Command D received: move forward val=");
    Serial.println(val);
    Move(val);
    break;
  case 'E':
    Serial.print("Command E received: move backward val=");
    Serial.println(val);
    MoveNeg(val);
    break;
  case 'F':
    ConnectCamera();
    break;
  case 'G':
    Magnification = floatVal;
    SetMagnification(Magnification, useMicroscopeObjective ? numericalAperture : lensAperture);
    break;
  case 'Q':
    if (useMicroscopeObjective) {
      numericalAperture = floatVal;
    } else {
      lensAperture = floatVal;
    }
    SetMagnification(Magnification, floatVal);
    break;
  case 'O':
    useMicroscopeObjective = (val == 1);
    if (useMicroscopeObjective) {
      SendLog("Objective mode: Microscope");
    } else {
      SendLog("Objective mode: Macro");
    }
    SetMagnification(Magnification, useMicroscopeObjective ? numericalAperture : lensAperture);
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