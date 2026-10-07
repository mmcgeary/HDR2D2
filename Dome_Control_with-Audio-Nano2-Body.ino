#include <Arduino.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#define USE_LIGHTWEIGHT_SERVO_LIBRARY
#define DISABLE_COMPLEX_FUNCTIONS
#define ENABLE_EASE_CUBIC
#include <ServoEasing.hpp>

// Define the pins for SoftwareSerial communication with the DFPlayer Mini
static const uint8_t PIN_MP3_TX = A1; // Arduino pin connected to DFPlayer's RX using 1K resistor
static const uint8_t PIN_MP3_RX = A0; // Arduino pin connected to DFPlayer's TX
SoftwareSerial mySoftwareSerial(A0, A1); // Create SoftwareSerial object(Rx, Tx)
DFRobotDFPlayerMini myDFPlayer; // Create DFPlayer Mini object

ServoEasing myServo;
const int servoPin = 9;

const int Nano1 = 10; 
int Blue = 8;
int Red = 7;   
const int dfBusy = A2;
float Audio = A5;
int sensorMin = 0;  // minimum sensor value
int sensorMax = 1024; // maximum sensor value
float audioLevel; // the audio level
float Thresh;  //threshold
const int trigger = A3;// remote B input
const int bolt = 5; // mag bolt relay input
int boltIn;
int servo1CurrentAngle = 90;
int servo1TargetAngle = 90;
int trigstate;
int AngleDiff;
int minLimit1 = 1;
// We add 1 to the max limit because the upper bound is exclusive
int maxLimit1 = 3; 
int minLimit2 = 1;
// We add 1 to the max limit because the upper bound is exclusive
int maxLimit2 = 102; 
long randNumber1;
long randNumber2;
int Rbeep=0;

void setup() {
  delay(2000); // it's a Nano thing
  // Initialize the DFPlayer Mini
  myDFPlayer.begin(mySoftwareSerial);
  // Check if the module is responding and if the SD card is found
  mySoftwareSerial.begin(9600); // open the serial port at 9600 bps
    Serial.begin(9600);
 //  delay(1000);
   Serial.println();
  Serial.print("DFRobot DFPlayer Mini");
  Serial.print("Initializing DFPlayer module ... Wait!");
  if (!myDFPlayer.begin(mySoftwareSerial)) {
   Serial.print("Unable to begin DFPlayer Mini. Please check connections.");
   while(true){
    delay(0);
   }
   // Halt if initialization fails
  }
  Serial.println();
  Serial.print("DFPlayer Mini initialized.");
  
  myDFPlayer.EQ(5);            // Normal equalization 0
  //  myDFPlayer.EQ(DFPLAYER_EQ_POP); 1
  //  myDFPlayer.EQ(DFPLAYER_EQ_ROCK); 2
  //  myDFPlayer.EQ(DFPLAYER_EQ_JAZZ); 3
  //  myDFPlayer.EQ(DFPLAYER_EQ_CLASSIC); 4
  //  myDFPlayer.EQ(DFPLAYER_EQ_BASS); 5
  pinMode(Nano1, OUTPUT); // Nano1 low trigger to play Leia
  pinMode(trigger, INPUT_PULLUP); // Trigger - Low Starts sequence
  pinMode(bolt, INPUT_PULLUP); // bolt relay normally closed
  pinMode(dfBusy, INPUT_PULLUP); //  BUSY SIGNAL - Low=Busy
  pinMode(Blue, OUTPUT); // Blue led
  pinMode(Red, OUTPUT); // Red led
  pinMode(Audio, INPUT); // from mic board
  pinMode(LED_BUILTIN, OUTPUT);
    
  /* easing type options 
  - QUADRATIC
  - QUARTIC
  - EXPONENTIAL
  - ELASTIC
  - CIRCULAR
  - CUBIC
  */
  //myServo.setEasingType(EASE_CIRCULAR_OUT);

  digitalWrite(Nano1, HIGH); // led off
  analogWrite(Red, 0); // red on
  analogWrite(Blue, 255); // blue off
  myServo.setEasingType(EASE_CUBIC_IN_OUT); 
  myServo.attach(servoPin, 90); // D9
  setSpeedForAllServos(35);
  // delay(200);
   //  if (!myServo.isMoving()) {
   //  myServo.easeTo(servo1TargetAngle);
  myDFPlayer.volume(8); // Make startup sound quieter
  delay(20);
   // } 
  myDFPlayer.playFolder(1, 9); // startup sound - 2sec
  myDFPlayer.volume(16); // set normal level
  delay(4000); // make 4 secs in final copy to allow everything to pwr up
}

void loop() {
 myServo.update();
 myDFPlayer.volume(15);
 analogWrite(Red, 0); // red on
 analogWrite(Blue, 255); // blue off
 digitalWrite(LED_BUILTIN, LOW); // pin 13 led off
//delay(1000); 
//int servo1CurrentAngle;
 int trigstate = digitalRead(trigger); // Remote B controls this
 while (trigstate == 1) { // pulsate blue led while idle - this makes the 3 color cycle
  for (int brightness = 0; brightness <= 195; brightness++) {
   analogWrite(Red, brightness); // Set brightness (0-255)
   analogWrite(Blue, (195-brightness)); // Set brightness (0-255)
   delay(5);
  } // end for
  for (int brightness = 195; brightness >= 0; brightness--) {
   analogWrite(Red, brightness); // Set brightness (0-255)
   analogWrite(Blue, (195-brightness)); // Set brightness (0-255)
   delay(5);
  } // end for
 trigstate = digitalRead(trigger);
 } // end while trigstate==1

while (trigstate == 0) { // trigger input low
  // Seed the random number generator using an unconnected analog pin A4
  randomSeed(analogRead(A4)); // open noisy pin - helps to attach dangling wire
  randNumber1 = random(minLimit1, maxLimit1);
  // Seed the random number generator using an unconnected analog pin A4
  randomSeed(analogRead(A4));
  randNumber2 = random(minLimit2, maxLimit2);
  Serial.print("Track (");
  Serial.print(randNumber1);
  Serial.print(", ");
  Serial.print(randNumber2);
  Serial.print(")");
  Serial.println();
  audioLevel = analogRead(Audio);// for baseline Thresh
  Thresh = audioLevel;
  //Serial.print("Threshold: ");
  // Serial.print(Thresh, 4);
  // Serial.println();
  updateAndWaitForAllServosToStop();
 
  delay(200); // provides delay between tracks
  randomSeed(analogRead(A4)); // open noisy pin
  servo1TargetAngle = random(0, 180);
  AngleDiff = (abs(servo1TargetAngle - servo1CurrentAngle));
  if (AngleDiff > 10) {
//   myServo.setEasingType(EASE_PRECISION_OUT); 
    if (AngleDiff > 150) { // 150
      if (!myServo.isMoving()) {
   
      myServo.startEaseTo(servo1TargetAngle); 
    }
  myDFPlayer.playFolder(4, 1); // Scream!

  Serial.print("Target Angle = ");
  Serial.print(servo1TargetAngle);
  Serial.println();
  Serial.print("Moving from ");
  Serial.print(servo1CurrentAngle);
  Serial.print(" to ");
  Serial.print(servo1TargetAngle);
  Serial.println();
  servo1CurrentAngle=servo1TargetAngle;
  } // end if > 150
  else {
    if (!myServo.isMoving()) {
      myServo.startEaseTo(servo1TargetAngle);
    } 
  myDFPlayer.playFolder(randNumber1, randNumber2); // Play the track}
        
  Serial.print("Target Angle = ");
  Serial.print(servo1TargetAngle);
  Serial.println();
  Serial.print("Moving from ");
  Serial.print(servo1CurrentAngle);
  Serial.print(" to ");
  Serial.print(servo1TargetAngle);
  Serial.println();
  servo1CurrentAngle=servo1TargetAngle; 
  } // end else
 } // end if angle > 10
delay(200); // to allow track to start and dfbusy to go low
Rbeep = digitalRead(dfBusy);
 while (Rbeep == 0) { 
   audioLevel = analogRead(Audio);
   if (audioLevel > Thresh) { // while above threshold - turn on Blue LED else turn off Red
    analogWrite(Blue, 0); //(audioLevel * 1023.0 / 5.0));
    analogWrite(Red, 255); 
    digitalWrite(LED_BUILTIN, HIGH); // LITE PIN 13 ONBOARD LED
    delay(20);
   } // dropped below threshold
   else { 
    analogWrite(Blue, 255); // - (audioLevel * 1023.0 / 5.0)); // drops below threshold - Light up Red
    analogWrite(Red, 0); //(audioLevel * 1023.0 / 5.0)); 
    digitalWrite(LED_BUILTIN, LOW);
   // delay(10);
   }   
 Rbeep = digitalRead(dfBusy); // see if dfplayer still playing track
 } // end while- rbeep is 0 -go back and test audiolevel against threshold 
audioLevel = analogRead(Audio);// for baseline Thresh  
Thresh = audioLevel;
analogWrite(Blue, 255); // - (audioLevel * 1023.0 / 5.0)); // drops below threshold - Light up Red
analogWrite(Red, 0); //(audioLevel * 1023.0 / 5.0)); 
digitalWrite(LED_BUILTIN, LOW);
  
boltIn = digitalRead(bolt);  
 if (boltIn == 1) { // restraining bolt is removed
  servo1TargetAngle = 90;
  myServo.easeTo(servo1TargetAngle); // middle
  delay(1500);
  digitalWrite(Nano1, LOW); // send sig to other Nano to turn on led, run holo servo
  delay(1000); // allow time for holo to move down and led to come on
  myDFPlayer.volume(30); 
  delay(20);
  myDFPlayer.playFolder(3, 1); // play Leia msg
  myDFPlayer.volume(15); 
  delay(5000);
  digitalWrite(Nano1, HIGH); // release holo servos to start up again
  servo1CurrentAngle=servo1TargetAngle;
  while(boltIn == 1) {
    boltIn = digitalRead(bolt); }   
 } // end if bolt==1
trigstate = digitalRead(trigger); //go back to start of while trigstate
 if (trigstate == 1) {
   servo1TargetAngle = 90;
   delay(1500);     
   if (!myServo.isMoving()) {
     myServo.startEaseTo(servo1TargetAngle);
   }
 myDFPlayer.playFolder(4, 6); // off sound
 servo1CurrentAngle=servo1TargetAngle;
 delay(4000);
 } // end if tristate==1
 myServo.update();
 } // end while trigstate 0 
} // end main loop

