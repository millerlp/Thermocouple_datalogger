/* Thermocouple_datalogger_v2 Luke Miller July 2012
   Updated for Arduino 1.0.1 environment
  This program is released under the MIT License
  Copyright (c) 2010 Luke Miller
  The software is provided "as is", and it may not actually do anything
  useful. No warranty is implied or given. I don't really care what you
  do with the software, but please don't expect any support. 
  
  This program is meant to read 8 thermocouple temperatures and save them to a SD card.
  The program relies on a Arduino with AVR 328, a SDcard and real time clock shield from 
  Adafruit Industries, a 20x4 line LCD display, and an AD595 thermocouple amplifier 
  hooked up to a ADG407B differential multiplexer (aka "mux") to read the 8 thermocouple lines.
  The AD595 amplifier will convert voltages from type K and type T thermocouples into a 
  10mV/°C output. It is spec'd to work with type K thermocouples over the full range, but
  will read type T thermocouples accurately over the ~0 to ~50°C range that is relevant to
  most biological systems.

  The SD card and real time clock shield used here are sourced from Adafruit Industries
  http://www.adafruit.com/index.php?main_page=product_info&cPath=17_21&products_id=243
  The tutorial for assembling and setting up the SD card and real time clock are here:
  http://www.ladyada.net/make/logshield/
  You must have the real time clock initialized (by running the sketches outlined in the
  LadyAda tutorial) before attempting to load and run this sketch on the Arduino. 
*/
#include <SD.h> // SD card library 
#include <Wire.h> 
#include <SPI.h>
#include "RTClib.h"
#include <LiquidCrystal.h> //for the LCD display

volatile unsigned long lastSave = 0; // time value of last save. Relies on realtime clock
volatile unsigned long lcdTimeOut = 0; //timer for turning off LCD
volatile byte saveInterval = 10; //time between saves (units = seconds, not millis)
volatile byte lcdInterval= 60; //time to wait before shutting off LCD (units = seconds)
const byte reps = 9; //number of temp readings to average per channel (count from zero)

//initialize the LiquidCrystal library with the Arduino pin numbers for the LCD screen
//This uses a standard parallel-type LCD screen based on the HD44780 controller
//http://www.adafruit.com/index.php?main_page=product_info&cPath=37&products_id=198
//See the helpful LCD tutorial at:
//http://www.ladyada.net/learn/lcd/charlcd.html
//to become familiar with these LCD's. Note that the data pin connections between the LCD
//and Arduino board are changed here compared to the LadyAda tutorial. 
LiquidCrystal lcd(8,3,4,5,6,7); //RS,EN,DB4,DB5,DB6,DB7

//Note, when using "analog" pins as digital input/output, you need to refer to the pin 
//numbers by their "digital" line # equivalents. Analog pins 0-5 are referred to as 
//pins 14-19 respectively when used as digital I/O lines. 
#define muxA0 15 //analog 1, control line for ADG407 mux
#define muxA1 16 //analog 2, control line for ADG407 mux
#define muxA2 17 //analog 3, control line for ADG407 mux
#define tempPin 0 // analog 0, where temperature values are read in
#define lcdLight 9  // Use this line to actuate a PN2222A transistor. Hook the base 
                    // of the transistor to a 680ohm resistor and then to pin 9 of Arduino. 
                    // Hook the emitter of the transistor to ground. Hook the collector of
                    // the transistor to the grounds of the LCD. For total shut-off
                    // of the LCD, run the backlight ground (pin 16) and the contrast
                    // pot ground to the collector. When pin 9 is pulled high, the 
                    // ground of the LCD will be connected to ground and it will light up. 
                    // The program will continue running and saving data even when the LCD
                    // is shut off.
#define button1 2 //This is the digital input for the control button during the setup loop                    
byte button1INT = 0; //This is the interrupt pin for the control button during the main loop
                    //The same button (button 1) is repurposed from its function in the setup 
                    //loop to act as the interrupt now. 

#define aref_voltage 1.1 // using internal voltage reference for maximum resolution

RTC_DS1307 RTC; // define the Real Time Clock object
DateTime now; //define a time object "now", from the RTClib library

// The objects to talk to the SD card
Sd2Card card;
SdVolume volume;
SdFile root;
SdFile file;
File logfile; // The object that represents our datafile
const byte chipSelect = 10; // chip select pin for SD card, 10 for regular Arduino, 53 for Mega

/*Below we set up the data storage arrays for the temperature readings. This sketch uses a 
smoothing routine to avoid aliasing and to keep the temperature readings from fluctuating 
too quickly. A series of 10 analog readings will be taken for each channel (1-8) and stored 
in the array 'tempInArray'. For each channel, the 10 analog readings will be added 
up (n1+n2+n3...+n10) and stored in the array 'total'. Then the value in 'total' (one per channel) 
will be divided by the number of readings (10) to get an average value. This average value is 
converted to a temperature and displayed during the main loop */
const byte chs = 8; //Number of thermocouple channels we'll be reading
const byte numReadings = 10; //Number of readings we'll be storing for each channel
unsigned int tempInArray[numReadings][chs]; //create array to hold analog values, for 8 channels
byte index = 0; //index value to iterate through in main loop for keeping running average
unsigned int total[chs]; //array of totals, one value per channel

//*****************************************************************************************
// Error handling loop, from the tutorials at ladyada.net listed above
// If there is a problem accessing the SD card, this loop gets called to display a cryptic
// error message. 
void error(char *str)
{
  lcd.clear();
  lcd.home();
  lcd.print("error: ");
  lcd.print(str);
  while(1);
}
//****************************************************************************************

//****************************************************************************************
// Setup loop
// In this loop we will set up all the digital/analog inputs and outputs, initialize the 
// SD card, let the user set the data save interval and the lcd light timeout.
void setup(void)
{
  Wire.begin(); // You must start the Wire thing before attempting to read the real time clock
  analogReference(INTERNAL); //We use the internal voltage reference (bandgap reference) of the
                             //AVR uC for the analog-to-digital conversion of the temperature.
                             //This gives us good resolution (~0.1C) over the 0 to ~100C range
  pinMode(muxA2, OUTPUT); //multiplexer control pins
  pinMode(muxA1, OUTPUT); //multiplexer control pins
  pinMode(muxA0, OUTPUT); //multiplexer control pins
  pinMode(button1, INPUT); // button for selecting options during setup and turning on display
  digitalWrite(button1, HIGH); // set internal pullup resistor
  pinMode(lcdLight, OUTPUT); //pin to toggle power to LCD (this is connected to the PN2222A transistor)
  digitalWrite(lcdLight, HIGH); //turn on LCD intially
  lcd.begin(20,4);  //(columns,rows) Initialize LCD
  lcd.print("Hello"); //greet the user. 
  delay(350);
  //**************************************************************************
  //Show current date and time from real time clock
  now = RTC.now(); //get current time from real time clock, store value in 'now'
  lcd.setCursor(0,0);
  lcd.print("Time: ");
  lcd.setCursor(0,1);
  lcd.print(now.hour(),DEC); //display current hour value
  lcd.print(":");
  if (now.minute() < 10) {
    lcd.print("0"); //insert leading zero if minute value is less than 10
    lcd.print(now.minute(),DEC); //display current minute
  }
  else lcd.print(now.minute(),DEC); //display current minute
  lcd.print(":");
  if (now.second() < 10) {
    lcd.print("0"); //insert leading zero if seconds value is less than 10
    lcd.print(now.second(),DEC); //display current seconds
  }
  else lcd.print(now.second(),DEC); //display current seconds
  lcd.setCursor(0,2);
  lcd.print(now.month(),DEC); //display month value
  lcd.print("/");
  lcd.print(now.day(),DEC); //display numeric day value
  lcd.print("/");
  lcd.print(now.year(),DEC); //display year
  delay(3000);
  
  //************************************************************************** 
 // initialize the SD card
  if (!SD.begin(chipSelect)) {
    error("Card failed, or not present");
  }
//  if (!card.init()) error("card.init, Reformat card");
  lcd.setCursor(0,3);
  lcd.println("card initialized");
  delay(1500);
  lcd.clear();
  
  // create a new file
  char name[] = "LOGGER00.CSV"; //File names will follow this format, and increment automatically
  for (uint8_t i = 0; i < 100; i++) {
    name[6] = i/10 + '0';
    name[7] = i%10 + '0';
    if (! SD.exists(name)) {
     logfile = SD.open(name, FILE_WRITE);
     break; 
    }
  }
  if (! logfile) {
    error("couldn't create file");
  }
  
  lcd.clear();
  lcd.home();
  lcd.print("Logging to: ");
  lcd.setCursor(0,1);
  lcd.print(name);
  delay(3000);
  lcd.clear();

  if (!RTC.begin()) {
    logfile.println("RTC failed");
    lcd.clear();
    lcd.home();
    lcd.print("RTC failed");
    delay(2500);
  }    
  
  //Write the output file header row to our file so that we can identify the data later
  logfile.println("unixtime,datetime,Ch1,Ch2,Ch3,Ch4,Ch5,Ch6,Ch7,Ch8");
  logfile.flush();
  delay(350);

  //***************************************************************************
  // Have user select save-data interval and lcd timeout 
  // These two lines call sub-routines that are down at the bottom of this file
  saveInterval = saveInt(); //call saveInt sub-function and return value
  lcdInterval = lcdInt();   //call lcdInt sub-function and return value
  //*******************************************************************************
  
  // initialize analog temperature value array
  for (byte channel = 0; channel < chs; channel++) { 
    for (byte i = 0; i < numReadings; i++) {
      tempInArray[i][channel] = 0; //initialize all values to 0
    }
  }
  //A side-effect of initializing the storage array to all zeros is that the first 10 values
  //of temperatures displayed on screen will be inaccurate until we fill up the tempInArray 
  //and get rid of the zeros
  
  now = RTC.now(); //update clock        
  while (now.second() != 0 && now.second() != 10 && now.second() != 20 && now.second() != 30 && now.second() != 40 && now.second() != 50) 
  { //try to start recording data on an even time interval
    //this can introduce a delay of up to 10 seconds
    now = RTC.now(); //update clock   
  }
  //while loop should quit when the seconds value is some multiple of 10, including 0
  
  lastSave = now.unixtime() + 10 ; //initialize the lastSave value so that we can 
                                   //determine when to save to the SD card. Note that we're
                                   //adding 10 seconds to the lastSave value to avoid saving 
                                   //the inaccurate values reported during the initial startup
                                   //when tempInArray still contains zeros.
  lcdTimeOut = now.unixtime();  //initialize lcdTimeOut to determine when to shut off the lcd backlight
  
  //*********************
  //Establish an interrupt routine for button1, so that the user can turn the LCD backlight back on
  //after it automatically shuts off. Any time the user presses button1, the program will stop what
  //it's doing and go to the sub-routine 'timeReset' (listed near the bottom of this file) to 
  //turn the LCD back on and reset the lcdTimeOut value.
  attachInterrupt(button1INT, timeReset, CHANGE); //interrupt calls "timeReset" function
  lcd.clear();
  
} //end of setup loop
//********************************************************************************

//********************************************************************************
// Main Loop
void loop(void)
{

  //******************************************************************************
  //first subtract the old value in this row of tempInArray from the running total 
  for (byte channel = 0; channel < chs; channel++) {
    total[channel] = total[channel] - tempInArray[index][channel];
  }
  
  //run through mux channels, read a temperature value for each channel and 
  //insert it into the appropriate location of tempInArray
  //first set mux channel to read
    for (byte channel = 0; channel < chs; channel++) {
      //use bitwise AND along with bitshift operator to send digital 0's and 1's
      //to the appropriate digital output pins to control the ADG407 mux.
      //The ADG407's truth table is conveniently arranged so that the
      //progression below will send the correct combination of 0 and 1
      //to the three digital pins to set a channel 1-8.
      digitalWrite(muxA0, (channel & 1));
      digitalWrite(muxA1, (channel & 2)>>1);
      digitalWrite(muxA2, (channel & 4)>>2);   
      delay(2); //delay for 2 milliseconds to give the mux time to settle
               //The ADG407 is spec'd to switch in less than 160 ns, so the
               //wait time used here should be more than sufficient
      
      //Next we'll take a reading on the tempPin analog channel, and stick the 
      //value in tempinArray in the appropriate location
      analogRead(tempPin);
      delay(20);
      tempInArray[index][channel] = analogRead(tempPin);    
      //Note that because of the multiplexer, all temperature readings can be done on
      //the same analog input pin (tempPin) since all 8 channels are sequentially routed
      //to this pin.      
  } //end of for loop 'channel'
  //We now have the array 'tempinArray' full of raw integer values read off the
  //analog input pin for each of the 8 channels.
  
  //Next calculate a new total value for each channel by adding on the new analog
  //value that was just read
  for (byte channel = 0; channel < chs; channel++) {
    total[channel] = total[channel] + tempInArray[index][channel];
  }
    
  index = index++; //increment index value
  if (index >= numReadings) { //wrap around if we've exceeding the value of numReadings
    index = 0;
  }
  //Finished with temperature measuring
  //**********************************************************************************
  
  //**********************************************************************************  
  //Update LCD display with current temperature values
  delay(500);  //temporary delay to make the LCD readable. Should implement a sleep 
               // function here to save some battery power. 
  lcd.home(); //put cursor at start of LCD screen
  byte row = 0;
  for (byte i = 0; i <=6; i = i + 2) { //this loop runs 4 times, one for each row of LCD
    lcd.setCursor(0,row); //move cursor to next row for subsequent write commands
    lcd.print("Ch");
    lcd.print(i+1); //display channel number (i + 1)
    lcd.print(":");
    lcdPrintDouble((((total[i]/numReadings) * aref_voltage / 1024) * 100), 1);
    lcd.setCursor(10,row);
    lcd.print("Ch");
    lcd.print(i+2);
    lcd.print(":");  
    lcdPrintDouble((((total[i+1]/numReadings) * aref_voltage / 1024) * 100), 1);
    row++; //increment row counter
  }
  
  //*********************************************************************************  
  //Write data to SD card if it's time to save
  now = RTC.now(); //get current time from Real Time Clock
  
  if (now.unixtime() >= (lastSave + saveInterval)) //if new unix time is greater than
                                                   //lastSave + saveInterval
  {
    noInterrupts(); //shut off interrupts during SD card writing
    lastSave = now.unixtime(); //update lastSave value
    logfile.print(now.unixtime());
    logfile.print(",");  
    logfile.print(now.month(), DEC);
    logfile.print("/");
    logfile.print(now.day(), DEC);
    logfile.print("/");
    logfile.print(now.year(), DEC);
    logfile.print(" ");
    logfile.print(now.hour(), DEC);
    logfile.print(":");
    logfile.print(now.minute(), DEC);
    logfile.print(":");
    logfile.print(now.second(), DEC);  
    logfile.print(",");
    //now save temperatures
    for (int i=0; i<=6;i++) { //write the first 7 temperatures in a loop
    logfile.print(((total[i]/numReadings) * aref_voltage / 1024) * 100);
    logfile.print(","); 
    }
    logfile.println(((total[7]/numReadings) * aref_voltage / 1024) * 100);
    logfile.flush();
    
    interrupts(); //turn interrupts back on
    //print save notification to LCD
    lcd.setCursor(19,0);
    lcd.print("S");
    lcd.setCursor(19,1);
    lcd.print("A");
    lcd.setCursor(19,2);
    lcd.print("V");
    lcd.setCursor(19,3);
    lcd.print("E");
    delay(400);
    for (byte i = 0; i<=3; i++) { //clear save notification
      lcd.setCursor(19,i);
      lcd.print(" ");
    }    
  } //end of lastSave loop  
  //check to see if it's time to shut off the LCD display to conserve power
  if (now.unixtime() > lcdTimeOut + lcdInterval + 3) {
    digitalWrite(lcdLight, LOW); //turn off LCD
  }
}  //end of main program loop
//**********************************************************************************


// sub-routines start below here
//**********************************************************************************
//Interrupt routine
void timeReset() { //this is called by the interrupt routine attached to interrupt 0
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();
  //If interrupts come faster than 200ms, ignore since it's probably button bounce
  if (interrupt_time - last_interrupt_time > 200) {    
    digitalWrite(lcdLight, HIGH); //turn LCD on
    lcdTimeOut = now.unixtime(); //reset lcdTimeOut
  }
  last_interrupt_time = interrupt_time;
}

//*********************************************************************************
// saveInt subroutine
// This is relegated to a subroutine to keep the main setup loop readable. This function
// returns the save data interval 'saveInterval' to the setup loop. 
byte saveInt() {
  boolean buttonValue1;
  boolean buttonValue2;
  boolean buttonState;
  now = RTC.now(); //get startup time
  unsigned long startTime = now.unixtime();
  lcd.clear();
  lcd.print("Choose save interval");
  delay(500);
  lcd.setCursor(0,1);
  lcd.print(saveInterval);
  lcd.setCursor(3,1);
  lcd.print("seconds");
  buttonState = digitalRead(button1); //get current state of button (should be HIGH)
  while (now.unixtime() <= startTime + 3) //the +3 gives the user a 3 second window to 
                                          //press the button again
  {
    now = RTC.now(); //update clock
    buttonValue1 = digitalRead(button1);
    delay(10); //perform a crude debounce by checking button twice over 10ms
    buttonValue2 = digitalRead(button1);
    if (buttonValue1 == buttonValue2) {
      if (buttonValue1 != buttonState) { //make sure button state has changed
        if (buttonValue1 == LOW) { //if button is pressed, change the saveInterval value
                                   //The user is given several save interval options ranging
                                   //from 5 seconds to 10 minutes. Given that SD card space
                                   //is rarely limiting, you might as well save data very often,
                                   //but the longer intervals are there for people that don't 
                                   //like having lots of data.
          if (saveInterval >= 5 && saveInterval < 20) {
            saveInterval = saveInterval + 5; //set value to 5, 10, 15 or 20 seconds
          } else if(saveInterval >= 20 && saveInterval < 30) {
            saveInterval = saveInterval + 10; //set value to 30 seconds
          } else if(saveInterval >= 30 && saveInterval < 60) {
            saveInterval = saveInterval + 30; //set value to 60 seconds
          } else if(saveInterval >= 60 && saveInterval < 120) {
            saveInterval = saveInterval + 60; //set value to 120 seconds
          } else if(saveInterval >= 120 && saveInterval < 240) {
            saveInterval = saveInterval + 120; //set value to 4 minutes (240 seconds)
          } else if(saveInterval >= 240) {
            saveInterval = 5; //circle back to 5 seconds after reaching 240 second option 
          }
          //Show the user the current saveInterval value before the while loop starts over
          //again and checks for new button presses
          lcd.setCursor(0,1);
          lcd.print("   ");
          if (saveInterval <= 60) { 
            lcd.setCursor(0,1);
            lcd.print(saveInterval);
            lcd.setCursor(3,1);
            lcd.print("seconds");
          } else if(saveInterval > 60) {
            lcd.setCursor(0,1);
            lcd.print((saveInterval / 60));
            lcd.setCursor(3,1);
            lcd.print("minutes");
          }
          startTime = now.unixtime(); //update startTime to give user more time
                                      //to choose a value
        }
      }
      buttonState = buttonValue1; //update buttonState so that only changes
                                  //in button status are registered
    }
  }
  //while loop has expired, show user that their choice is being stored
  lcd.setCursor(0,3);
  lcd.print("Storing");
  delay(400);
  for (int i = 0; i <= 2; i++) {
    lcd.print(".");
    delay(350);
  }
  return saveInterval; //return the saveInterval value back to the calling function 
                       //in the setup loop
} //end of saveInt sub-routine

//***********************************************************************************
// lcdInt subroutine. This lets the user choose the amount of time before the LCD is 
// shut off. This uses input from button1 and returns a value "lcdInterval" to the
// setup loop.
byte lcdInt() {
  boolean buttonValue1; 
  boolean buttonValue2;
  boolean buttonState;
  now = RTC.now(); //get startup time
  unsigned long startTime = now.unixtime();
  lcd.clear();
  lcd.print("Choose LCD backlight");
  lcd.setCursor(0,1);
  lcd.print("timeout:");
  delay(500);
  lcd.setCursor(0,2);
  lcd.print(lcdInterval);
  lcd.setCursor(3,2);
  lcd.print("seconds");
  buttonState = digitalRead(button1); //get current state of button (should be HIGH by default)
  while (now.unixtime() <= startTime + 3) //the +3 gives the user a 3 second window to 
                                          //press the button again
  {
    now = RTC.now(); //update clock
    buttonValue1 = digitalRead(button1); //read value of button1 (LOW if pressed)
    delay(10); //perform a crude debounce by checking button twice over 10ms
    buttonValue2 = digitalRead(button1); //read value of button1 again
    if (buttonValue1 == buttonValue2) { //if both readings were the same
      if (buttonValue1 != buttonState) { //make sure button state has changed
        if (buttonValue1 == LOW) { //if button is pressed by user (= LOW)
          if(lcdInterval >= 10 && lcdInterval < 30) {
            lcdInterval = lcdInterval + 10; //set value to 10,20, or 30 seconds
          } else if(lcdInterval >= 30 && lcdInterval < 60) {
            lcdInterval = lcdInterval + 30; //set value to 60 seconds
          } else if(lcdInterval >= 60 && lcdInterval < 120) {
            lcdInterval = lcdInterval + 60; //set value to 2 minutes (120 seconds)
          } else if(lcdInterval >= 120 && lcdInterval < 240) {
            lcdInterval = lcdInterval + 120; //set value to 4 minutes (240 seconds)
          } else if(lcdInterval >= 240) {
            lcdInterval = 10; //circle back to 10 seconds if value exceeds 240 seconds
          }
          //Display current choice on LCD
          lcd.setCursor(0,2);
          lcd.print("   ");
          if (lcdInterval <= 60) {
            lcd.setCursor(0,2);
            lcd.print(lcdInterval);
            lcd.setCursor(3,2);
            lcd.print("seconds");
          } else if(lcdInterval > 60) {
            lcd.setCursor(0,2);
            lcd.print((lcdInterval / 60));
            lcd.setCursor(3,2);
            lcd.print("minutes");
          }
          startTime = now.unixtime(); //update startTime to give user more time
                                      //to choose a value
        }
      }
      buttonState = buttonValue1; //update buttonState so that only changes
                                  //in button status are registered
    }
  }
  //Show user what choice was selected and stored
  lcd.setCursor(0,3);
  lcd.print("Starting");
  delay(400);
  for (int i = 0; i <= 2; i++) {
    lcd.print(".");
    delay(350);
  }
  return lcdInterval; //return lcdInterval value to setup loop
} //end of lcdInt sub-routine


//***********************************************************************************
//subroutine to format numbers for display on LCD screen.
//Taken from the Arduino user forum, written by user 'mem'
//http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1207226548/13#13

void lcdPrintDouble( double val, byte precision){
  // prints val on a ver 0012 text lcd with number of decimal places determine by precision
  // precision is a number from 0 to 6 indicating the desired decimial places
  // example: printDouble( 3.1415, 2); // prints 3.14 (two decimal places)

  if(val < 0.0){
    lcd.print('-');
    val = -val;
  }
  if (int(val) < 100) {
    lcd.print(" "); //pad with a space for values less than 100
    lcd.print(int(val));
  } else {
    lcd.print (int(val));  //prints the int part
  }
  if( precision > 0) {
    lcd.print("."); // print the decimal point
    unsigned long frac;
    unsigned long mult = 1;
    byte padding = precision -1;
    while(precision--)
      mult *=10;

    if(val >= 0)
       frac = (val - int(val)) * mult;
    else
       frac = (int(val)- val ) * mult;
    unsigned long frac1 = frac;
    while( frac1 /= 10 )
     padding--;
    while(  padding--)
       lcd.print("0");
       lcd.print(frac,DEC) ;
  }
}
