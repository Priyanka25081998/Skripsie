#include <TimeLib.h>
#include <TinyGPS.h>       // http://arduiniana.org/libraries/TinyGPS/
#include <SD.h>
#include <Wire.h>
#include "Adafruit_ADT7410.h"
Adafruit_ADT7410 tempsensor = Adafruit_ADT7410();
// TinyGPS and SoftwareSerial libraries are the work of Mikal Hart
TinyGPS gps; 
File dataFile;
#define SerialGPS Serial1
const int offset = 2;  
float milli = 0;
float c = 0;
int t = 0;
int n = 1;
const int chipSelect = BUILTIN_SDCARD;
#define CLKSRC 4 

void setup() {
  SerialGPS.begin(9600);
  SD.begin(chipSelect);
  tempsensor.begin();
  
  
  CCM_CSCMR1 &= ~CCM_CSCMR1_PERCLK_CLK_SEL; 
  CCM_CCGR1 |= CCM_CCGR1_GPT(CCM_CCGR_ON) ;  
  GPT1_CR = 0;
  GPT1_SR = 0x3F; // clear all prior status
  GPT1_CR = GPT_CR_EN | GPT_CR_CLKSRC(CLKSRC) | GPT_CR_FRR;
  attachInterrupt(12, pinisr, RISING);
}

uint32_t gpt_ticks() {
  return GPT1_CNT;
}

volatile uint32_t pps, ticks;
void pinisr() {
  pps = 1;
  ticks = gpt_ticks();
}

void loop() {
  dataFile = SD.open("gpt2.txt", FILE_WRITE);
  syncWithGPS();
  dataFile.close();
}

void syncWithGPS(){
  while (SerialGPS.available()) {
    if (gps.encode(SerialGPS.read())) { // process gps messages
      // when TinyGPS reports new data...
      unsigned long age;
      int year;
      byte month, day, hour, minute, second;
      static uint32_t prev = 0;
   
      if (pps) {
        // set the Time to the latest GPS reading
        gps.crack_datetime(&year, &month, &day, &hour, &minute, &second, NULL, &age);
        setTime(hour, minute, second, day, month, year);
        adjustTime(offset * SECS_PER_HOUR);
        c = tempsensor.readTempC();
        if (prev != 0) {
          t = ticks - prev;
          milli = ticks;
          GPSwriteSD();
          n ++;
        }
        pps = 0;
        c = 0;
        prev = ticks;
      }
    }
  }
}

void GPSwriteSD(){
   dataFile.print("GPS");
   dataFile.print(",");
   dataFile.print(hour());
   printDigitsSD(minute());
   printDigitsSD(second());
   dataFile.print(",");
   dataFile.print(day());
   dataFile.print(",");
   dataFile.print(month());
   dataFile.print(",");
   dataFile.print(year());
   dataFile.print(",");
   dataFile.print(milli);
   dataFile.print(",");
   dataFile.print(c);  
   dataFile.print(",");
   dataFile.print(t);
   dataFile.print(",");
   dataFile.println(n);
}

void printDigitsSD(int digits) {
  if(digits < 10)
    dataFile.print('0');
  dataFile.print(digits);
}
