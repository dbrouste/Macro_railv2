// board_build.partitions = huge_app.csv
//
// --------------------------------------------------

// this header is needed for Bluetooth Serial -> works ONLY on ESP32
#include "BluetoothSerial.h" 
#include "Arduino.h"
#include <WiFi.h>

// init Class:
BluetoothSerial ESP_BT; 

// init PINs: assign any pin on ESP32
#define stp 32
#define dir 33
#define MS1 26
#define MS2 27
#define EN  25

int progress = 0;

// Parameters
char commande = '0';
char value = '0';
int currentPosition = 1000000;
int startPosition = currentPosition;
int endPosition = currentPosition;
float StepperMinDegree = 1.8; // pas mimimum du moteur en degree
int StepperAngleDiv = 8; //1 2 4 ou 8
int thread_size = 700; //in um. M3 = 600 M4 = 700 M5 = 800
int CameraSteps = 20; // in um, lenght between focal plane
int attente = 1000; // Attente avant photo (en ms)
bool direction = 1;
unsigned long lastmillis;

// Sony function
volatile int counter;
const char* ssid     = "DIRECT-CeE0:ILCE-7RM2";
const char* password = "9E8EqQDV";     // your WPA2 password
const char* host = "192.168.122.1";   // fixed IP of camera
const int httpPort = 8080;
char JSON_1[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"getVersions\",\"params\":[]}";
char JSON_2[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"startRecMode\",\"params\":[]}";
char JSON_3[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"startBulbShooting\",\"params\":[]}";
char JSON_4[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"stopBulbShooting\",\"params\":[]}";
char JSON_5[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"actTakePicture\",\"params\":[]}";
//char JSON_6[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"actHalfPressShutter\",\"params\":[]}";
//char JSON_7[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"cancelHalfPressShutter\",\"params\":[]}";
//char JSON_8[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setSelfTimer\",\"params\":[2]}";
//char JSON_9[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setFNumber\",\"params\":[\"5.6\"]}";
//char JSON_10[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setShutterSpeed\",\"params\":[\"1/200\"]}";
//char JSON_11[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setIsoSpeedRate\",\"params\":[]}";
//char JSON_6[]="{\"method\":\"getEvent\",\"params\":[true],\"id\":1,\"version\":\"1.0\"}";
//char JSON_3[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"startLiveview\",\"params\":[]}";
//char JSON_4[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"stopLiveview\",\"params\":[]}";
WiFiClient client;


void SetMagnification(int val)
{
  CameraSteps = (int) 2.2*2.8*2.8/(val*val);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Sony                                  ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



void httpPost(char* jString) {
  //SerialBT.print("connecting to ");
  //SerialBT.println(host);
  if (!client.connect(host, httpPort)) {
    //SerialBT.println("connection failed");
    return;
  }
  else {
    //SerialBT.print("connected to ");
    //SerialBT.print(host);
    //SerialBT.print(":");
    //SerialBT.println(httpPort);
  }
  // We now create a URI for the request
  String url = "/sony/camera/";
 
  //SerialBT.print("Requesting URL: ");
  //SerialBT.println(url);
 
  // This will send the request to the server
  client.print(String("POST " + url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n"));
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(strlen(jString));
  // End of headers
  client.println();
  // Request body
  client.println(jString);
  //SerialBT.println("wait for data");
  lastmillis = millis();
  while (!client.available() && millis() - lastmillis < 8000) {} // wait 8s max for answer
 
  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    String line = client.readStringUntil('\r');
    //SerialBT.print(line);
  }
  //SerialBT.println();
  //SerialBT.println("----closing connection----");
  //SerialBT.println();
  client.stop();
}

int ConnectCamera()
{
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {   // wait for WiFi connection
    delay(500);
    //SerialBT.print(".");
  }
  //SerialBT.println("");
  //SerialBT.println("WiFi connected");
  //SerialBT.println("IP address: ");
  //SerialBT.println(WiFi.localIP());
  httpPost(JSON_1);  // initial connect to camera
  httpPost(JSON_2); // startRecMode
  //httpPost(JSON_3);  //startLiveview  - in this mode change camera settings  (skip to speedup operation)
  return 1;
}

void TakePicture()
{
  httpPost(JSON_5);  //actTakePicture
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Motor                                 ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//Reset Easy Driver pins to default states
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
  PasCalc = distance*360*StepperAngleDiv/(thread_size*StepperMinDegree);
  if (PasCalc<1) {PasCalc = 1;}
  return PasCalc;
}

void TurnMotor(int Step)
{
  int x;
  
  for(x= 0; x<Step; x++)  //Loop the forward stepping enough times for motion to be visible
  {
    digitalWrite(stp,HIGH); //Trigger one step forward
    delay(1);
    digitalWrite(stp,LOW); //Pull step pin low so it can be triggered again
    delay(1);
    if (direction)
      {currentPosition++;}
    else
      {currentPosition--;}
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
  direction = 1;
  digitalWrite(dir, LOW); //Pull direction pin low to move "forward"
  TurnMotor(val);
  return 1;
}

int Recule(int val)
{
  direction = 0;
  digitalWrite(dir, HIGH); //Pull direction pin low to move "forward"
  TurnMotor(val);
  return 1;
}

int Move(int val)
{
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
  return 1;
}

int MoveNeg(int val)
{
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
  return 1;
}

void GoTo(int val)
{
  int diff = val-currentPosition;
  if (diff>0)
  {
    Avance(diff);
  }
  else
  {
    Recule(diff);
  }
}

void GoToCamera(int val)
{
  int diff = val-currentPosition;
  direction = 1;
  digitalWrite(dir, LOW); //Pull direction pin low to move "forward"

  for (int x=0;x<=diff/CameraSteps;x++)
  {
    progress = x*100*CameraSteps/diff;
    ESP_BT.write(progress);
    delay(attente);
    TakePicture();
    TurnMotor(ConvDistStep(CameraSteps));
  }
}

int Start()
{
  GoTo(startPosition);
  GoToCamera(endPosition);
  return 1;
}

int Stop()
{
  return 1;
}

int StartStop(int val)
{
  if (val==0) 
  {return Start(); }
  else 
  {return Stop();}
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////                   Setup+Loop                            ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  pinMode(stp, OUTPUT);
  pinMode(dir, OUTPUT);
  pinMode(MS1, OUTPUT);
  pinMode(MS2, OUTPUT);
  pinMode(EN, OUTPUT);
  ResolutionMoteur(StepperAngleDiv);
  resetEDPins(); //Set step, direction, microstep and enable pins to default states
  Serial.begin(19200);
  ESP_BT.begin("ESP32_Rail"); //Name of your Bluetooth interface -> will show up on your phone
}

void loop() {
  
  // -------------------- Receive Bluetooth signal ----------------------
  while (ESP_BT.available()) 
  {
    // separate button ID from button value -> button ID is 10, 20, 30, etc, value is 1 or 0
    commande = (char) ESP_BT.read();
    value = (int) ESP_BT.read();
  }


    switch (commande) {
      case 'A':  
        StartStop(value);
        break;
      case 'B':  
        GoTo(value);
        break;
      case 'C':  
        DefinePos(value);
        break;
      case 'D':  
        Move(value);
        break;
      case 'E':  
        MoveNeg(value);
        break;
      case 'F':  
        ConnectCamera();
        break;
      case 'G':  
        SetMagnification(value);
        break;

    }
}
