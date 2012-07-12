Thermocouple_datalogger

This is a design for an Arduino-based 8-channel thermocouple datalogger.
It makes use of an Analog Devices AD595 thermocouple amplifier that 
works with type K and type T thermocouples. The 8 thermocouple channels
are routed through a ADG407B differential multiplexer and fed into the 
AD595 amplifier. Data are saved to a SD card, and a real time clock 
provides time keeping abilities. 

Design and hardware described here:
http://lukemiller.org/index.php/2010/08/a-thermocouple-datalogger-based-on-the-arduino-platform/
