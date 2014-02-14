/*
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


#define LCD_COLS 20
#define LCD_ROWS 4
#define TIME_SPACER ':'
#define DATE_SPACER '/'

#define LCD_SHOW_TZ 1
#define LCD_SHOW_TEMP 1

#define ROT_ENC_A_PIN 3
#define ROT_ENC_B_PIN 2
#define KEY_SELECT_PIN 4
#define ONEWIRE_PIN 23

#define TZ_EEPROM_ADDR 42

#define MENU_SCREEN 0
#define CLOCK_SCREEN 1
#define BIG_CLOCK_SCREEN 2
#define GPS_SCREEN 3
#define TEMPERATURE_SCREEN 4



#include <Wire.h>
#include <Time.h>
#include <DS1307RTC.h>
#include <TinyGPS.h>
#include <Timezone.h>
#include "M2tk.h"  // Menu library
#include "utility/m2ghnlc.h" // for New LiquidCrystal support in M2tk
#include <LiquidCrystal_I2C.h> // New LiquidCrystal library
#include <OneWire.h>
#include <DallasTemperature.h>


/*
    For different display types. Not implemented.
    LiquidCrystal		#include <LiquidCrystal.h>
    LiquidCrystal_I2C		#include <LiquidCrystal_I2C.h>
    LiquidCrystal_SR		#include <LiquidCrystal_SR.h>
    LiquidCrystal_SR2W		#include <LiquidCrystal_SR2W.h>
    LiquidCrystal_SR3W		#include <LiquidCrystal_SR3W.h>
*/



LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
HardwareSerial serial_gps = HardwareSerial(); // use hardware UART for gps serial
TinyGPS gps;
OneWire oneWire(ONEWIRE_PIN);  // 4.7K resistor to ground required
DallasTemperature sensors(&oneWire);



typedef struct{
    char* name;
    Timezone tz;
} timezone_item_t;

/* globals */

DeviceAddress tempDeviceAddress;

float temperature;

uint8_t require_update = 0;
uint8_t tz_hour = 0;
uint8_t tz_minute = 0;   // offset hours from gps time (UTC)
uint8_t tz_id = 0;

const int sync_interval = 2;   // time in seconds between GPS time sync


char linebuf[LCD_COLS];
byte degreeChar[8] = {
	0b11000,
	0b11000,
	0b00111,
	0b01000,
	0b01000,
	0b01000,
	0b01000,
	0b00111
};

uint8_t menu = 0;

uint8_t td_hour;
uint8_t td_min;
uint8_t td_sec;



/*================*/
//Time Zones

//Australia Eastern Time Zone (Sydney, Melbourne)
TimeChangeRule aEDT = {"AEDT", First, Sun, Oct, 2, 660};    //UTC + 11 hours
TimeChangeRule aEST = {"AEST", First, Sun, Apr, 3, 600};    //UTC + 10 hours
Timezone ausET(aEDT, aEST);

//Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
Timezone CE(CEST, CET);

//United Kingdom (London, Belfast)
TimeChangeRule BST = {"BST", Last, Sun, Mar, 1, 60};        //British Summer Time
TimeChangeRule GMT = {"GMT", Last, Sun, Oct, 2, 0};         //Standard Time
Timezone UK(BST, GMT);

//US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  //Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   //Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);

//US Central Time Zone (Chicago, Houston)
TimeChangeRule usCDT = {"CDT", Second, dowSunday, Mar, 2, -300};
TimeChangeRule usCST = {"CST", First, dowSunday, Nov, 2, -360};
Timezone usCT(usCDT, usCST);

//US Mountain Time Zone (Denver, Salt Lake City)
TimeChangeRule usMDT = {"MDT", Second, dowSunday, Mar, 2, -360};
TimeChangeRule usMST = {"MST", First, dowSunday, Nov, 2, -420};
Timezone usMT(usMDT, usMST);

//Arizona is US Mountain Time Zone but does not use DST
Timezone usAZ(usMST, usMST);

//US Pacific Time Zone (Las Vegas, Los Angeles)
TimeChangeRule usPDT = {"PDT", Second, dowSunday, Mar, 2, -420};
TimeChangeRule usPST = {"PST", First, dowSunday, Nov, 2, -480};
Timezone usPT(usPDT, usPST);

TimeChangeRule *tcr;

timezone_item_t tz_list[] = {
  {"Central Europe", CE},
  {"Australia EST", ausET},
  {"United Kingdom", UK},
  {"US East", usET},
  {"US Central", usCT},
  {"US Pacific", usPT},
  {"US Mountain", usMT},
  {"Arizona", usAZ}
};

uint8_t total_tzs = sizeof(tz_list)/sizeof(tz_list[0]);

uint8_t selected_tz = 0;
Timezone system_tz = tz_list[selected_tz].tz;

const char *fn_idx_to_tz_name(uint8_t idx) {
  return tz_list[idx].name;
}

/* GPS TIME SECTION */
//=================================================
time_t gpsTimeSync() {
  Serial.println("Sync...");
  //  returns time if avail from gps, else returns 0
  int gps_fail = 0;
  time_t gpstime = 0;
  unsigned long fix_age = 0 ;   
  unsigned long date, time;
  gps.get_datetime(&date, &time, &fix_age);
  // Ignore if fix age is invalid or stale
  if (fix_age == TinyGPS::GPS_INVALID_AGE || fix_age > 1000) {
    Serial.print("TimeSync failed due to "); Serial.print(fix_age == TinyGPS::GPS_INVALID_AGE ? "invalid" : "stale"); Serial.println(" fix age");
    gps_fail = 1;
  }
  // Ignore if no valid date or time yet received, such as when GPGGA is received before a GPRMC, because there's no date in a GPGGA
  if (date == TinyGPS::GPS_INVALID_DATE || time == TinyGPS::GPS_INVALID_TIME) {
    Serial.print("TimeSync failed due to invalid "); Serial.println(date == TinyGPS::GPS_INVALID_DATE ? "date" : "time");
    gps_fail = 1;
  }
  if (gps_fail) {
    return (RTC.get());
  } else {
    gpstime = gpsTimeToUnixTime(); // return time only if updated recently by gps 
    if (RTC.get()) RTC.set(gpstime);
    return gpstime;
    //return gpstime + (tz_hour * SECS_PER_HOUR) + (tz_minute * 60); // return corrected time
  }
}
 
time_t gpsTimeToUnixTime() {
  // returns time_t from gps date and time (approximates UTC)
  tmElements_t tm;
  int year;
  gps.crack_datetime(&year, &tm.Month, &tm.Day, &tm.Hour, &tm.Minute, &tm.Second, NULL, NULL);
  tm.Year = year - 1970; 
  time_t time = makeTime(tm);
  return time; 
}



/* LCD MENU SECTION */
//=================================================
// Forward declaration of the toplevel element
M2_EXTERN_ALIGN(top_el_expandable_menu);
M2_EXTERN_HLIST(el_top_td );
M2_EXTERN_HLIST(el_top_dt );
M2_EXTERN_HLIST(el_top_tz );

// Left entry: Menu name. Submenus must have a '.' at the beginning
// Right entry: Reference to the target dialog box (In this example all menus call the toplevel element again
m2_xmenu_entry m2_2lmenu_data[] = 
{
  { "Temerature", NULL },
    { ". Min/Max/Ave", &top_el_expandable_menu }, 
    {   ". Current Sensors", &top_el_expandable_menu },
    { ". Settings", &top_el_expandable_menu },
  { "Time/Date", NULL },
    { ". Time Adjust", &el_top_td },
    { ". Date Adjust", &el_top_dt },
    { ". Time Zone", &el_top_tz   },
  { "Exit", NULL, &exit_menu},
  { NULL, NULL },
};

// The first visible line and the total number of visible lines.
// Both values are written by M2_X2LMENU and read by M2_VSB
uint8_t el_x2l_first = 0;
uint8_t el_x2l_cnt = 4;

// M2_X2LMENU definition
// Option l4 = four visible lines
// Option e1 = first column has a width of 1 char
// Option w16 = second column has a width of 16 chars

M2_X2LMENU(el_x2l_main, "l4e1w15", &el_x2l_first, &el_x2l_cnt, m2_2lmenu_data, '+','-','\0');
M2_SPACE(el_space, "w1h1");
M2_VSB(el_vsb, "l4w1", &el_x2l_first, &el_x2l_cnt);
M2_LIST(list_2lmenu) = { &el_x2l_main, &el_space, &el_vsb };
M2_HLIST(el_hlist, NULL, list_2lmenu);
M2_ALIGN(top_el_expandable_menu, NULL , &el_hlist);

//=================================================
// m2 object and constructor
M2tk m2(&top_el_expandable_menu, m2_es_auto_repeat, m2_eh_4bs, m2_gh_nlc);

const char *exit_menu(uint8_t idx, uint8_t msg)
{
  if ( msg == M2_STRLIST_MSG_SELECT ) {
    menu = 0;
  }
  return "";
}
//=================================================





/*===================== */
// edit TimeZone (tz) dialog

void fn_tz_ok (m2_el_fnarg_p fnarg) {
  system_tz = tz_list[selected_tz].tz;
  system_tz.writeRules(TZ_EEPROM_ADDR);
  Serial.println(selected_tz);
  m2.setRoot(&top_el_expandable_menu);
}

M2_LABEL(el_tz_label, NULL, "Select Time Zone");
M2_COMBO(el_tz_combo, "v1", &selected_tz, total_tzs, fn_idx_to_tz_name);
//M2_U8NUM(el_tz_hour, "c2", -24,24, &tz_hour);
//M2_LABEL(el_tz_sep, NULL, ":");
//M2_U8NUM(el_tz_minute, "c2", 0,59, &tz_minute);

//M2_LIST(list_timezone) = { &el_tz_label, &el_tz_hour, &el_tz_sep, &el_tz_minute};
M2_LIST(list_timezone) = { &el_tz_label, &el_tz_combo}; 
M2_VLIST(el_timezone, NULL, list_timezone);

M2_BUTTON(el_tz_ok, NULL, "OK", fn_tz_ok);
M2_ROOT(el_tz_cancel, NULL, "Cancel", &top_el_expandable_menu);
M2_LIST(list_tz_buttons) = {&el_tz_cancel, &el_tz_ok};
M2_HLIST(el_tz_buttons, NULL, list_tz_buttons);

M2_LIST(list_tz) = {&el_timezone, &el_tz_buttons };
M2_VLIST(el_top_tz, NULL, list_tz);


/*=========================================================================*/
/* edit time dialog */


/*
void td_get_from_RTC(void)
{
  RTC.getTime();
  td_hour = RTC.hour;
  td_min = RTC.minute;
  td_sec = RTC.second;


}

void td_put_to_RTC(void)
{
  RTC.getTime();
  RTC.fillByHMS(td_hour, td_min, td_sec);
  RTC.setTime();
  RTC.startClock();  
} */

void td_ok_fn(m2_el_fnarg_p fnarg) 
{
  //td_put_to_RTC();
  m2.setRoot(&top_el_expandable_menu);
}

M2_U8NUM(el_td_hour, "c2", 0,23,&td_hour);
M2_LABEL(el_td_sep1, NULL, ":");
M2_U8NUM(el_td_min, "c2", 0,59,&td_min);
M2_LABEL(el_td_sep2, NULL, ":");
M2_U8NUM(el_td_sec, "c2", 0,59,&td_sec);

M2_LIST(list_time) = { &el_td_hour, &el_td_sep1, &el_td_min, &el_td_sep2, &el_td_sec };
M2_HLIST(el_time, NULL, list_time);

M2_ROOT(el_td_cancel, NULL, "cancel", &top_el_expandable_menu);
M2_BUTTON(el_td_ok, NULL, "ok", td_ok_fn);
M2_LIST(list_td_buttons) = {&el_td_cancel, &el_td_ok };
M2_HLIST(el_td_buttons, NULL, list_td_buttons);

M2_LIST(list_td) = {&el_time, &el_td_buttons };
M2_VLIST(el_top_td, NULL, list_td);

/*=========================================================================*/
/* edit date dialog */

uint8_t dt_day;
uint8_t dt_month;
uint8_t dt_year;
/*
void dt_get_from_RTC()
{
  RTC.getTime();
  dt_day = RTC.day;
  dt_month = RTC.month;
  dt_year = (RTC.year-2000);
}

void dt_put_to_RTC(void)
{
  RTC.getTime();
  RTC.fillByYMD(dt_year+2000, dt_month, dt_day);
  RTC.setTime();
  RTC.startClock();  
}*/

void dt_ok_fn(m2_el_fnarg_p fnarg) 
{
  //dt_put_to_RTC();
  m2.setRoot(&top_el_expandable_menu);
}

M2_U8NUM(el_dt_day, "c2", 1,31,&dt_day);
M2_LABEL(el_dt_sep1, NULL, ".");
M2_U8NUM(el_dt_month, "c2", 1,12,&dt_month);
M2_LABEL(el_dt_sep2, NULL, ".");
M2_U8NUM(el_dt_year, "c2", 0,99,&dt_year);

M2_LIST(list_date) = { &el_dt_day, &el_dt_sep1, &el_dt_month, &el_dt_sep2, &el_dt_year };
M2_HLIST(el_date, NULL, list_date);

M2_ROOT(el_dt_cancel, NULL, "cancel", &top_el_expandable_menu);
M2_BUTTON(el_dt_ok, NULL, "ok", dt_ok_fn);
M2_LIST(list_dt_buttons) = {&el_dt_cancel, &el_dt_ok };
M2_HLIST(el_dt_buttons, NULL, list_dt_buttons);

M2_LIST(list_dt) = {&el_date, &el_dt_buttons };
M2_VLIST(el_top_dt, NULL, list_dt);




//================================================================
// Arduino setup and loop
void setup() {
  serial_gps.begin(115200);
  Serial.begin(115200);
  
  sensors.begin();
  sensors.getAddress(tempDeviceAddress, 0);
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
    
  system_tz.readRules(TZ_EEPROM_ADDR);
  
  m2_SetNewLiquidCrystal(&lcd, 20, 4);
  m2.setPin(M2_KEY_SELECT, KEY_SELECT_PIN);
  m2.setPin(M2_KEY_ROT_ENC_A, ROT_ENC_A_PIN);
  m2.setPin(M2_KEY_ROT_ENC_B, ROT_ENC_B_PIN);
  /*setSyncProvider(RTC.get);   // the function to get the time from the RTC
  if(timeStatus()!= timeSet) 
     Serial.println("Unable to sync with the RTC");
  else
     Serial.println("RTC has set the system time"); */
  //t.every(1000, dt_get_from_RTC);
  setSyncProvider(gpsTimeSync);
  setSyncInterval(sync_interval);
  lcd.createChar(0, degreeChar);
}

const char *OrdinalSuffix(int x) {
  if (x % 100 - x % 10 != 10) {
    switch (x % 10) {
      case 1: return "st";
      case 2: return "nd";
      case 3: return "rd";
      default: return "th";
    }
  }
  return "th";
}

time_t prev_time = -1;
float flat, flon;
unsigned long fix_age;

void loop() {

  char c;

    
  if (menu) {
    m2.checkKey();
    if ( m2.handleKey()) {
      m2.checkKey();
      m2.draw();
    }
    if (menu == 0) { // on menu exit
      lcd.clear();
    }
  } else {
    m2.checkKey();
    uint8_t key = m2.getKey();
    if ( key == M2_KEY_NEXT || key == M2_KEY_PREV) {
      //menu = 1;
    } else if ( key == M2_KEY_SELECT ) {
      //stop alarm or something?
      menu = 1;
    }
    if ( now() != prev_time ) {
      prev_time = now();
      display();
    }
  }
  
  if (serial_gps.available()) {
    do {
      c = serial_gps.read();
      gps.encode(c);
      //Serial.print(c);
    } while (0);
    //Serial.println();
  }
  
  
}

void display() {
  
    /* Get temperature from OneWire device */
  temperature = sensors.getTempCByIndex(0);
  sensors.setWaitForConversion(false);  // makes it async
  sensors.requestTemperatures();
  sensors.setWaitForConversion(true);
  
  time_t t = system_tz.toLocal(now(), &tcr);
  Serial.print("Timezone: ");
  Serial.println(tz_list[selected_tz].name);
  char tz_buf[16] = "UTC";
  if (timeStatus() != timeNeedsSync) {
    strcpy(tz_buf, tcr -> abbrev); 
  } else {
    strcpy(tz_buf, "UTC");
    strcpy(tz_buf, tcr -> abbrev);
  }
  
  lcd.setCursor(0,0);
  sprintf(linebuf, "%02d%c%02d%c%02d %s %05.2f\x08", hour(t), TIME_SPACER, minute(t), TIME_SPACER, second(t), isAM(t) ? "AM" : "PM", temperature);
  lcd.printf("%-*s", LCD_COLS, linebuf);
  lcd.setCursor(0,1);
  strcpy(linebuf, dayShortStr(weekday(t)));
  sprintf(linebuf, "%s %d%s %s %d", linebuf, day(t), OrdinalSuffix(day(t)), monthShortStr(month(t)), year(t));
  lcd.printf("%-*s", LCD_COLS, linebuf);
  /*lcd.setCursor(0,2);
  lcd.printf("UNIX: %010d", now() - (timezone * SECS_PER_HOUR)); */
  
  lcd.setCursor(0, 2);
  if( timeStatus() == timeNotSet ) {
    lcd.print("Time not set!");
  } else {
    //lcd.printf("%04d-%02d-%02dT%02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    gps.f_get_position(&flat, &flon, &fix_age);
    if (fix_age == TinyGPS::GPS_INVALID_AGE || fix_age > 1000) {
      // print blank lines
      strcpy(linebuf, "");
      lcd.printf("%-*s", LCD_COLS, linebuf);
      lcd.setCursor(0,3);
      lcd.printf("%-*s", LCD_COLS, linebuf);
    } else {
      // print location information (latitude, longitude)
      sprintf(linebuf, "lat: %+011.6f", flat);
      lcd.printf("%-*s", LCD_COLS, linebuf);
      lcd.setCursor(0, 3);
      sprintf(linebuf, "lon: %+011.6f", flon);
      lcd.printf("%-*s", LCD_COLS, linebuf);
    }
  }
}

