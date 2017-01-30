
/*

CIRCUIT description: (note descriptions will double up as I describe each piece of hardware)
Keypad connected to Arduino pins as described below
  (currently for the blue keypads I purchased, not the one integrated in the safe door)
Arduino:
  - [LeoStick D9 | Uno 3] to mosfet gate (G) AND green LED +ve side
  - [LeoStick D0 | Uno 4] to red LED +ve side
  - [LeoStick D1 | Uno 5] to yellow LED +ve side
  - [LeoStick D2 | Uno A4] to DS3231 module pin "D" (D2 is the I2C SDA pin for LeoStick, or A4 for Uno)
  - [LeoStick D3 | Uno A5] to DS3231 module pin "C" (D3 is the I2C SCL pin for LeoStick, or A5 for Uno)
  - [LeoStick 13 | Uno 2] to watchdog "IN"
  - [LeoStick RST | Uno RST] to watchdog pin "OUT"
  - 5v+ to DS3231 module "+" and watchdog pin "VCC"
  - GND connects to:
    - DS3231 module pin "-"
    - 330ohm resistor for Green LED (-ve side)
    - 330ohm resistor for Yellow LED (-ve side)
    - 330ohm resistor for Red LED (-ve side)
    - Mosfet source (S)
    - 6V Battery Pack -ve
    - watchdog pin "GND"
DS3231 module
  - pin "+" to arduino 5v
  - pin "D" to [LeoStick D2 | Uno A4] (D2 is the I2C SDA pin for LeoStick, or A4 for Uno)
  - pin "C" to [LeoStick D3 | Uno A5] (D3 is the I2C SCL pin for LeoStick, or A5 for Uno)
  - pin "-" to arduino GND
Red LED
  - -ve to 330ohm resister then gnd
  - +ve to [LeoStick D0 | Uno 4]
Yellow LED
  - -ve to 330ohm resistor then gnd
  - +ve to [LeoStick D1 | Uno 5]
Green LED
  - -ve to 330ohm resistor then gnd
  - +ve to [LeoStick D9 | Uno 3] and Mosfet Gate (G)
Mosfet
  - Gate (G) to [LeoStick D9 | Uno 3] and Green LED +ve side
  - Drain (D) to diode (non-band side) and solenoid -ve
  - Source (S) to GND
Solenoid
  - +ve diode (band side) and to 6v battery pack +ve
  - -ve to diode (non-band side) AND mosfet drain (D)
WatchDog
  - GND to gnd
  - VCC to arduino 5V
  - IN to [LeoStick 13 | Uno 2]
  - OUT to [LeoStick RST | Uno Reset]

*/

//========================================
//--LIBRARIES------------------
#include <Keypad.h>

#include <DS3232RTC.h>
#include <Time.h>
#include <TimeLib.h>

#include <Wire.h> //for communicating with Serial. To be removed in production version.

#include <sha1.h>
#include <TOTP.h>
#include <string.h>

#include <EEPROM.h>  //for storing and retrieving the shared key

//#include <JeeLib.h> // Low power functions library

//ISR(WDT_vect) { Sleepy::watchdogEvent(); } // Initialize the watchdog

//--ARE YOU USING NANO OR UNO? UNCOMMENT ONE-----
//===============================================

#define LEOSTICK    //Some may apply to Nano as well...
//#define ARDUINO_UNO

//===============================================


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
#if defined(ARDUINO_UNO)
byte rowPins[ROWS] = {9,8,7,6}; //(pin 9 to the outside of the 4 slots in the pad)
byte colPins[COLS] = {10,11,12}; //(pin 12 to the outside of the 3 slots in the pad)
#elif defined (LEOSTICK)
byte rowPins[ROWS] = {4,5,6,7}; //(pin 4 to the outside of the 4 slots in the pad)
byte colPins[COLS] = {8,10,12}; //(pin 12 to the outside of the 3 slots in the pad, then 10 and 8)
#else
#error
#endif

Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

// ----TIME
const bool LOWPOWER = true;         // set to true to enable low-power sleeping
long blinkInterval = 100;           // interval at which to blink (milliseconds)
long blinkLength = 1000;            // length of the blink signal (in milliseconds). must be longer than blinkInterval, should be a multiple of it.
long solenoidLength = 2000;         // length the solenoid remains open (in milliseconds)
long delayBeforeReset = 4000;       // length of time after last keypress before inputbuffer will reset.
long changeModeTimeOut=100000;      
long watchDogTimeout = 120000;      // signal the watchdog timer every 2 minutes to prevent system reset.
char hmacKey[10];      //empty var to store value read from EEPROM
//char sharedSecret[11] = "S2KELQL4ZY";  //Why [11] and not [10]? - appears it needs the null terminator string...

// ----OUTPUT PINS
#if defined (ARDUINO_UNO)
byte ledPinY = 5;                   //Yellow LED
byte ledPinR = 4;                   //Red LED
byte solenoidPin = 3;
byte watchDogPin = 2;
#elif defined (LEOSTICK)
byte ledPinY = 1;                   //Yellow LED
byte ledPinR = 0;                   //Red LED
byte solenoidPin = 9;               //also the Green LED on the LeoStick
byte watchDogPin = 13;              //also the Red LED on the LeoStick
#else
#error
#endif

// --VARIABLES------------------
boolean blink = false;              // whether or not ledPinR should blink at interval blinkInterval
boolean keepBlinking = false;       // whether or not ledPinR should blink for time blinkLength
boolean solenoidOpen = false;       // whether or not the solenoid should open (nb: the green LED is on the same circuit)
unsigned long currentMillis = 0;    // will store last time LED was updated
unsigned long previousMillis = 0;   // will store last time LED was updated
unsigned long blinkStart = 0;       // will store the time that the LED started blinking
unsigned long solenoidStart = 0;    // will store the time that the solenoid opened
unsigned long lastKeyPress = 0;     // will store last time a key was pressed
char key = 0;
char totpCode[6];                   // calculated TOTP code
char preTotpCode[6];
char postTotpCode[6];
char inputCode[7];                  // the code entered (zero indexed - so holds 8 data points in total)
unsigned int eeAddress = 0;        // the current address in the EEPROM (i.e. which byte we're going to write to/read from next)
unsigned int inputCode_idx;         // counter for length of the input code
TOTP totp = TOTP((uint8_t*) hmacKey, 10);      // note that "S2KELQL4ZY" has 10 characters.
boolean change_mode = false;        // if user has tried to get into change mode
boolean code_correct=false;         // verified got into change mode
unsigned long change_mode_timer = 0; //time since change mode entered
unsigned long watchDogTimer = 0;   // time since watchdog module was last updated
boolean verified_change_mode=false;  //if user has successfully entered change mode.
char inputKey[10];                  //new key entered
unsigned int inputKey_idx;          // counter for length of new key.



//========================================

void setup() {
  Wire.begin();
  Serial.begin(9600);
  Serial.println("Starting TOTP_Lock.ino");

//setTime(10, 07, 00, 30, 1, 2017);   //set the system time (HH, MM, SS, DD, MM, YYYY)
//Serial.println("Arduino time has been set");
//RTC.set(now());                     //set the RTC from the system time
//Serial.println("Arduino has set the RTC time");

  setSyncProvider(RTC.get);   // the function to get the time from the RTC
  if(timeStatus() != timeSet)
      Serial.println("Unable to sync with the RTC");
  else {
    setTime(RTC.get());
    Serial.print("RTC has set the arduino time to -");
    printTheTime();
  }

//  EEPROM.put(eeAddress, sharedSecret);
//  Serial.print("EEPROM written with sharedSecret: ");
//  Serial.println(sharedSecret);

  EEPROM.get(eeAddress, hmacKey);  // pull the shared key value from EEPROM

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
    
    watchDogHandler();
    kpDelayHandler();
    blinkHandler();
    solenoidHandler();
    changeModeTimer();
    
    //TODO need to add handler to reset change_mode after timeout.

    //TODO need to worry about LEDs for change_mode.

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
  Serial.print(second(), DEC);
  Serial.print(" (UNIX time = ");
  Serial.print(now());
  Serial.println(')');
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
          keepBlinking = true;
        break;
        case '#':
          digitalWrite(ledPinY,!digitalRead(ledPinY));
          Serial.println();
          Serial.println("Change password mode, please enter the OTP to verify");
          change_mode=true;
          change_mode_timer=millis();
          break;
        default: // all other keys than * and #
          if (!keepBlinking && !solenoidOpen) {
              digitalWrite(ledPinY,!digitalRead(ledPinY));
              Serial.print(key);
  
              if (kpBuffer(key, 6)==1 && codeChecker() ==1) {
                if (change_mode) {   //have verified Change Mode!
                  Serial.println("Change Mode Verified");
                  verified_change_mode=true;
                  setNewKey();
                }
                else {  // normal operation mode - go ahead and open the lock
                  openSolenoid();
                }
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

char kpBuffer(char key, char len) {   //save key value in input buffer
  inputCode[inputCode_idx++] = key;
  if(inputCode_idx == len) {    // if the buffer is full, add string terminator, reset the index, and return 1
    inputCode[inputCode_idx] = '\0';
    inputCode_idx = 0;
    return 1;
  } else {
    return 0;
  }
}

//========================================

void kpDelayHandler() {  // monitor last time a key was pressed. Reset if too long ago.
  if (inputCode_idx > 0 && currentMillis - lastKeyPress > delayBeforeReset) {
    Serial.println();
    Serial.println("too slow, resetting the input buffer...");
    inputCode_idx = 0;
    blinkStart = millis();
    keepBlinking = true;
  }
}

//========================================

void blinkHandler() {
  if (blink){
    if(currentMillis - previousMillis > blinkInterval) {
      previousMillis = currentMillis;
      digitalWrite(ledPinR,!digitalRead(ledPinR));
    }
  }
  if (keepBlinking){
    blink = true;
    if(currentMillis - blinkStart > blinkLength) {
      digitalWrite(ledPinR, LOW);
      blink = false;
      keepBlinking = false;
    }
  }
}

//========================================

void solenoidHandler() {
  if (solenoidOpen){
    if(currentMillis - solenoidStart > solenoidLength) {
      digitalWrite(solenoidPin,LOW);
      solenoidOpen = false;
      Serial.println("Solenoid closed");
      Serial.println();
    }
  }
}

//=========================================

void openSolenoid() {
  Serial.println("Solenoid opened");
  digitalWrite(solenoidPin,HIGH);
  solenoidStart = millis();
  solenoidOpen = true;
}

//========================================

int codeChecker() {
  printTheTime();
  Serial.print("Code entered was: ");
  Serial.println(inputCode);

  long GMT = RTC.get();
  long pre = GMT-30;
  long post = GMT+30;
  
  Serial.print("Previous TOTP code is: ");
  Serial.println(totp.getCode(pre));
  Serial.print("Current TOTP code is: ");
  Serial.println(totp.getCode(GMT));
  Serial.print("Next TOTP code is: ");
  Serial.println(totp.getCode(post));

  if(strcmp(inputCode, totp.getCode(GMT)) == 0  ||
  strcmp(inputCode, totp.getCode(pre)) == 0 ||
  strcmp(inputCode, totp.getCode(post)) == 0 )  {
    Serial.println("Code was correct");
    return 1;
    }
  else {        // totp code is incorrect
    Serial.println("Wrong code entered");
    blinkStart = millis();
    keepBlinking = true;
    change_mode=false;
    verified_change_mode=false;
    return 0;
  }
}

//=========================================

void setNewKey() {
  //new key is in an array (null terminated), inputKey.

  Serial.println("We're in the \"setNewKey\" function now.");
  //totp = TOTP(hmacKey, 10);

  //can use web app to convert to 32-bit encoding for google authenticator. (provide link in instructions.)

  //that's it, everything should function as normal.
  //now reset lights and back to entering a code, checking based on new key.
}

//=========================================

void changeModeTimer() {
  if (change_mode){
    if(currentMillis - change_mode_timer > changeModeTimeOut) {
      change_mode=false;
      verified_change_mode=false;
      Serial.println("Change mode deactivated");
      Serial.println();
    }
  }
}

//=========================================

void watchDogHandler() { //signals the WatchDog module
  if(!inputCode_idx > 0 && currentMillis - watchDogTimer > watchDogTimeout) {
    digitalWrite(watchDogPin,HIGH);
    watchDogTimer = millis();
  }
  if(digitalRead(watchDogPin) == HIGH && currentMillis - watchDogTimer > 100) {
    digitalWrite(watchDogPin,LOW);
    Serial.println("WatchDog signal sent");
  }
}
