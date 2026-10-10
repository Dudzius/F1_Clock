/*
 ==============================================================================
  F1 PITWALL - ESP32 + 16x2 I2C LCD desk display
 ==============================================================================
  Shows: clock, weather, F1 constructors / drivers standings, next race,
  and a buzzer alert before qualifying and the race.

  REQUIRED LIBRARIES (install through the Arduino Library Manager)
    - WiFiManager        by tzapu            (WiFi setup portal)
    - ArduinoJson        by Benoit Blanchon  (version 7.x)
    - LiquidCrystal_I2C  by Frank de Brabander / johnrickman (any common fork)
    - ESP32 board package by Espressif

  HARDWARE
    - ESP32 dev board
    - 16x2 LCD with I2C backpack (address 0x27 or 0x3F, see LCD_ADDRESS)
    - 1 push button  : GPIO 18 to GND (internal pull-up is used)
    - 1 passive buzzer: GPIO 4

  FIRST START / CHANGING WIFI
    - On first start the display shows "Connect WiFi to: F1_PITWALL".
      Join that WiFi network with your phone and enter your home WiFi.
    - To change WiFi later: hold the button while powering the device on.

  BUTTON
    - Short press : next screen
    - Long press  : (2 s) flip the night backlight state until the next
                    scheduled change (22:00 or 06:00 by default)

  HOW TIME WORKS (important)
    The old version asked internet APIs for the time zone and often failed.
    Now the ESP32 gets the time from NTP servers and works out summer/winter
    time ITSELF using the rule in TIMEZONE below. No API needed, it switches
    automatically and never needs a restart.

  SETTING UP FOR ANOTHER CITY (3 things to change in USER SETTINGS):
    1. TIMEZONE   - copy a line from the table below
    2. LATITUDE / LONGITUDE - your city (used for weather only)
    3. NIGHT_START_HOUR / NIGHT_END_HOUR if you want other dim hours
 ==============================================================================
*/

#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson (v7)
#include <LiquidCrystal_I2C.h>  // https://github.com/johnrickman/LiquidCrystal_I2C
#include <Wire.h>
#include <time.h>
#include <vector>

/*#######################################*/
/*------------ USER SETTINGS ------------*/

// ---- Time zone (POSIX rule, handles summer/winter time automatically) ----
// Vilnius / Lithuania / Eastern Europe (UTC+2, DST last Sun Mar -> last Sun Oct):
const char* const TIMEZONE = "EET-2EEST,M3.5.0/3,M10.5.0/4";
//
// Other places - replace the line above with one of these:
//   Central Europe (Berlin, Paris, Rome, Warsaw): "CET-1CEST,M3.5.0,M10.5.0/3"
//   UK / Ireland:                                 "GMT0BST,M3.5.0/1,M10.5.0"
//   Portugal:                                     "WET0WEST,M3.5.0/1,M10.5.0"
//   US Eastern (New York):                        "EST5EDT,M3.2.0,M11.1.0"
//   US Central (Chicago):                         "CST6CDT,M3.2.0,M11.1.0"
//   US Pacific (Los Angeles):                     "PST8PDT,M3.2.0,M11.1.0"
//   India:                                        "IST-5:30"
//   Japan:                                        "JST-9"
//   Sydney:                                       "AEST-10AEDT,M10.1.0,M4.1.0/3"
//   No daylight saving, plain UTC:                "UTC0"
// (Note the sign: in these rules places EAST of Greenwich are NEGATIVE.)
// More zones: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

// ---- Location for the weather forecast (Vilnius) ----
// The weather service finds the local time zone from these coordinates by itself.
const float LATITUDE = 54.6872f;
const float LONGITUDE = 25.2797f;

// ---- F1 season: "current" or a year such as "2021" ----
const char* const F1_SEASON = "current";

// ---- Night mode: backlight off from NIGHT_START_HOUR until NIGHT_END_HOUR ----
// (start must be later in the day than end, e.g. 22 and 6)
const int NIGHT_START_HOUR = 22;
const int NIGHT_END_HOUR = 6;

// ---- Weekly restart (only used if the internet has been gone for a long time)
// 1 = Monday ... 6 = Saturday, 0 = Sunday. Restarts at most once a week.
const int RESTART_WEEKDAY = 1;
const int RESTART_HOUR = 4;

// ---- Hardware ----
#define BLUE_BUTTON 18
#define BUZZER_PIN 4
#define LCD_ADDRESS 0x27  // try 0x3F if the screen stays blank

// ---- Set to 1 to see debug messages in the Serial Monitor (115200) ----
#define DEBUG_MODE 0

/*------------ USER SETTINGS ------------*/
/*#######################################*/

// ---- Other constants (normally no need to change) ----
const char* const NTP_SERVER_1 = "pool.ntp.org";
const char* const NTP_SERVER_2 = "time.nist.gov";
const char* const NTP_SERVER_3 = "lt.pool.ntp.org";  // any regional pool is fine
const time_t MIN_VALID_TIME = 1700000000;            // anything earlier = clock not synced yet
const char* const nameForAP = "F1_PITWALL";          // WiFi setup hotspot name

const unsigned long STAGE_INTERVAL_MS = 15UL * 60UL * 1000UL;  // clock -> weather every 15 min
const unsigned long CLOCK_INTERLUDE_MS = 3000;                 // clock shown between screens
const unsigned long HTTP_TIMEOUT_MS = 6000;
const unsigned long OFFLINE_WARNING_MS = 2UL * 60UL * 60UL * 1000UL;   // "!" on clock after 2 h without data
const unsigned long RESTART_OFFLINE_MS = 6UL * 60UL * 60UL * 1000UL;   // restart is allowed after 6 h without data

#if DEBUG_MODE
#define DBG(x) Serial.println(x)
#else
#define DBG(x)
#endif

#define NOTE_A4 440
#define NOTE_B4 494
#define REST 0

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// notification alert melody
const int v2melody[] = { REST, NOTE_A4, REST, NOTE_A4, REST, NOTE_A4, REST, NOTE_A4, REST, NOTE_B4 };
const int v2durations[] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 2 };

// custom characters
uint8_t formula1_1[8] = { 0b00110, 0b01100, 0b01100, 0b11111, 0b11111, 0b01100, 0b01100, 0b00110 };
uint8_t formula1_2[8] = { 0b11100, 0b11101, 0b01011, 0b11111, 0b11111, 0b01011, 0b11101, 0b11100 };
uint8_t formula1_3[8] = { 0b00000, 0b11110, 0b11111, 0b11111, 0b11111, 0b11111, 0b11110, 0b00000 };
uint8_t formula1_4[8] = { 0b11100, 0b11101, 0b01011, 0b11101, 0b11101, 0b01011, 0b11101, 0b11100 };
uint8_t temperatureIcon[8] = { 0b00100, 0b01010, 0b01010, 0b01010, 0b01110, 0b11111, 0b11111, 0b01110 };
uint8_t rainIcon[8] = { 0b00100, 0b00100, 0b01010, 0b01010, 0b10001, 0b10001, 0b10001, 0b01110 };
uint8_t celsiusIcon[8] = { 0b11000, 0b11000, 0b00000, 0b00111, 0b00100, 0b00100, 0b00111, 0b00000 };
uint8_t SunIcon_1[8] = { 0b10000, 0b01001, 0b00011, 0b10110, 0b00110, 0b00011, 0b01001, 0b10000 };
uint8_t SunIcon_2[8] = { 0b00001, 0b10010, 0b11000, 0b01101, 0b01100, 0b11000, 0b10010, 0b00001 };
uint8_t moonIcon_1[8] = { 0b00000, 0b10001, 0b00011, 0b00110, 0b00110, 0b00011, 0b00001, 0b01000 };
uint8_t moonIcon_2[8] = { 0b00000, 0b10000, 0b01000, 0b00001, 0b00000, 0b01000, 0b10000, 0b00000 };
uint8_t cloudIcon_1[8] = { 0b00000, 0b00000, 0b00000, 0b01110, 0b10001, 0b10000, 0b01111, 0b00000 };
uint8_t cloudIcon_2[8] = { 0b00000, 0b01110, 0b10001, 0b10001, 0b11001, 0b00001, 0b11110, 0b00000 };
uint8_t cloudIconFill_1[8] = { 0b00000, 0b00000, 0b00000, 0b01110, 0b11111, 0b11111, 0b01111, 0b00000 };
uint8_t cloudIconFill_2[8] = { 0b00000, 0b01110, 0b11111, 0b11111, 0b01111, 0b11111, 0b11110, 0b00000 };

/*#######################################*/
/*----------------- State ---------------*/

// screens
enum DisplayStage { STAGE_CLOCK,
                    STAGE_WEATHER,
                    STAGE_CONSTRUCTORS,
                    STAGE_DRIVERS,
                    STAGE_CALENDAR,
                    STAGE_NOTIFICATION };
DisplayStage displayStage = STAGE_CLOCK;
DisplayStage pendingStage = STAGE_CLOCK;  // screen waiting behind the 3 s clock
bool hasPending = false;
unsigned long interludeUntil = 0;
unsigned long lastStageChange = 0;
bool stageInitialized = false;  // each screen draws its static parts once per visit

struct ScrollState {
  int scrollIndex = 0;
  int scrollSpeed = 375;
  unsigned long lastScroll = 0;
  unsigned long headerUntil = 0;
};
ScrollState constructorScroll, driverScroll, weatherScroll, notificationScroll;

// clock screen
String lastDateStr = "";
unsigned long clockLastUpdate = 0;

// F1 data
int seasonYear = 0;
int currentRound = 0;
int upcomingRound = 0;
int totalRounds = 0;
bool roundKnown = false;
bool constructorsLoaded = false;
bool driversLoaded = false;
bool raceLoaded = false;
bool hasUpcomingRace = false;
int constructorCount = 0;
int driverCount = 0;
String raceErrorMsg = "";
String raceName = "";
time_t raceStart = 0;
time_t qualiStart = 0;  // 0 = no qualifying info
String constructorTop, constructorBottom, driverTop, driverBottom;
String carSymbol, carLine;

// weather data
bool weatherLoaded = false;
float currentTemp = 0, futureTemp = 0;
int currentCloud = 0, currentPrecip = 0;
int futureCloud = 0, futurePrecip = 0;

// data refresh timers (epoch seconds)
time_t nextDataCheckTime = 0;
time_t nextWeatherCheckTime = 0;
bool lastRefreshOk = false;       // false = last full download had a failure -> retry it
unsigned long lastSuccessMs = 0;  // last time any internet request worked

// calendar screen
int calPhase = 0;
String calText = "";
int calScrollIdx = 0;
unsigned long calPhaseStart = 0;
unsigned long calLastStep = 0;
String calRaceLine = "", calQualiLine = "";

// notifications
struct Notification {
  String message;
  time_t triggerTime;
  bool triggered;
};
const int MAX_NOTIFICATIONS = 3;
Notification notificationQueue[MAX_NOTIFICATIONS];
int notificationCount = 0;
int currentNotificationIndex = -1;
String notificationMsg = "";
unsigned long notifStart = 0;
unsigned long lastBlinkTime = 0;
bool textVisible = true;

// buzzer
bool buzzerActive = false;
bool buzzerNoteOn = false;
int buzzerIdx = 0;
unsigned long buzzerNoteStart = 0;

// backlight / night mode
bool backlightIsOn = true;
bool backlightToggled = false;  // long press flips the automatic state
int lastNightState = -1;

// wifi
bool wifiWasConnected = false;

/*----------------- State ---------------*/
/*#######################################*/

// forward declarations
void advanceStage();
void switchTo(DisplayStage s);

/*#######################################*/
/*----------- Helper functions ----------*/

bool timeIsValid() {
  return time(nullptr) > MIN_VALID_TIME;
}

bool isNightHour(int hour) {
  return (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR);
}

// starts (or restarts) background time sync. The ESP32 then keeps the clock
// updated by itself and applies summer/winter time from TIMEZONE.
void startTimeSync() {
  configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}

// shows 2 lines of text on the lcd
void showMessage(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// returns exactly 16 characters starting at "from" (padded with spaces)
String slice16(const String& s, int from) {
  String out = "";
  if (from < (int)s.length()) out = s.substring(from, from + 16);
  while (out.length() < 16) out += ' ';
  return out;
}

// cleans up characters not available on the lcd 1602
String sanitizeForLCD(String input) {
  static const char* const FROM[] = { "ü", "Ü", "ö", "Ö", "ä", "Ä", "ã", "Ã", "ñ", "Ñ", "é", "É", "è", "È", "á", "Á", "à", "À", "ê", "Ê", "ô", "Ô", "í", "Í", "ó", "Ó", "ç", "Ç", "ø", "Ø", "å", "Å", "ë", "ï", "ú", "Ú", "ć", "č", "š", "ž", "ł", "ń", "ś", "ß" };
  static const char* const TO[] = { "u", "U", "o", "O", "a", "A", "a", "A", "n", "N", "e", "E", "e", "E", "a", "A", "a", "A", "e", "E", "o", "O", "i", "I", "o", "O", "c", "C", "o", "O", "a", "A", "e", "i", "u", "U", "c", "c", "s", "z", "l", "n", "s", "ss" };
  const int n = sizeof(FROM) / sizeof(FROM[0]);
  for (int i = 0; i < n; i++) input.replace(FROM[i], TO[i]);

  // anything still non-ASCII is dropped so the lcd never shows garbage
  String out = "";
  for (unsigned int i = 0; i < input.length(); i++) {
    unsigned char c = (unsigned char)input[i];
    if (c >= 32 && c < 127) out += (char)c;
  }
  return out;
}

// converts a UTC date/time to epoch seconds WITHOUT using the time zone.
// (the API gives UTC; the lcd later converts to local time automatically)
time_t utcToEpoch(int y, int m, int d, int hh, int mm, int ss) {
  y -= (m <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;
  return (time_t)days * 86400 + hh * 3600 + mm * 60 + ss;
}

// parses "2026-10-25" + "13:00:00Z" (UTC) into epoch seconds
time_t parseUtc(const char* dateStr, const char* timeStr) {
  int y = 1970, mo = 1, d = 1, h = 0, mi = 0, s = 0;
  if (dateStr) sscanf(dateStr, "%d-%d-%d", &y, &mo, &d);
  if (timeStr) sscanf(timeStr, "%d:%d:%d", &h, &mi, &s);
  return utcToEpoch(y, mo, d, h, mi, s);
}

// loads the 4 custom characters used for the little race car
void createCarChars() {
  lcd.createChar(1, formula1_1);
  lcd.createChar(2, formula1_2);
  lcd.createChar(3, formula1_3);
  lcd.createChar(4, formula1_4);
}

void setBacklight(bool on) {
  if (on && !backlightIsOn) {
    lcd.backlight();
    backlightIsOn = true;
  } else if (!on && backlightIsOn) {
    lcd.noBacklight();
    backlightIsOn = false;
  }
}

// Night mode: backlight is off between NIGHT_START_HOUR and NIGHT_END_HOUR.
// A long press flips the current state; the flip is cancelled at the next
// automatic change (so you never end up with a permanently dark or bright display).
void updateBacklight() {
  if (!timeIsValid()) return;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int night = isNightHour(t.tm_hour) ? 1 : 0;
  if (night != lastNightState) {
    lastNightState = night;
    backlightToggled = false;
  }
  setBacklight((night == 1) == backlightToggled);
}

// true if the device has had no internet data for a long time (shows "!" on clock)
bool isOffline() {
  return WiFi.status() != WL_CONNECTED || (millis() - lastSuccessMs) > OFFLINE_WARNING_MS;
}

// WiFi watchdog: reconnects automatically after a router restart
void maintainWiFi() {
  static unsigned long lastRetry = 0;
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected) {
    if (!wifiWasConnected && !timeIsValid()) startTimeSync();
    wifiWasConnected = true;
  } else {
    wifiWasConnected = false;
    if (millis() - lastRetry > 30000) {
      lastRetry = millis();
      WiFi.reconnect();
    }
  }
}

// Restarts the device at most once a week (default Monday 04:xx), and ONLY
// if there has been no working internet for 6 hours. A restart resets the
// "last success" timer, so it can never loop.
void checkWeeklyRestart() {
  if ((millis() - lastSuccessMs) < RESTART_OFFLINE_MS) return;
  if (!timeIsValid()) return;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_wday == RESTART_WEEKDAY && t.tm_hour == RESTART_HOUR) {
    DBG("Weekly restart (no internet for a long time)");
    ESP.restart();
  }
}

// things that only need to run once per second
void housekeeping() {
  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  maintainWiFi();
  updateBacklight();
  checkWeeklyRestart();
}

/*----------- Helper functions ----------*/
/*#######################################*/

/*#######################################*/
/*------------- Data fetching -----------*/

// HTTPS GET with timeout and a few retries. Returns "" on failure.
// NOTE: setInsecure() skips certificate checking. That is fine here: the data
// is public (standings/weather) and nothing secret is sent.
String httpGet(const String& url, const char* what, int retries) {
  for (int attempt = 1; attempt <= retries; attempt++) {
    if (WiFi.status() != WL_CONNECTED) return "";

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    String payload = "";
    if (http.begin(client, url)) {
      int code = http.GET();
      if (code == HTTP_CODE_OK) {
        payload = http.getString();
      } else {
        DBG(String(what) + ": attempt " + String(attempt) + " failed, HTTP " + String(code));
      }
      http.end();
    }

    if (payload.length() > 0) {
      lastSuccessMs = millis();
      return payload;
    }
    if (attempt < retries) delay(1000);
  }
  return "";
}

String f1BaseUrl() {
  return String("https://api.jolpi.ca/ergast/f1/") + F1_SEASON;
}

// downloads standings and builds the two scrolling text rows
bool fetchStandings(const String& url, bool isConstructor, String& topOut, String& bottomOut, int& countOut) {
  String payload = httpGet(url, isConstructor ? "Constructors" : "Drivers", 2);
  if (payload == "") return false;

  const char* listKey = isConstructor ? "ConstructorStandings" : "DriverStandings";

  // filter: only keep the fields we need (saves a lot of memory)
  JsonDocument filter;
  JsonObject fTable = filter["MRData"].to<JsonObject>()["StandingsTable"].to<JsonObject>();
  fTable["season"] = true;
  fTable["round"] = true;
  JsonArray fLists = fTable["StandingsLists"].to<JsonArray>();
  JsonObject fList = fLists.add<JsonObject>();
  JsonArray fItems = fList[listKey].to<JsonArray>();
  JsonObject fItem = fItems.add<JsonObject>();
  fItem["position"] = true;
  fItem["points"] = true;
  fItem["wins"] = true;
  if (isConstructor) {
    fItem["Constructor"]["name"] = true;
  } else {
    fItem["Driver"]["code"] = true;
    fItem["Driver"]["familyName"] = true;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    DBG(String("Standings JSON error: ") + error.c_str());
    return false;
  }

  JsonObject table = doc["MRData"]["StandingsTable"];
  seasonYear = table["season"].as<int>();
  currentRound = table["round"].as<int>();
  roundKnown = true;

  JsonArray list = table["StandingsLists"][0][listKey];

  std::vector<String> lines;
  for (JsonObject item : list) {
    String line = "P" + String(item["position"].as<int>()) + " ";

    if (isConstructor) {
      const char* name = item["Constructor"]["name"].as<const char*>();
      line += sanitizeForLCD(name ? name : "?");
    } else {
      const char* code = item["Driver"]["code"].as<const char*>();
      if (code) {
        line += code;
      } else {
        const char* fam = item["Driver"]["familyName"].as<const char*>();
        line += sanitizeForLCD(fam ? fam : "?");
      }
    }

    line += " ";
    int wins = item["wins"].as<int>();
    if (wins > 0) line += String(wins) + "w ";  // only show wins when above 0 (saves space)
    const char* pts = item["points"].as<const char*>();
    line += String(pts ? pts : "0") + "pts";
    lines.push_back(line);
  }

  // build the two ticker rows once (cars alternate between top and bottom row)
  String top = "            ";
  String bottom = "                    ";
  for (size_t i = 0; i < lines.size(); i++) {
    String entry = "    " + carSymbol + "  " + lines[i];
    if (i % 2 == 0) top += entry;
    else bottom += entry;
  }
  topOut = top;
  bottomOut = bottom;
  countOut = (int)lines.size();
  return true;
}

// finds the next race (and its qualifying time)
bool fetchUpcomingRace(const String& url) {
  String payload = httpGet(url, "Calendar", 2);
  if (payload == "") return false;

  JsonDocument filter;
  JsonObject fMD = filter["MRData"].to<JsonObject>();
  fMD["total"] = true;
  JsonObject fRT = fMD["RaceTable"].to<JsonObject>();
  JsonArray fRaces = fRT["Races"].to<JsonArray>();
  JsonObject fRace = fRaces.add<JsonObject>();
  fRace["round"] = true;
  fRace["raceName"] = true;
  fRace["date"] = true;
  fRace["time"] = true;
  fRace["Qualifying"]["date"] = true;
  fRace["Qualifying"]["time"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    DBG(String("Calendar JSON error: ") + error.c_str());
    return false;
  }

  totalRounds = doc["MRData"]["total"].as<int>();
  JsonArray races = doc["MRData"]["RaceTable"]["Races"].as<JsonArray>();

  hasUpcomingRace = false;
  raceErrorMsg = "";

  if (currentRound >= totalRounds) {
    raceErrorMsg = "No upcoming race";
    return true;
  }

  for (JsonObject r : races) {
    int round = r["round"].as<int>();
    if (round != currentRound + 1) continue;

    const char* name = r["raceName"].as<const char*>();
    const char* date = r["date"].as<const char*>();
    const char* rTime = r["time"].as<const char*>();
    const char* qDate = r["Qualifying"]["date"].as<const char*>();
    const char* qTime = r["Qualifying"]["time"].as<const char*>();

    if (!date) break;
    raceStart = parseUtc(date, rTime);
    qualiStart = qDate ? parseUtc(qDate, qTime) : 0;
    raceName = sanitizeForLCD(name ? name : "Grand Prix");
    upcomingRound = round;
    hasUpcomingRace = true;
    return true;
  }

  raceErrorMsg = "Race not found";
  return true;
}

// weather for now and +4 hours (location is hardcoded in USER SETTINGS)
bool fetchCurrentWeather() {
  if (!timeIsValid()) return false;  // we need the real hour to pick the right forecast entry

  String url = "https://api.open-meteo.com/v1/forecast?";
  url += "latitude=" + String(LATITUDE, 4);
  url += "&longitude=" + String(LONGITUDE, 4);
  url += "&hourly=apparent_temperature,precipitation_probability,cloud_cover&timezone=auto&forecast_days=2";

  String payload = httpGet(url, "Weather", 2);
  if (payload == "") return false;

  JsonDocument filter;
  filter["hourly"]["apparent_temperature"] = true;
  filter["hourly"]["precipitation_probability"] = true;
  filter["hourly"]["cloud_cover"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    DBG(String("Weather JSON error: ") + error.c_str());
    return false;
  }

  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  int h = timeinfo.tm_hour;

  JsonArray tempArray = doc["hourly"]["apparent_temperature"];
  JsonArray cloudArray = doc["hourly"]["cloud_cover"];
  JsonArray precipArray = doc["hourly"]["precipitation_probability"];

  if ((int)tempArray.size() < h + 5 || (int)cloudArray.size() < h + 5 || (int)precipArray.size() < h + 5) return false;

  currentTemp = tempArray[h].as<float>();
  currentCloud = cloudArray[h].as<int>();
  currentPrecip = precipArray[h].as<int>();
  futureTemp = tempArray[h + 4].as<float>();
  futureCloud = cloudArray[h + 4].as<int>();
  futurePrecip = precipArray[h + 4].as<int>();
  weatherLoaded = true;

  DBG(String("Weather now: ") + currentTemp + "C " + currentCloud + "% clouds " + currentPrecip + "% rain");
  return true;
}

// light request: only asks which round the standings are at
int fetchCurrentRound() {
  String payload = httpGet(f1BaseUrl() + "/driverstandings/?format=json", "RoundCheck", 2);
  if (payload == "") return -1;

  JsonDocument filter;
  filter["MRData"]["StandingsTable"]["round"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) return -1;
  return doc["MRData"]["StandingsTable"]["round"].as<int>();
}

// 3 reminders per race weekend (quali notifications are skipped if there is no quali time)
void scheduleRaceNotifications() {
  notificationCount = 0;
  if (!hasUpcomingRace) return;

  if (qualiStart > 0) {
    notificationQueue[notificationCount].message = "Make Predictions";
    notificationQueue[notificationCount].triggerTime = qualiStart - 60 * 60;
    notificationQueue[notificationCount].triggered = false;
    notificationCount++;

    notificationQueue[notificationCount].message = " Quali in 5 min ";
    notificationQueue[notificationCount].triggerTime = qualiStart - 5 * 60;
    notificationQueue[notificationCount].triggered = false;
    notificationCount++;
  }

  notificationQueue[notificationCount].message = " Race in 5 min! ";
  notificationQueue[notificationCount].triggerTime = raceStart - 5 * 60;
  notificationQueue[notificationCount].triggered = false;
  notificationCount++;
}

// downloads everything from the F1 API and schedules the next refresh
void refreshAllData() {
  String base = f1BaseUrl();

  bool okC = fetchStandings(base + "/constructorstandings/?format=json", true, constructorTop, constructorBottom, constructorCount);
  if (okC) constructorsLoaded = true;
  delay(500);

  bool okD = fetchStandings(base + "/driverstandings/?format=json", false, driverTop, driverBottom, driverCount);
  if (okD) driversLoaded = true;
  delay(500);

  // the "next race" needs a known round number, so only ask once standings worked
  bool okR = false;
  if (roundKnown) {
    okR = fetchUpcomingRace(base + "/races/?format=json");
    if (okR) raceLoaded = true;
  }

  scheduleRaceNotifications();

  time_t now = time(nullptr);
  lastRefreshOk = (okC && okD && okR);
  if (!lastRefreshOk) {
    nextDataCheckTime = now + 5 * 60;  // something failed: try again in 5 minutes
  } else if (hasUpcomingRace) {
    // check for new standings 3 hours after the race starts
    time_t t = raceStart + 3 * 60 * 60;
    if (t < now + 10 * 60) t = now + 10 * 60;
    nextDataCheckTime = t;
  } else {
    nextDataCheckTime = now + 24 * 60 * 60;  // off-season: look again tomorrow
  }
}

// decides when to re-download data (runs from loop)
void checkForNewDataIfTime() {
  if (displayStage == STAGE_NOTIFICATION) return;  // never stall the alert
  if (WiFi.status() != WL_CONNECTED) return;
  if (!timeIsValid()) return;

  time_t now = time(nullptr);

  if (now >= nextDataCheckTime) {
    if (!lastRefreshOk || !hasUpcomingRace) {
      refreshAllData();
    } else {
      int fetchedRound = fetchCurrentRound();
      if (fetchedRound > currentRound) refreshAllData();
      else nextDataCheckTime = now + 30 * 60;
    }
  }

  if (now >= nextWeatherCheckTime) {
    bool ok = fetchCurrentWeather();
    nextWeatherCheckTime = now + (ok ? 60 * 60 : 5 * 60);
  }
}

/*------------- Data fetching -----------*/
/*#######################################*/

/*#######################################*/
/*------------- Buzzer / input ----------*/

void startBuzzer() {
  buzzerActive = true;
  buzzerNoteOn = false;
  buzzerIdx = 0;
}

// non-blocking melody player
void updateBuzzer() {
  if (!buzzerActive) return;
  const int NUM_NOTES = sizeof(v2melody) / sizeof(v2melody[0]);
  unsigned long now = millis();

  if (!buzzerNoteOn) {
    if (buzzerIdx >= NUM_NOTES) {
      noTone(BUZZER_PIN);
      buzzerActive = false;
      return;
    }
    int noteDuration = 1000 / v2durations[buzzerIdx];
    if (v2melody[buzzerIdx] > 0) tone(BUZZER_PIN, v2melody[buzzerIdx], noteDuration);
    buzzerNoteStart = now;
    buzzerNoteOn = true;
  } else {
    int noteDuration = 1000 / v2durations[buzzerIdx];
    if (now - buzzerNoteStart >= (unsigned long)(noteDuration + 20)) {
      noTone(BUZZER_PIN);
      buzzerIdx++;
      buzzerNoteOn = false;
    }
  }
}

// short press = next screen, long press (2 s) = flip night backlight
void handleButtonPress() {
  static bool lastReading = HIGH;
  static bool stableState = HIGH;
  static unsigned long lastChange = 0;
  static unsigned long pressedAt = 0;
  static bool longHandled = false;
  const unsigned long debounceMs = 50;
  const unsigned long longPressMs = 2000;

  bool reading = digitalRead(BLUE_BUTTON);
  if (reading != lastReading) {
    lastChange = millis();
    lastReading = reading;
  }

  if ((millis() - lastChange) > debounceMs && reading != stableState) {
    stableState = reading;
    if (stableState == LOW) {
      pressedAt = millis();
      longHandled = false;
    } else if (!longHandled) {
      advanceStage();  // released after a short press
    }
  }

  if (stableState == LOW && !longHandled && (millis() - pressedAt) >= longPressMs) {
    longHandled = true;
    backlightToggled = !backlightToggled;
    updateBacklight();
  }
}

// triggers a scheduled notification when its time comes
void checkNotifications() {
  if (currentNotificationIndex != -1) return;  // one at a time
  if (!timeIsValid()) return;
  time_t now = time(nullptr);

  for (int i = 0; i < notificationCount; i++) {
    Notification& n = notificationQueue[i];
    if (!n.triggered && now >= n.triggerTime && now < n.triggerTime + 5 * 60) {
      n.triggered = true;
      notificationMsg = n.message;
      currentNotificationIndex = i;
      hasPending = false;
      switchTo(STAGE_NOTIFICATION);
      DBG(String("Notification: ") + n.message);
      return;
    }
  }
}

/*------------- Buzzer / input ----------*/
/*#######################################*/

/*#######################################*/
/*------------ Screen handling ----------*/

// is there anything to show on this screen?
bool stageAvailable(DisplayStage s) {
  switch (s) {
    case STAGE_WEATHER: return weatherLoaded;
    case STAGE_CONSTRUCTORS: return constructorsLoaded && constructorCount > 0;
    case STAGE_DRIVERS: return driversLoaded && driverCount > 0;
    case STAGE_CALENDAR: return raceLoaded;
    default: return true;
  }
}

// order: Clock -> Weather -> Constructors -> Drivers -> Calendar -> Clock
// (screens without data are skipped)
DisplayStage nextStageInCycle(DisplayStage s) {
  DisplayStage n = s;
  for (int i = 0; i < 6; i++) {
    switch (n) {
      case STAGE_CLOCK: n = STAGE_WEATHER; break;
      case STAGE_WEATHER: n = STAGE_CONSTRUCTORS; break;
      case STAGE_CONSTRUCTORS: n = STAGE_DRIVERS; break;
      case STAGE_DRIVERS: n = STAGE_CALENDAR; break;
      default: n = STAGE_CLOCK; break;
    }
    if (n == STAGE_CLOCK || stageAvailable(n)) return n;
  }
  return STAGE_CLOCK;
}

// enters a screen with a clean state
void switchTo(DisplayStage s) {
  displayStage = s;
  stageInitialized = false;
  lastStageChange = millis();
  lastDateStr = "";
  clockLastUpdate = 0;
  calPhase = 0;
  if (s != STAGE_NOTIFICATION) currentNotificationIndex = -1;
  buzzerActive = false;
  noTone(BUZZER_PIN);
  lcd.clear();
}

// moves to the next screen. Between two data screens the clock is shown for 3 s.
void advanceStage() {
  // pressed (or timer ended) during the 3 s clock -> go to the waiting screen
  if (hasPending) {
    hasPending = false;
    switchTo(pendingStage);
    return;
  }

  DisplayStage next = nextStageInCycle(displayStage);
  bool fromDataScreen = (displayStage != STAGE_CLOCK && displayStage != STAGE_NOTIFICATION);

  if (fromDataScreen && next != STAGE_CLOCK) {
    pendingStage = next;
    hasPending = true;
    interludeUntil = millis() + CLOCK_INTERLUDE_MS;
    next = STAGE_CLOCK;
  }
  switchTo(next);
}

// clock -> weather automatically every 15 min; ends the 3 s clock between screens
void autoAdvanceStage() {
  if (displayStage != STAGE_CLOCK) return;
  if (hasPending) {
    if (millis() >= interludeUntil) advanceStage();
    return;
  }
  if (millis() - lastStageChange > STAGE_INTERVAL_MS) advanceStage();
}

/*------------ Screen handling ----------*/
/*#######################################*/

/*#######################################*/
/*----------- Stages rendering ----------*/

// current time and date
void renderClockStage() {
  if (millis() - clockLastUpdate < 1000) return;
  clockLastUpdate = millis();

  if (!timeIsValid()) {
    lastDateStr = "";
    lcd.setCursor(0, 0);
    lcd.print(" Time not set   ");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.status() == WL_CONNECTED ? " syncing time..." : " no WiFi, retry ");
    return;
  }

  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char dateBuf[17], timeBuf[17];
  strftime(dateBuf, sizeof(dateBuf), "%a %b %d", &timeinfo);
  strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);

  String dateStr = String(dateBuf);
  String timeStr = String(timeBuf);

  if (dateStr != lastDateStr) {
    lastDateStr = dateStr;
    String row = "";
    int pad = (16 - (int)dateStr.length()) / 2;
    for (int i = 0; i < pad; i++) row += ' ';
    row += dateStr;
    while (row.length() < 16) row += ' ';
    lcd.setCursor(0, 0);
    lcd.print(row);
  }

  String row1 = "";
  int pad1 = (16 - (int)timeStr.length()) / 2;
  for (int i = 0; i < pad1; i++) row1 += ' ';
  row1 += timeStr;
  while (row1.length() < 16) row1 += ' ';
  lcd.setCursor(0, 1);
  lcd.print(row1);

  // small "!" in the corner = no fresh internet data for a while
  lcd.setCursor(15, 0);
  lcd.print(isOffline() ? '!' : ' ');
}

// constructors / drivers scrolling ticker
void renderScrollingStage(const String& header1, const String& header2, const String& top, const String& bottom, ScrollState& s) {
  unsigned long now = millis();

  if (!stageInitialized) {
    stageInitialized = true;
    createCarChars();
    lcd.setCursor(0, 0);
    lcd.print(header1);
    lcd.setCursor(0, 1);
    lcd.print(header2);
    s.scrollIndex = 0;
    s.lastScroll = now;
    s.headerUntil = now + 3000;  // show the title for 3 s first
    return;
  }

  if (now < s.headerUntil) return;
  if (now - s.lastScroll < (unsigned long)s.scrollSpeed) return;
  s.lastScroll = now;

  lcd.setCursor(0, 0);
  lcd.print(slice16(top, s.scrollIndex));
  lcd.setCursor(0, 1);
  lcd.print(slice16(bottom, s.scrollIndex));

  s.scrollIndex++;
  int maxLen = ((int)top.length() > (int)bottom.length()) ? (int)top.length() : (int)bottom.length();
  if (s.scrollIndex > maxLen) advanceStage();
}

void renderConstructorsStage() {
  const char fillChar = (char)255;
  const String spacer = String(fillChar) + String(fillChar) + String(fillChar);
  String header1 = spacer + " F1 W C C " + spacer;
  String header2 = String(seasonYear) + " Race:" + String(currentRound) + "/" + String(totalRounds);
  renderScrollingStage(header1, header2, constructorTop, constructorBottom, constructorScroll);
}

void renderDriversStage() {
  const char fillChar = (char)255;
  const String spacer = String(fillChar) + String(fillChar) + String(fillChar);
  String header1 = spacer + " F1 W D C " + spacer;
  String header2 = String(seasonYear) + " Race:" + String(currentRound) + "/" + String(totalRounds);
  renderScrollingStage(header1, header2, driverTop, driverBottom, driverScroll);
}

// "Today" / "Tomorrow" / "5 days" until the race (in local time)
String whenText() {
  if (!timeIsValid()) return "";
  time_t now = time(nullptr);
  struct tm a, b;
  localtime_r(&now, &a);
  localtime_r(&raceStart, &b);
  a.tm_hour = a.tm_min = a.tm_sec = 0;
  b.tm_hour = b.tm_min = b.tm_sec = 0;
  a.tm_isdst = -1;
  b.tm_isdst = -1;
  long dayDiff = lround((double)(mktime(&b) - mktime(&a)) / 86400.0);

  if (dayDiff < 0) return "Passed";
  if (dayDiff == 0) return "Today";
  if (dayDiff == 1) return "Tomorrow";
  return String(dayDiff) + " days";
}

void renderRaceError() {
  static unsigned long errorStart = 0;
  if (!stageInitialized) {
    stageInitialized = true;
    errorStart = millis();
    lcd.setCursor(0, 0);
    lcd.print("Race info error");
    lcd.setCursor(0, 1);
    lcd.print(raceErrorMsg.substring(0, 16));
  }
  if (millis() - errorStart > 7000) advanceStage();
}

// next race: name + days left (scrolls if long), then race and quali times
void renderCalendarStage() {
  if (!hasUpcomingRace) {
    renderRaceError();
    return;
  }

  const unsigned long holdMs = 2000;
  const unsigned long timesMs = 5000;
  unsigned long now = millis();

  switch (calPhase) {
    case 0: {  // draw title + start of the race name
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Next race: " + String(upcomingRound) + "/" + String(totalRounds));
      String when = whenText();
      calText = raceName + (when.length() ? "  |  " + when : String(""));
      lcd.setCursor(0, 1);
      lcd.print(slice16(calText, 0));
      calScrollIdx = 0;
      calPhaseStart = now;
      calPhase = 1;
      break;
    }
    case 1:  // hold
      if (now - calPhaseStart >= holdMs) {
        calPhase = ((int)calText.length() > 16) ? 2 : 4;
        calPhaseStart = now;
        calLastStep = now;
      }
      break;
    case 2:  // scroll
      if (now - calLastStep >= 375) {
        calLastStep = now;
        calScrollIdx++;
        lcd.setCursor(0, 1);
        lcd.print(slice16(calText, calScrollIdx));
        if (calScrollIdx >= (int)calText.length() - 16) {
          calPhase = 3;
          calPhaseStart = now;
        }
      }
      break;
    case 3:  // hold at the end
      if (now - calPhaseStart >= holdMs) calPhase = 4;
      break;
    case 4: {  // draw race + quali times (local time)
      lcd.clear();
      char raceTimeStr[17], qualiTimeStr[17];
      struct tm raceLocal, qualiLocal;
      localtime_r(&raceStart, &raceLocal);
      strftime(raceTimeStr, sizeof(raceTimeStr), "R: %b %d %H:%M", &raceLocal);
      if (qualiStart > 0) {
        localtime_r(&qualiStart, &qualiLocal);
        strftime(qualiTimeStr, sizeof(qualiTimeStr), "Q: %b %d %H:%M", &qualiLocal);
      } else {
        strcpy(qualiTimeStr, "Q: n/a");
      }
      lcd.setCursor(0, 0);
      lcd.print(raceTimeStr);
      lcd.setCursor(0, 1);
      lcd.print(qualiTimeStr);
      calPhaseStart = now;
      calPhase = 5;
      break;
    }
    case 5:
      if (now - calPhaseStart >= timesMs) advanceStage();
      break;
  }
}

// flashing alert with moving cars and the buzzer
void renderNotificationStage() {
  const unsigned long duration = 15000;
  const unsigned long blinkInterval = 500;
  unsigned long now = millis();

  if (!stageInitialized) {
    stageInitialized = true;
    createCarChars();
    notifStart = now;
    notificationScroll.lastScroll = 0;
    notificationScroll.scrollIndex = 0;
    lastBlinkTime = 0;
    textVisible = true;
    startBuzzer();
  }

  updateBuzzer();

  if (now - notifStart > duration) {
    notificationMsg = "";
    currentNotificationIndex = -1;
    advanceStage();
    return;
  }

  if (now - notificationScroll.lastScroll >= (unsigned long)notificationScroll.scrollSpeed) {
    lcd.setCursor(0, 0);
    lcd.print(slice16(carLine, notificationScroll.scrollIndex));
    notificationScroll.scrollIndex++;
    if (notificationScroll.scrollIndex > (int)carLine.length() - 16) notificationScroll.scrollIndex = 0;
    notificationScroll.lastScroll = now;
  }

  if (now - lastBlinkTime >= blinkInterval) {
    lcd.setCursor(0, 1);
    lcd.print(textVisible ? slice16(notificationMsg, 0) : String("                "));
    textVisible = !textVisible;
    lastBlinkTime = now;
  }
}

// weather now and +4 h, scrolling
void renderWeatherStage() {
  static String line1, line2;
  static int period1 = 1, period2 = 1;
  static unsigned long weatherStart = 0;
  const String header1 = "Now: ";
  const String header2 = "+4h: ";
  const int scrollAreaWidth = 16 - 5;
  unsigned long now = millis();

  if (!stageInitialized) {
    stageInitialized = true;
    weatherStart = now;
    weatherScroll.scrollIndex = 0;
    weatherScroll.lastScroll = 0;

    lcd.createChar(1, temperatureIcon);
    lcd.createChar(2, rainIcon);
    lcd.createChar(3, celsiusIcon);

    // sun / moon / cloud icon depends on cloud cover (moon only at night)
    time_t t = time(nullptr);
    struct tm tmNow;
    localtime_r(&t, &tmNow);
    bool nightNow = isNightHour(tmNow.tm_hour);
    bool nightLater = isNightHour((tmNow.tm_hour + 4) % 24);

    auto setCloudIcons = [](int base, int cloud, bool night) {
      if (cloud < 30) {
        lcd.createChar(base, night ? moonIcon_1 : SunIcon_1);
        lcd.createChar(base + 1, night ? moonIcon_2 : SunIcon_2);
      } else if (cloud < 70) {
        lcd.createChar(base, cloudIcon_1);
        lcd.createChar(base + 1, cloudIcon_2);
      } else {
        lcd.createChar(base, cloudIconFill_1);
        lcd.createChar(base + 1, cloudIconFill_2);
      }
    };
    setCloudIcons(4, currentCloud, nightNow);
    setCloudIcons(6, futureCloud, nightLater);

    const String tempSymbol = String((char)1);
    const String rainSymbol = String((char)2);
    const String celsiusSymbol = String((char)3);
    const String weatherSymbol = String((char)4) + String((char)5);
    const String futureSymbol = String((char)6) + String((char)7);

    String d1 = tempSymbol + " " + String(currentTemp, 1) + celsiusSymbol + "  " + rainSymbol + " " + String(currentPrecip) + "%  " + weatherSymbol + " " + String(currentCloud) + "%";
    String d2 = tempSymbol + " " + String(futureTemp, 1) + celsiusSymbol + "  " + rainSymbol + " " + String(futurePrecip) + "%  " + futureSymbol + " " + String(futureCloud) + "%";

    line1 = "    " + d1 + "    " + d1 + "    " + d1;
    line2 = "    " + d2 + "    " + d2 + "    " + d2;
    period1 = d1.length() + 4;  // one repeat of "    " + data
    period2 = d2.length() + 4;
  }

  if (now - weatherScroll.lastScroll >= (unsigned long)weatherScroll.scrollSpeed) {
    int o1 = weatherScroll.scrollIndex % period1;
    int o2 = weatherScroll.scrollIndex % period2;
    lcd.setCursor(0, 0);
    lcd.print(header1);
    lcd.print(line1.substring(o1, o1 + scrollAreaWidth));
    lcd.setCursor(0, 1);
    lcd.print(header2);
    lcd.print(line2.substring(o2, o2 + scrollAreaWidth));
    weatherScroll.scrollIndex++;
    weatherScroll.lastScroll = now;
  }

  if (now - weatherStart > 23000) advanceStage();
}

void renderCurrentStage() {
  switch (displayStage) {
    case STAGE_CLOCK: renderClockStage(); break;
    case STAGE_WEATHER: renderWeatherStage(); break;
    case STAGE_CONSTRUCTORS: renderConstructorsStage(); break;
    case STAGE_DRIVERS: renderDriversStage(); break;
    case STAGE_CALENDAR: renderCalendarStage(); break;
    case STAGE_NOTIFICATION: renderNotificationStage(); break;
  }
}

/*----------- Stages rendering ----------*/
/*#######################################*/

/*#######################################*/
/*------------------ Setup --------------*/

// connects to saved WiFi, or opens the setup portal on first start.
// If the saved WiFi is just temporarily gone (power cut, router rebooting) the
// device does NOT stop: it runs as a clock and keeps reconnecting.
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  bool forcePortal = false;
  bool connected = false;

  // hold the button while powering on to forget the saved WiFi
  if (digitalRead(BLUE_BUTTON) == LOW) {
    showMessage("Resetting WiFi..", "");
    delay(1500);
    if (digitalRead(BLUE_BUTTON) == LOW) {
      wm.resetSettings();
      forcePortal = true;
    }
  }

  if (!forcePortal) {
    showMessage("Checking WiFi...", "");
    WiFi.begin();
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
      delay(250);
    }
    connected = (WiFi.status() == WL_CONNECTED);
  }

  bool hasSavedWiFi = (WiFi.SSID().length() > 0);

  // no WiFi saved yet (or reset requested): open the setup hotspot
  if (!connected && (forcePortal || !hasSavedWiFi)) {
    showMessage("Connect WiFi to:", nameForAP);
    while (!connected) {
      connected = wm.startConfigPortal(nameForAP);
    }
  }

  if (connected) {
    WiFi.setAutoReconnect(true);
  } else {
    showMessage("WiFi not found", "Retrying...");
    delay(2000);
  }
}

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  pinMode(BLUE_BUTTON, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  lastSuccessMs = millis();

  // car symbol (custom chars 1-4) and the car row used by the alert screen
  carSymbol = String((char)1) + String((char)2) + String((char)3) + String((char)4);
  carLine = "";
  while (carLine.length() < 24) carLine += carSymbol + "    ";

  // apply the time zone rule right away (before the first NTP sync)
  setenv("TZ", TIMEZONE, 1);
  tzset();

  setupWiFi();
  wifiWasConnected = (WiFi.status() == WL_CONNECTED);
  startTimeSync();

  if (wifiWasConnected) {
    showMessage("WiFi Connected", "Syncing time...");
    unsigned long t0 = millis();
    while (!timeIsValid() && millis() - t0 < 15000) delay(200);

    showMessage("WiFi Connected", "Fetching data...");
    refreshAllData();
    nextWeatherCheckTime = 0;  // weather is fetched right away by the first loop
    if (fetchCurrentWeather()) nextWeatherCheckTime = time(nullptr) + 60 * 60;
  }

  switchTo(STAGE_CLOCK);

#if DEBUG_MODE
  Serial.println("Setup done");
  Serial.println(String("Time valid: ") + (timeIsValid() ? "yes" : "no"));
#endif
}

void loop() {
  handleButtonPress();
  housekeeping();
  autoAdvanceStage();
  checkNotifications();
  checkForNewDataIfTime();
  renderCurrentStage();
}

/*------------------ Setup --------------*/
/*#######################################*/
