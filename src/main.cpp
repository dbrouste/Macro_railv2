// board_build.partitions = huge_app.csv
// monitor_speed = 115200
// board_build.filesystem = littlefs
// --------------------------------------------------

#include "Arduino.h"
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>

// BLE UUIDs
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_RX "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHARACTERISTIC_UUID_TX "1ccecade-7d72-46cb-8dc5-523e1e92ebdb"

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

Preferences preferences;

WebServer server(80);

// Command queue (from BLE callback to main loop)
volatile char pendingCommand = 'Z';
volatile int pendingValue = 0;
String pendingPassword = "";

// init PINs: assign any pin on ESP32
#define stp 17
#define dir 4
#define MS1 18
#define MS2 27
#define EN  25

// Parameters
char commande = '0';
char valuechar = '0';
int value = 0;
int currentPosition = 1000000;
int startPosition = currentPosition-1;
int endPosition = currentPosition+1;
float StepperMinDegree = 1.8; // pas mimimum du moteur en degree
int StepperAngleDiv = 8; //1 2 4 ou 8
int CurrentDriverResolution = StepperAngleDiv;
int thread_size = 700; //in um. M3 = 600 M4 = 700 M5 = 800
int CameraSteps = 20; // in um, lenght between focal plane
int attente = 4000; // Attente avant photo (en ms)
bool direction = 1;
unsigned long lastmillis;
float lensAperture = 3.5;
int progress = 0;
bool InvertSide = 1;  //If motor is moving in the wrong direction
int Magnification = 10;


// Sony function
volatile int counter;
const char* ssid     = "DIRECT-CeE0:ILCE-7RM2";
const char* ssid2     = "DIRECT-mgE0:ILCE-6300";
const char* password = "9E8EqQDV";     // your WPA2 password. Get it on Sony camera (connect with password procedure)
char cameraPassword[64] = "qXb1X35h";
const char* host = "192.168.122.1";   // fixed IP of camera
const int httpPort = 8080;
char JSON_1[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"getVersions\",\"params\":[]}";
char JSON_2[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"startRecMode\",\"params\":[]}";
char JSON_3[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"startBulbShooting\",\"params\":[]}";
char JSON_4[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"stopBulbShooting\",\"params\":[]}";
char JSON_5[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"actTakePicture\",\"params\":[]}";
char JSON_10[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setShutterSpeed\",\"params\":[\"1/160\"]}";
char JSON_11[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setIsoSpeedRate\",\"params\":[\"100\"]}";
char JSON_13[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"startLiveview\",\"params\":[]}";
char JSON_14[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"stopLiveview\",\"params\":[]}";
char JSON_15[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"getSupportedIsoSpeedRate\",\"params\":[]}";
char JSON_16[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setCameraFunction\",\"params\":[\"Remote Shooting\"]}";
char JSON_17[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"getAvailableCameraFunction\",\"params\":[]}";
char JSON_18[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"getAvailableApiList\",\"params\":[]}";

WiFiClient client;

// BLE Callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
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
        } else {
            pendingValue = payload.toInt();
            pendingCommand = cmd;
        }
      }
    }
};


void SendParameter(int progress,int CameraSteps, int EstimatedTime, int CurrentTime)
{
  String stringToSend = String(progress) + "#"  + String(CameraSteps) + "#" + String(EstimatedTime) + "#" + String(CurrentTime);
  // Send via BLE notification
  if (deviceConnected && pTxCharacteristic) {
      pTxCharacteristic->setValue((uint8_t*)stringToSend.c_str(), stringToSend.length());
      pTxCharacteristic->notify();
  }
}

void SendLog(String message) {
  String stringToSend = "L#" + message;
  if (deviceConnected && pTxCharacteristic) {
      pTxCharacteristic->setValue((uint8_t*)stringToSend.c_str(), stringToSend.length());
      pTxCharacteristic->notify();
  }
}

void SetMagnification(int magnification,float aperture)
{
  CameraSteps = (int) 2.2*aperture*aperture*(magnification+1)*(magnification+1)/(3*magnification*magnification);  //https://www.zerenesystems.com/cms/stacker/docs/tables/macromicrodof   reduced by 3 to get better result
  SendParameter(0,CameraSteps,0,0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Sony                                  ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stopSetupWifi() {
  Serial.println("Stopping AP and WebServer...");
  server.stop();
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
}

void httpPost(char* jString) {
  if (!client.connect(host, httpPort)) {
    return;
  }
  String url = "/sony/camera";
  
  client.print(String("POST " + url + " HTTP/1.1\r\n")); ///A6300
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(strlen(jString));
  client.println();
  client.println(jString);
  lastmillis = millis();
  while (!client.available() && millis() - lastmillis < 8000) {} // wait 8s max for answer
 
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.println(line);
  }
  client.stop();
}

int ConnectCamera()
{
  stopSetupWifi(); // Transition from Setup AP to STA mode
  
  SendLog("Scanning for Sony camera...");
  int n = WiFi.scanNetworks();
  String targetSSID = "";
  
  if (n == 0) {
    Serial.println("No networks found");
    SendLog("No networks found.");
    return 0;
  } else {
    for (int i = 0; i < n; ++i) {
      if (WiFi.SSID(i).indexOf("ILCE") >= 0) {
        targetSSID = WiFi.SSID(i);
        break;
      }
    }
  }
  
  if (targetSSID == "") {
    Serial.println("No Sony ILCE camera found.");
    SendLog("No Sony camera found.");
    return 0;
  }
  
  Serial.println(WiFi.status());
  SendLog("Connecting to " + targetSSID + "...");
  WiFi.begin(targetSSID.c_str(), cameraPassword);
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {   // wait 10s max
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
  
  httpPost(JSON_1);  // initial connect to camera
  httpPost(JSON_2); // startRecMode
  SendLog("Sony Camera Ready!");
  return 1;
}

int DisconnectCamera()
{
  Serial.println(WiFi.status());
  WiFi.disconnect();
  SendLog("Disconnected from camera.");
  return 1;
}

int StopLiveView()
{
  httpPost(JSON_14);
  return 1;
}

void TakePicture()
{
  httpPost(JSON_5);  //actTakePicture
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Motor                                 ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void resetEDPins()
{
  digitalWrite(stp, LOW);
  digitalWrite(dir, LOW);
  digitalWrite(MS1, LOW);
  digitalWrite(MS2, LOW);
  digitalWrite(EN, HIGH);
}

void ResolutionMoteur(int Resolution)
{
  CurrentDriverResolution = Resolution; //Set the stepper driver resolution/divider

  if (Resolution == 8) {
    digitalWrite(MS1, HIGH); //Pull MS1, and MS2 high to set logic to 1/8th microstep resolution
    digitalWrite(MS2, HIGH);
    }
  else if (Resolution == 4) {
    digitalWrite(MS1, LOW);
    digitalWrite(MS2, HIGH);
    }
  else if (Resolution == 2) {
    digitalWrite(MS1, HIGH);
    digitalWrite(MS2, LOW); 
    } 
  else if (Resolution == 1) {
    digitalWrite(MS1, LOW);
    digitalWrite(MS2, LOW);
    }
}

int ConvDistStep(int distance)//Convert dist (1um unity) to steps
{
  int PasCalc = 0;
  PasCalc = (int) distance*360*CurrentDriverResolution/(thread_size*StepperMinDegree);
  if (PasCalc<1) {PasCalc = 1;}
  Serial.println("ConvDistStep ");Serial.println(PasCalc);
  return PasCalc;
}

void TurnMotor(int Step)
{
  int x;
  digitalWrite(EN, LOW);
  
  for(x= 0; x<Step; x++)
  {
    digitalWrite(stp,HIGH); //Trigger one step forward
    delay(1);
    digitalWrite(stp,LOW); //Pull step pin low so it can be triggered again
    delay(1);
    if (direction)
      {currentPosition = currentPosition-8/CurrentDriverResolution;
      }
    else
      {
      currentPosition = currentPosition+8/CurrentDriverResolution;
      }
  }
}

int DefinePos(int val)
{
  if (val==0) 
  {
    startPosition = currentPosition;}
  else 
  {endPosition = currentPosition;}
  return 1; 
}

int Avance(int val) //val is a distance
{
  direction = 0;
  digitalWrite(dir, HIGH^InvertSide); //Pull direction pin low to move "forward"
  TurnMotor(val);
  return 1;
}

int Recule(int val)
{
  direction = 1;
  digitalWrite(dir, LOW^InvertSide); //Pull direction pin low to move "forward"
  TurnMotor(val);
  return 1;
}

int Move(int val)
{
  Serial.print("Move val ");Serial.println(val);
  ResolutionMoteur(1);
  switch (val) {
      case 1:  
        Avance(ConvDistStep(100));  //0.1mm
        break;
      case 2:  
        Avance(ConvDistStep(1000));  //1mm
        break;
      case 3:  
        Avance(ConvDistStep(10000));  //10mm
        break;
  }
  ResolutionMoteur(StepperAngleDiv);
  return 1;
}

int MoveNeg(int val)
{
  ResolutionMoteur(1);
    switch (val) {
      case 1:  
        Recule(ConvDistStep(100));  //0.1mm
        break;
      case 2:  
        Recule(ConvDistStep(1000));  //1mm
        break;
      case 3:  
        Recule(ConvDistStep(10000));  //10mm
        break;
  }
  ResolutionMoteur(StepperAngleDiv);
  return 1;
}

void GoTo(int val)
{
  int diff = val-currentPosition;
  Serial.println("Diff ");Serial.println(diff);
  if (diff>0)
  {
    Avance(diff);
  }
  else
  {
    Recule(abs(diff));
  }
}

void GoToStartEnd(int val)
{
  if (val==0) 
  {
    GoTo(startPosition);}
  else 
  {
    GoTo(endPosition);}
}

void GoToCamera(int val)
{
   int diff = val-currentPosition;  //number of steps to do
  if (diff<0) {return;}
  int PictureNumber = (int) diff/ConvDistStep(CameraSteps);  //calculate the picture number
  Serial.print("DiffCamera");Serial.println(diff);
  Serial.print("PictureNumber");Serial.println(PictureNumber);
  int EstimatedTime = PictureNumber*(attente+1);
  int CurrentTime = 0;

  direction = 0;  //
  digitalWrite(dir, HIGH^InvertSide); //Pull direction pin low to move "forward"

  for (int x=0;x<=PictureNumber;x++)
  {
    progress = x*100/PictureNumber;
    CurrentTime = x*(attente+1);
    EstimatedTime = EstimatedTime-CurrentTime;
    
    SendParameter(progress,CameraSteps,EstimatedTime,CurrentTime);

    delay(attente);
    Serial.println("Take picture");
    TakePicture();
    delay(100);
    TurnMotor(ConvDistStep(CameraSteps));
  }
}

int Start()
{
  Serial.println("GoToStart");
  GoTo(startPosition);
  Serial.println("GoCamera");
  GoToCamera(endPosition);
  return 1;
}

int Stop()
{
  resetEDPins();
  DisconnectCamera();
  return 1;
}

int StartStop(int val)
{
  if (val==0) 
  {
    int i = Start();
    resetEDPins();
    return i; }
  else 
  { resetEDPins();
    return Stop();}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Setup+Loop                            ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  pinMode(stp, OUTPUT);
  pinMode(dir, OUTPUT);
  pinMode(MS1, OUTPUT);
  pinMode(MS2, OUTPUT);
  pinMode(EN, OUTPUT);
  resetEDPins(); //Set step, direction, microstep and enable pins to default states
  ResolutionMoteur(StepperAngleDiv);
  SetMagnification(Magnification,lensAperture); //mag,aperture
  
  preferences.begin("rail_app", false);
  String savedPass = preferences.getString("sony_pass", "qXb1X35h");
  savedPass.toCharArray(cameraPassword, 64);

  Serial.begin(115200);
  Serial.println("Starting Macro Rail System");

  // Init LittleFS
  if(!LittleFS.begin(true)){
    Serial.println("An Error has occurred while mounting LittleFS");
  }

  // Init WiFi AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32_Rail_Setup");
  Serial.println("AP started: ESP32_Rail_Setup / 192.168.4.1");

  // Init WebServer
  server.serveStatic("/", LittleFS, "/index.html");
  server.begin();
  Serial.println("Web Server Started");

  // Init NimBLE
  NimBLEDevice::init("ESP32_Rail");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // RX Characteristic
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                       CHARACTERISTIC_UUID_RX,
                       NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
                     );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  // TX Characteristic
  pTxCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      NIMBLE_PROPERTY::NOTIFY
                    );

  pService->start();
  
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
  Serial.println("BLE Advertising Started");
}

void processCommand(char cmd, int val) {
    if (cmd == 'P') {
      Serial.print("Saving new password: "); Serial.println(pendingPassword);
      pendingPassword.toCharArray(cameraPassword, 64);
      preferences.putString("sony_pass", pendingPassword);
      return;
    }

    if (cmd != 'Z') {
      Serial.print("commande ");Serial.print(cmd);Serial.print(" value ");Serial.println(val);
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
        SetMagnification(val,lensAperture);
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
    processCommand(pendingCommand, pendingValue);
    pendingCommand = 'Z';
  }
}