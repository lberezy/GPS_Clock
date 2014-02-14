#GPS Clock v0.1
This is a simple display clock that runs on the Arduino platform created out out
of frustration with my current bedside clock losing time constantly.

It is designed to display a simple clock interface to an LCD. UTC time is
synchronised via GPS NEMA data and stored in RTC module. Has support for
adjustable timezones and automatic daylight-savings periods. Settings can be
adjusted on the fly via a GUI menu interface. Also has support for displaying
current temperature read from the simple and cheap Dallas 1-Wire range of
temperature sensors.

##Hardware
	* The code currently compiles correctly for the Teensy 3.1 (@ 96Mhz)
		May work on standard Arduino boards, althought as GPS data is handled
		over a serial connection, baud rate of the GPS module will likely need
		to be reduced in order to fit in enough time for the main drawing and screen
		update loops within the main loop.

	* Standard NEMA compatible Serial GPS module (Tested succesfully at 10Hz @ 112500 baud)
	* I2C RTC module (optional, but very useful) - DS1307/DS3231 etc
	* 20x4 Character LCD - I use an I2C backpack compatable with New Liquid Crystal, but any should work
	* 1-Wire Temperature Sensor - DS18B20 provides 12bit resolution
	* Push button rotary encoder for menu interface

##Software
This project makes use of the following libraries:
	* [New Liquid Crystal](https://bitbucket.org/fmalpartida/new-liquidcrystal) - Drop in faster and more extensible replacement for standard Arduino Liquid Crystal
	* [DS1307RTC](https://www.pjrc.com/teensy/td_libs_DS1307RTC.html) - For RTC
	* [Arduino Timezone Library](https://github.com/JChristensen/Timezone)
	* [m2tklib](https://code.google.com/p/m2tklib/) - For LCD menu system
	* [Dallas Temperature Control Library](http://milesburton.com/Dallas_Temperature_Control_Library) - For DS18XXX series 1-Wire sensors
	* [OneWire](http://www.pjrc.com/teensy/td_libs_OneWire.html) - Requires [modification](http://forum.pjrc.com/threads/252-OneWire-library-for-Teensy-3-(DS18B20)) for use with Teensy 3.x boards
	* [Wire](http://www.pjrc.com/teensy/td_libs_Wire.html) - For I2C
