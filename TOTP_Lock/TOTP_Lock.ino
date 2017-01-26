
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
  - 5v+ to DS3231 module "+" pin                              
  - GND connects to:
    - DS3231 module pin "-"                                   
    - 330ohm resistor for Green LED (-ve side)                
    - 330ohm resistor for Yellow LED (-ve side)               
    - 330ohm resistor for Red LED (-ve side)                  
    - Mosfet source (S)                                       
    - 6V Battery Pack -ve
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
//#include <JeeLib.h> // Low power functions library

//ISR(WDT_vect) { Sleepy::watchdogEvent(); } // Initialize the watchdog

//--ARE YOU USING NANO OR UNO? UNCOMMENT ONE-----
//===============================================
  //#define ARDUINO_NANO 
  #define ARDUINO_UNO
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
#elif defined (ARDUINO_NANO)
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
uint8_t hmacKey[] = {0x53, 0x32, 0x4b, 0x45, 0x4c, 0x51, 0x4c, 0x34, 0x5a, 0x59};  // shared secret is "S2KELQL4ZY" in HEX.
// NB the uint8_t constant will be a variable later (once I write a way to change it without re-flashing the arduino)

// ----OUTPUT PINS
#if defined (ARDUINO_UNO)
byte ledPinY = 5;                   //Yellow LED
byte ledPinR = 4;                   //Red LED
byte solenoidPin = 3;
#elif defined (ARDUINO_NANO)   //(or LeoStick)
byte ledPinY = 1;                   //Yellow LED
byte ledPinR = 0;                   //Red LED
byte solenoidPin = 9;               //also the Green LED on the LeoStick
#else
#error
#endif

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
char totpCode[6];                   // calculated TOPTP code
char preTotpCode[6];
char postTotpCode[6];
char inputCode[7];                  // the code entered (zero indexed - so holds 8 data points in total)
unsigned int inputCode_idx;         // counter for length of the input code
TOTP totp = TOTP(hmacKey, 10);      // note that "S2KELQL4ZY" has 10 characters.


//========================================

void setup() {
  Serial.begin(9600);
  Serial.println("Starting TOTP_Lock_Uno.ino");
  Wire.begin();


/*  These lines, if un-commented, first set the system time (maintained by the Time library) to
 *  a hard-coded date and time, and then sets the RTC from the system time. This only needs to be
 *  done if the RTC is new, or has become too far out of sync.
 *  I use it by un-commenting, setting the setTime to a few minutes in the future (remember must be in GMT),
 *  loading the sketch onto the arduino, then with the RTC connected, waiting and watching the clock until 
 *  you press the "reset" button right when the hard-coded time becomes real. Now the RTC has been set, you should
 *  disconnect it, comment out these lines again and re-load the sketch onto the arduino to prefent it re-writing 
 *  that time every time the arduino starts...
*/
//the setTime() function is part of the Time library.
//setTime(01, 52, 00, 26, 1, 2017);   //set the system time (HH, MM, SS, DD, MM, YYYY)
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
  long pre = GMT-30;
  long post = GMT+30;
  
  //memcpy(totpCode,totp.getCode(GMT),6);
  //memcpy(preTotpCode,totp.getCode(pre),6);
  //memcpy(postTotpCode,totp.getCode(post),6);
  
  Serial.print("Current TOTP code is: ");
  Serial.println(totp.getCode(GMT));
  
  Serial.print("Previous TOTP code is: ");
  Serial.println(totp.getCode(pre));
  Serial.print("Next TOTP code is: ");
  Serial.println(totp.getCode(post));
  
  if(strcmp(inputCode, totp.getCode(GMT)) == 0  ||
  strcmp(inputCode, totp.getCode(pre)) == 0 ||
  strcmp(inputCode, totp.getCode(post)) == 0 )
  {      // totp code is correct
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

  memset(totpCode,0,6);
  memset(preTotpCode,0,6);
  memset(postTotpCode,0,6); 
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
