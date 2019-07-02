/* /* 07/01/2019 Copyright Tlera Corporation
 *  
 *  Created by Kris Winer
 *  
 VEML6030  is  a  high  accuracy  ambient  light  digital  16-bit  resolution sensor in a miniature transparent 
 2 mm x 2 mm package.  It  includes  a  high  sensitive  photodiode,  a  low  noise  amplifier,  a  16-bit  A/D  
 converter  and  supports  an  easy to use I2C bus communication interface and additional interrupt feature.

 VEML6030’s   functions   are   easily   operated   via the simple command format 
 of I2C (SMBus compatible) interface  protocol.  VEML6030’s  operating  voltage  ranges  from   2.5   V   to   
 3.6   V.    
 
 Library may be used freely and without limit with attribution.
 
*/
#include <STM32L0.h>
#include "TimerMillis.h"
#include <RTC.h>
#include "VEML6030.h"
#include "I2Cdev.h"

#define I2C_BUS    Wire               // Define the I2C bus (Wire instance) you wish to use

I2Cdev             i2c_0(&I2C_BUS);   // Instantiate the I2Cdev object and point to the desired I2C bus

const char        *build_date = __DATE__;   // 11 characters MMM DD YYYY
const char        *build_time = __TIME__;   // 8 characters HH:MM:SS

// Cricket pin assignments
#define myLed     10 // blue led 
#define myVBat_en  2 // enable VBat read
#define myVBat    A1 // VBat analog read pin

// RTC set up
/* Change these values to set the current initial time */

uint8_t seconds = 0;
uint8_t minutes = 20;
uint8_t hours = 13;

/* Change these values to set the current initial date */

uint8_t day =   5;
uint8_t month = 5;
uint8_t year = 18;

uint8_t Seconds, Minutes, Hours, Day, Month, Year;

bool alarmFlag = true;


bool SerialDebug = true;

// battery voltage monitor definitions
float VDDA, VBAT, VBUS, STM32L0Temp;


// VEML6030 interrupt detects excursions below a low threshold abd above a high threshold
#define VEML6030Int A3

// Specify VEML6030 configuration parameters

//Choices are:
// IT: (IT_25  25 ms, IT_50  50 ms,) IT_100 = 100 ms, IT_200 = 200 ms, IT_400 = 400 ms, IT_800 = 800 ms
// Gain: Gain_1x 1x gain, Gain_2x 2x gain, Gain_0_125 1/8 x gain, Gain_0_25 1/4 x gain
// Persistance: 0x00 = 1, 0x01 = 2, 0x02 = 4, 0x03 = 8
// Power save Mode = 0x00, 0x01, 0x02, 0x03 higher the mode, longer time between data but lower the current usage
uint8_t IT = IT_100, Gain = Gain_1x, Persistance = 0x00, powerMode = 0x00;  // configuration variable

uint16_t ALSData = 0, WhiteData = 0, IntStatus = 0;
// lux/LSBit, ambient light sensitivity increases with integration time
float Sensitivity = 0.0288f/((float) (1 << IT) ); // for IT = 100 ms, 200 ms, 400 ms or 800 ms only
float ambientLight, whiteLight;
volatile bool VEML6030_flag = false;

VEML6030 VEML6030(&i2c_0);

void setup()
{
  Serial.begin(115200);
  delay(4000);
  Serial.println("Serial enabled!");
  
  pinMode(myLed, OUTPUT);
  digitalWrite(myLed, HIGH);  // start with blue led off, since active LOW

  pinMode(myVBat_en, OUTPUT);
  pinMode(myVBat, INPUT);
  analogReadResolution(12);

  pinMode(A3, INPUT);
  
  I2C_BUS.begin();                                      // Set master mode, default on SDA/SCL for STM32L4
  delay(1000);
  I2C_BUS.setClock(400000);                             // I2C frequency at 400 kHz
  delay(1000);

  // Set the RTC time
  SetDefaultRTC();

  VDDA = STM32L0.getVDDA();
  VBUS = STM32L0.getVBUS();
  digitalWrite(myVBat_en, HIGH);
  VBAT = 1.27f * VDDA * analogRead(myVBat) / 4096.0f;
  digitalWrite(myVBat_en, LOW);
  STM32L0Temp = STM32L0.getTemperature();
  
  // Internal STM32L0 functions
  Serial.print("VDDA = "); Serial.print(VDDA, 2); Serial.println(" V");
  Serial.print("VBAT = "); Serial.print(VBAT, 2); Serial.println(" V");
  if(VBUS ==  1)  Serial.println("USB Connected!"); 
  Serial.print("STM32L0 MCU Temperature = "); Serial.println(STM32L0Temp, 2);
  Serial.println(" ");
  
  VEML6030.init(IT, Gain, Persistance); // initialize the VEML6030 ALS
  VEML6030.enablepowerSave(powerMode);
  VEML6030.setHighThreshold(0x0400); // set high threshold to 1024/65,536
  VEML6030.setLowThreshold(0x0008);  // set  low threshold to    8/65,536
  uint16_t HiThrs = VEML6030.getHighThreshold();
  uint16_t LoThrs = VEML6030.getLowThreshold();
  Serial.print("High Threshold is : 0x"); Serial.println(HiThrs, HEX);
  Serial.print("Lo Threshold is : 0x"); Serial.println(LoThrs, HEX);
  
   // set alarm to update the RTC periodically
  RTC.setAlarmTime(12, 0, 0);
  RTC.enableAlarm(RTC.MATCH_ANY); // alarm once a second

  RTC.attachInterrupt(alarmMatch);

  attachInterrupt(VEML6030Int, myinthandler3, FALLING);
  
  /* end of setup */

}
/* 
 *  
 * Everything in the main loop is based on interrupts, so that 
 * if there has not been an interrupt event the STM32L082 should be in STOP mode
*/
 
void loop()
{

// VEML6030 data
    if(VEML6030_flag)
    {
      VEML6030_flag = false;
      IntStatus = VEML6030.getIntStatus();
      if(IntStatus & 0x8000) Serial.println("Low Threshold Crossed!");
      if(IntStatus & 0x4000) Serial.println("High Threshold Crossed!");
   }


  /*RTC*/
  if (alarmFlag) { // update serial output and log to SPI flash whenever there is an RTC alarm
      alarmFlag = false;

      VDDA = STM32L0.getVDDA();
      digitalWrite(myVBat_en, HIGH);
      VBAT = 1.27f * VDDA * analogRead(myVBat) / 4096.0f;
      digitalWrite(myVBat_en, LOW);
      
      if(SerialDebug) {
      Serial.print("VBAT = "); Serial.print(VBAT, 2); Serial.println(" V");
    }

    // VEML6030 Data
    ALSData = VEML6030.getALSData();
    Serial.print("Raw ALS Data is : 0x"); Serial.println(ALSData, HEX); 
    ambientLight = ((float)ALSData)*Sensitivity; // ALS in lux
    WhiteData = VEML6030.getWhiteData();
    whiteLight = ((float)WhiteData)*Sensitivity; // White light in lux
    Serial.print("VEML6030 ALS: "); Serial.print(ambientLight, 2); Serial.println(" lux"); Serial.println(" ");
    Serial.print("VEML6030 White: "); Serial.print(whiteLight, 2); Serial.println(" lux"); Serial.println(" ");
    
    // Read RTC
    Serial.println("RTC:");
    RTC.getDate(day, month, year);
    RTC.getTime(hours, minutes, seconds);

    Serial.print("RTC Time = ");
    if (hours < 10)   {Serial.print("0");Serial.print(hours); } else Serial.print(hours);
    Serial.print(":");
    if (minutes < 10) {Serial.print("0"); Serial.print(minutes); } else Serial.print(minutes);
    Serial.print(":");
    if (seconds < 10) {Serial.print("0"); Serial.print(seconds); } else Serial.print(seconds);
    Serial.println(" ");

    Serial.print("RTC Date = ");
    Serial.print(year); Serial.print(":"); Serial.print(month); Serial.print(":"); Serial.println(day);
    Serial.println();
      
    digitalWrite(myLed, LOW); delay(10); digitalWrite(myLed, HIGH);
        
    } // end of alarm section
    
    STM32L0.stop();        // Enter STOP mode and wait for an interrupt
   
}  /* end of loop*/


/* Useful functions */

void myinthandler3()
{
  VEML6030_flag = true;
  STM32L0.wakeup();
}


void alarmMatch()
{
  alarmFlag = true;
  STM32L0.wakeup();
}

void SetDefaultRTC()                                                                                 // Function sets the RTC to the FW build date-time...
{
  char Build_mo[3];
  String build_mo = "";

  Build_mo[0] = build_date[0];                                                                       // Convert month string to integer
  Build_mo[1] = build_date[1];
  Build_mo[2] = build_date[2];
  for(uint8_t i=0; i<3; i++)
  {
    build_mo += Build_mo[i];
  }
  if(build_mo == "Jan")
  {
    month = 1;
  } else if(build_mo == "Feb")
  {
    month = 2;
  } else if(build_mo == "Mar")
  {
    month = 3;
  } else if(build_mo == "Apr")
  {
    month = 4;
  } else if(build_mo == "May")
  {
    month = 5;
  } else if(build_mo == "Jun")
  {
    month = 6;
  } else if(build_mo == "Jul")
  {
    month = 7;
  } else if(build_mo == "Aug")
  {
    month = 8;
  } else if(build_mo == "Sep")
  {
    month = 9;
  } else if(build_mo == "Oct")
  {
    month = 10;
  } else if(build_mo == "Nov")
  {
    month = 11;
  } else if(build_mo == "Dec")
  {
    month = 12;
  } else
  {
    month = 1;                                                                                       // Default to January if something goes wrong...
  }
  if(build_date[4] != 32)                                                                            // If the first digit of the date string is not a space
  {
    day   = (build_date[4] - 48)*10 + build_date[5]  - 48;                                           // Convert ASCII strings to integers; ASCII "0" = 48
  } else
  {
    day   = build_date[5]  - 48;
  }
  year    = (build_date[9] - 48)*10 + build_date[10] - 48;
  hours   = (build_time[0] - 48)*10 + build_time[1]  - 48;
  minutes = (build_time[3] - 48)*10 + build_time[4]  - 48;
  seconds = (build_time[6] - 48)*10 + build_time[7]  - 48;
  RTC.setDay(day);                                                                                   // Set the date/time
  RTC.setMonth(month);
  RTC.setYear(year);
  RTC.setHours(hours);
  RTC.setMinutes(minutes);
  RTC.setSeconds(seconds);
}



