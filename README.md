Thermocouple_datalogger

Updated 2014-10-02 to version 3. Versions 1 and 2 likely don't work
on modern versions of the Arduino platform. 

This is a design for an Arduino-based 8-channel thermocouple datalogger.
It makes use of an Analog Devices AD595 thermocouple amplifier that 
works with type K and type T thermocouples. The 8 thermocouple channels
are routed through a ADG407B differential multiplexer and fed into the 
AD595 amplifier. Data are saved to a SD card, and a real time clock 
provides time keeping abilities. 

You will need the SdFat library from https://github.com/greiman/SdFat
and the RTClib library from https://github.com/mizraith/RTClib 
to use version 3. The Wire, SPI, and LiquidCrystal libraries should
come with the stock Arduino software. 


The design and hardware are described here:
http://lukemiller.org/index.php/2010/08/a-thermocouple-datalogger-based-on-the-arduino-platform/
