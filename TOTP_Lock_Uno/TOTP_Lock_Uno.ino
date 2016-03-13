/* Continuation of prototype version 1.4 - introducing Watchdog via Jeelib library, trying to get it working...

FIXED:
- got it working with the DS3231 Clock Module
- cleaned up setup() and loop() sections, moving a lot of stuff into functions.
- don't allow key presses while the 2second blink is active or the solenoid is open.
- reset keyboard buffer 4 seconds after last key pressed.

/  Todo:
- investigate what happens if currentMillis() rolls over (pretty sure nothing because it is unsigned)
- introduce watchdog timer to save battery
- introduce a way to change the shared key
- give a longer time for the code to work (increase the totp period)
- currently reading the RTC time twice during codeChecker - once inside printTheTime() and once in codeChecker().
  Two reads in quick succession like this may be contributing to clock drift... maybe fix so only required once?
- one-time use code (don't allow repeat-use?)
- read and check the previous three codes as well as the current one, OR...
- ...take into account clock-drift by auto-resetting the clock if a pattern is detected.
- ... only after I find a way to ensure One Time use of the codes though.
- QR codes can have longer periods - https://code.google.com/p/google-authenticator/wiki/KeyUriFormat
- QR code generator (use free text option) - https://www.the-qrcode-generator.com/


CIRCUIT (Arduino Uno):
keypad connected as below (currently for the blue keypads pictured, not the one integrated in the safe door I am hacking)
arduino pin 3 to mosfet gate (G)
arduino pin 4 to red LED +ve side
arduino pin 5 to yellow LED +ve side
arduino pin A4 to DS3231 module pin "D"
arduino pin A5 to DS3231 module pin "C"
DS3231 module pin "+" to arduino 5v
DS3231 module pin "-" to arduino gnd
red LED -ve to 330ohhm resister

(to continue)




*/

//========================================
//--LIBRARIES------------------
#include <Keypad.h>

#include <DS3232RTC.h>
#include <Time.h> 

#include <Wire.h> 

#include <sha1.h>
#include <TOTP.h>

//#include <JeeLib.h> // Low power functions library

//ISR(WDT_vect) { Sleepy::watchdogEvent(); } // Initialize the watchdog

// --CONSTANTS------------------
// ----KEYBOARD
const byte ROWS = 4; //four rows
const byte COLS = 3; //three columns
char keys[ROWS][COLS] = {
	{'1','2','3'},
	{'4','5','6'},
	{'7','8','9'},
	{'*','0','#'}
	};
byte rowPins[ROWS] = {9,8,7,6}; //connect to the row pinouts of the keypad
                                //(pin 9 to the outside of the 4 slots in the pad)
byte colPins[COLS] = {10,11,12}; //connect to the column pinouts of the keypad 
                                 //(pin 12 to the outside of the 3 slots in the pad)
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

// ----TIME
const bool LOWPOWER = true;         // set to true to enable low-power sleeping
long blinkInterval = 100;           // interval at which to blink (milliseconds)
long blinkLength = 1000;            // length of the blink signal (in milliseconds). must be longer than blinkInterval, should be a multiple of it.
long solenoidLength = 2000;         // length the solenoid remains open (in milliseconds)
long delayBeforeReset = 4000;       // length of time after last keypress before inputbuffer will reset.
uint8_t hmacKey[] = {0x4d, 0x79, 0x4c, 0x65, 0x67, 0x6f, 0x44, 0x6f, 0x6f, 0x72};  // shared secret is "MyLegoDoor" in HEX.
// NB the uint8_t constant will be a variable later

// ----OUTPUT PINS - for UNO
byte ledPinY = 5;                   //Yellow LED
byte ledPinR = 4;                   //Red LED
byte solenoidPin = 3;

// --VARIABLES------------------
boolean blink = false;              // whether or not ledPinR should blink at interval blinkInterval
boolean blin2 = false;              // whether or not ledPinR should blink for time blinkLength
boolean solenoidOpen = false;       // whether or not the solenoid should open (nb: the green LED is on the same circuit)
unsigned long currentMillis = 0;    // will store last time LED was updated
unsigned long previousMillis = 0;   // will store last time LED was updated
unsigned long blinkStart = 0;       // will store the time that the LED started blinking
unsigned long solenoidStart = 0;    // will store the time that the solenoid opened
unsigned long lastKeyPress = 0;     // will store last time a key was pressed
char key = 0;
char* totpCode;                     // calculated TOPTP code
char inputCode[7];                  // the code entered (zero indexed - so holds 8 data points in total)
unsigned int inputCode_idx;         // counter for length of the input code
TOTP totp = TOTP(hmacKey, 10);      // note that "MyLegoDoor" has 10 characters.


//========================================

void setup() {
  Serial.begin(9600);
  Serial.println("Starting Prototype_12-2-2015.ino");
  Wire.begin();

//this section first sets the system time (maintained by the Time library) to
//a hard-coded date and time, and then sets the RTC from the system time.
//the setTime() function is part of the Time library.
//setTime(04, 45, 00, 07, 2, 2016);   //set the system time to 04h45m00s on 07Feb2016
//RTC.set(now());                     //set the RTC from the system time

    setSyncProvider(RTC.get);   // the function to get the time from the RTC
    if(timeStatus() != timeSet) 
        Serial.println("Unable to sync with the RTC");
    else
        Serial.println("RTC has set the system time");      
  
  pinMode(ledPinY, OUTPUT);
  digitalWrite(ledPinY, LOW);        // sets initial value
  pinMode(ledPinR, OUTPUT);
  digitalWrite(ledPinR, LOW);
  pinMode(solenoidPin, OUTPUT);
  digitalWrite(solenoidPin, LOW);    // should already be closed, but just in case
  keypad.addEventListener(keypadEvent); //adds an event listener for this keypad
}

//========================================

void loop() {
//  if (LOWPOWER) {
//    Sleepy::loseSomeTime(16); // minimum watchdog granularity is 16 ms
//  }
//  else {
    key = keypad.getKey();
    currentMillis = millis();
    
    kpDelayHandler();
    blinkHandler();
    solenoidHandler();
//  }
}

//========================================

void printTheTime() {      // Prints the current time.
  //DateTime now = RTC.get(); //Read the current time and store it
  Serial.println();
  Serial.print(year(), DEC);
  Serial.print('/');
  Serial.print(month(), DEC);
  Serial.print('/');
  Serial.print(day(), DEC);
  Serial.print(' ');
  Serial.print(hour(), DEC);
  Serial.print(':');
  Serial.print(minute(), DEC);
  Serial.print(':');
  Serial.println(second(), DEC);
  Serial.print("UNIX time = ");
  //Serial.println(unixtime());
  Serial.println(now());
}

//========================================

void keypadEvent(KeypadEvent key){    // handler for the Keypad
  switch (keypad.getState()){
    case PRESSED:
      lastKeyPress = millis();
      switch (key){
        case '*':                 // reset code and blink for blinkLength ms.
          Serial.println();
          Serial.println("* pressed, resetting the input buffer...");
          inputCode_idx = 0;
          blinkStart = millis();
          blin2 = true;
        break;
        default: // all other keys than *
          if (!blin2 && !solenoidOpen) {
            digitalWrite(ledPinY,!digitalRead(ledPinY));
            Serial.print(key);
            inputCode[inputCode_idx++] = key;  //save key value in input buffer
            // if the buffer is full, add string terminator, reset the index, then call codeChecker
            if(inputCode_idx == 6) {
              inputCode[inputCode_idx] = '\0';
              inputCode_idx = 0;
              codeChecker();
            }
          }
          else {
            Serial.println();
            Serial.println("not accepting input, please wait");
            digitalWrite(ledPinR,!digitalRead(ledPinR));
          }
      }
    break;
    case RELEASED:
      switch (key){
        case '*': 
          //don't do anything to this key
        break;
        default:
          digitalWrite(ledPinY, LOW);
          digitalWrite(ledPinR, LOW);
      }
    break;
    case HOLD:
      // don't do anything special if a key is held down.
    break;
  }
}

//========================================

void blinkHandler() {
  if (blin2){
    blink = true;
    if(currentMillis - blinkStart > blinkLength) {
      digitalWrite(ledPinR, LOW);
      blink = false;
      blin2 = false;
    }
  }
  if (blink){
    if(currentMillis - previousMillis > blinkInterval) {
      previousMillis = currentMillis;   
      digitalWrite(ledPinR,!digitalRead(ledPinR));
    }
  }
}

//========================================

void solenoidHandler() {
  if (solenoidOpen){
    if(currentMillis - solenoidStart > solenoidLength) {   
      digitalWrite(solenoidPin,LOW);
      solenoidOpen = false;  // close the solenoid
      Serial.println("Solenoid closed");
      Serial.println();
    }
  }
}

//========================================

void codeChecker() {
  printTheTime(); 
  Serial.print("Code entered was: ");
  Serial.println(inputCode);

  //DateTime now = RTC.get();  // Read the current time and store it
  long GMT = RTC.get();
  totpCode = totp.getCode(GMT);
  Serial.print("TOTP code is: ");
  Serial.println(totpCode);
  
  if(strcmp(inputCode, totpCode) == 0) {      // totp code is correct
    Serial.println("Code was correct! Solenoid opened");
    digitalWrite(solenoidPin,HIGH);
    solenoidStart = millis();
    solenoidOpen = true;
  }
  else {                                      // totp code is incorrect
    Serial.println("Wrong code entered");
    Serial.println();
    blinkStart = millis();
    blin2 = true;
  }
}

void kpDelayHandler() {  // monitor last time a key was pressed. Reset if too long ago.
  if (inputCode_idx > 0 && currentMillis - lastKeyPress > delayBeforeReset) {
    Serial.println();
    Serial.println("too slow, resetting the input buffer...");
    inputCode_idx = 0;
    blinkStart = millis();
    blin2 = true;
  }
}
