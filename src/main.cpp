// board_build.partitions = huge_app.csv
// monitor_speed = 115200
// --------------------------------------------------

// this header is needed for Bluetooth Serial -> works ONLY on ESP32
#include "BluetoothSerial.h" 
#include "Arduino.h"
#include <WiFi.h>

// init Class:
BluetoothSerial ESP_BT; 

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
int Magnification = 7.5;


// Sony function
volatile int counter;
const char* ssid     = "DIRECT-CeE0:ILCE-7RM2";
const char* ssid2     = "DIRECT-mgE0:ILCE-6300";
const char* password = "9E8EqQDV";     // your WPA2 password. Get it on Sony camera (connect with password procedure)
const char* password2 = "qXb1X35h";
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
char JSON_10[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setShutterSpeed\",\"params\":[\"1/160\"]}";
char JSON_11[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setIsoSpeedRate\",\"params\":[\"100\"]}";
//char JSON_12[]="{\"method\":\"getEvent\",\"params\":[true],\"id\":1,\"version\":\"1.0\"}";
char JSON_13[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"startLiveview\",\"params\":[]}";
char JSON_14[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"stopLiveview\",\"params\":[]}";
char JSON_15[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"getSupportedIsoSpeedRate\",\"params\":[]}";
char JSON_16[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"setCameraFunction\",\"params\":[\"Remote Shooting\"]}";
char JSON_17[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"getAvailableCameraFunction\",\"params\":[]}";
char JSON_18[] = "{\"version\":\"1.0\",\"id\":1,\"method\":\"getAvailableApiList\",\"params\":[]}";

WiFiClient client;

void SendParameter(int progress,int CameraSteps, int EstimatedTime, int CurrentTime)
{
  String stringOne, stringToSend;
  stringOne = String();
  stringToSend = String();
  stringOne = "#";
  stringToSend = progress + stringOne  + CameraSteps + stringOne + EstimatedTime + stringOne +CurrentTime;
  ESP_BT.println(stringToSend);
}

void SetMagnification(int magnification,float aperture)
{
  CameraSteps = (int) 2.2*aperture*aperture*(magnification+1)*(magnification+1)/(3*magnification*magnification);  //https://www.zerenesystems.com/cms/stacker/docs/tables/macromicrodof   reduced by 3 to get better result

  SendParameter(0,CameraSteps,0,0);
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
  //String url = "/sony/camera/";
  String url = "/sony/camera";
  //SerialBT.print("Requesting URL: ");
  //SerialBT.println(url);
 
  // This will send the request to the server
  
  // client.print(String("POST " + url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n")); ///A7R2
  client.print(String("POST " + url + " HTTP/1.1\r\n")); ///A6300
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
    Serial.println(line);
  }
  //SerialBT.println();
  //SerialBT.println("----closing connection----");
  //SerialBT.println();
  client.stop();
}

int ConnectCamera()
{
  Serial.println(WiFi.status());
  WiFi.disconnect();
  WiFi.begin(ssid2, password2);
  while (WiFi.status() != WL_CONNECTED) {   // wait for WiFi connection
    delay(500);
    //Serial.print(".");
    Serial.println(WiFi.status());
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  httpPost(JSON_1);  // initial connect to camera
  httpPost(JSON_2); // startRecMode

  //httpPost(JSON_13); //startLiveview  - in this mode change camera settings  (skip to speedup operation)
  //httpPost(JSON_10);
  //httpPost(JSON_11);
  return 1;
}

int DisconnectCamera()
{
  Serial.println(WiFi.status());
  WiFi.disconnect();
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
      //Serial.println("currentPosition ");Serial.println(currentPosition);
      }
    else
      {
      currentPosition = currentPosition+8/CurrentDriverResolution;
      //Serial.println("currentPosition ");Serial.println(currentPosition);
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
  //Serial.print("StartStop ");Serial.println(val);
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
  resetEDPins(); //Set step, direction, microstep and enable pins to default states
  ResolutionMoteur(StepperAngleDiv);
  SetMagnification(Magnification,lensAperture); //mag,aperture
  Serial.begin(115200);
  ESP_BT.begin("ESP32_Rail"); //Name of your Bluetooth interface -> will show up on your phone
}

void loop() {
  
  // -------------------- Receive Bluetooth signal ----------------------
  while (ESP_BT.available()) 
  {

    commande = (char) ESP_BT.read();
    delay(10);
    valuechar = ESP_BT.read();
    value = valuechar - '0'; //conv ASCII char to int
    if (commande !='Z') 
    {Serial.print("commande ");Serial.print(commande);Serial.print(" value ");Serial.println(value);}

    switch (commande) {
      case 'A':  
        //Serial.println("Start");
        StartStop(value);
        break;
      case 'B':  
        //Serial.println("GotoStartEnd");Serial.println(value);  
        GoToStartEnd(value);
        break;
      case 'C':  
        DefinePos(value);
        break;
      case 'D':
        Move(value);
        //Serial.println("Movex");
        break;
      case 'E':  
        MoveNeg(value);
        break;
      case 'F':  
        ConnectCamera();
        break;
      case 'G':  
        SetMagnification(value,lensAperture);
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
        
      case 'Z':
        break;
    }
    commande = 'Z';
  }
}