#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
//#include <LiquidCrystal.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <time.h>
//#include <esp_sntp.h>
//#include <Preferences.h>

#define BLUE_BUTTON 15
#define BUZZER_PIN 4

#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 494
#define REST 0


LiquidCrystal_I2C lcd(0x27,16,2);

// for notification melody (not the most pleasent sound but good enough)
const int melody[] = {
  /*REST, NOTE_A4, NOTE_A4, NOTE_A4, REST, 
  NOTE_E4, NOTE_E4,            
  NOTE_E4, NOTE_G4, NOTE_E4,  REST,    */

  /*REST, NOTE_E4, NOTE_E4, 
  NOTE_E4, NOTE_D4, NOTE_F4, REST,

  NOTE_A4, NOTE_A4, NOTE_B4, NOTE_C4, NOTE_A4,  
  NOTE_G4, NOTE_G4, NOTE_E4, NOTE_F4,   */

  NOTE_A4, NOTE_A4, NOTE_B4, NOTE_C4, NOTE_A4,
  NOTE_G4, NOTE_G4, NOTE_C4, NOTE_D4,

  NOTE_D4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_D4,
  NOTE_C4, NOTE_C4, NOTE_A4, NOTE_B4,

  NOTE_D4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_D4,
  NOTE_C4, NOTE_C4, NOTE_E4, NOTE_F4,

  NOTE_A4, NOTE_A4, NOTE_A4, REST,
  NOTE_A4, NOTE_A4,
  NOTE_A4, NOTE_G4, NOTE_B4, REST
};
const int durations[] = {
  /*4,3,3,3,4,
  3,3,
  5,5,5,4,*/

  /*4,4,4,
  6,6,2,4,

  2,2,6,6,6,
  2,2,2,1,*/

  2, 2, 6, 6, 6,
  2, 2, 2, 1,

  2, 2, 6, 6, 6,
  2, 2, 2, 1,

  2, 2, 6, 6, 6,
  2, 2, 2, 1,

  4, 4, 4, 2,
  4, 4,
  6, 6, 2, 2
};

// formula 1 car made from 4 custom characters
uint8_t formula1_4[8] = {
  0b00110,
  0b01100,
  0b01100,
  0b11111,
  0b11111,
  0b01100,
  0b01100,
  0b00110,
};
uint8_t formula2_4[8] = {
  0b11100,
  0b11101,
  0b01011,
  0b11111,
  0b11111,
  0b01011,
  0b11101,
  0b11100,
};
uint8_t formula3_4[8] = {
  0b00000,
  0b11110,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11110,
  0b00000,
};
uint8_t formula4_4[8] = {
  0b11100,
  0b11101,
  0b01011,
  0b11101,
  0b11101,
  0b01011,
  0b11101,
  0b11100,
};

//Preferences prefs;
int ChosenSeasonYear = 0;  // Global season year

HTTPClient http;
const char* nameForAP = "F1_Standings";  // hotspot name

// display stages for different data
enum DisplayStage { STAGE_CONSTRUCTORS,
                    STAGE_DRIVERS,
                    STAGE_CALENDAR,
                    STAGE_CLOCK,
                    STAGE_NOTIFICATION };
static DisplayStage displayStage = STAGE_CLOCK;

enum BuzzerState { BUZZER_IDLE,
                   BUZZER_PLAYING_NOTE,
                   BUZZER_RESTING };
BuzzerState buzzerState = BUZZER_IDLE;
int buzzerIndex = 0;
unsigned long buzzerStartTime = 0;
bool buzzerActive = false;

bool raceNotified = false;
bool qualiNotified = false;
bool predictionNotified = false;
String notificationMsg = "";

unsigned long lastStageChange = 0;
const unsigned long stageInterval = 15UL * 60UL * 1000UL;

// variables for data
int seasonYear = 0;
int currentRound = 0;
int upcomingRound = 0;
int totalRounds = 0;
String raceErrorMsg;
String raceName;
time_t raceStart, qualiStart;
std::vector<String> bodyLines;
std::vector<String> constructorLines;
std::vector<String> driverLines;

struct ScrollState {
  int scrollIndex = 0;
  int scrollSpeed = 375;
  int state = 0;
  unsigned long lastScroll = 0;
};
ScrollState constructorScroll, driverScroll, calendarScroll;
bool calendarFirstEntry = true;
bool calendarScrolling = false;
bool calendarDoneScrolling = false;
unsigned long calendarStepStartTime = 0;

// variables for time and date
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const char* time_zone = "EET-2EEST,M3.5.0/3,M10.5.0/4";  //Europe/Vilnius
String lastDateStr = "";

bool waitingForNextData = false;
time_t nextDataCheckTime = 0;

bool isNightMode = false;

/*-----------Data fetching-----------*/

// for getting upcoming race data to display
void fetchUpcomingRace(const char* url) {
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Failed to fetch race data");
    raceErrorMsg = "HTTP request";
    http.end();
    return;
  }

  // Prepare filter
  JsonDocument filter;
  JsonObject f_mrdata = filter["MRData"].to<JsonObject>();
  f_mrdata["total"] = true;
  JsonObject f_raceTable = f_mrdata["RaceTable"].to<JsonObject>();

  JsonObject f_race0 = f_raceTable["Races"].add<JsonObject>();
  f_race0["round"] = true;
  f_race0["raceName"] = true;
  f_race0["date"] = true;
  f_race0["time"] = true;

  JsonObject f_qual = f_race0["Qualifying"].to<JsonObject>();
  f_qual["date"] = true;
  f_qual["time"] = true;


  DynamicJsonDocument doc(512);

  DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (error) {
    Serial.print("Deserialization failed: ");
    Serial.println(error.c_str());
    raceErrorMsg = "JSON parse";
    return;
  }

  JsonObject races = doc["MRData"];
  totalRounds = (int)races["total"];
  JsonArray raceList = races["RaceTable"]["Races"].as<JsonArray>();

  if (currentRound >= totalRounds) {
    raceErrorMsg = "No upcoming races";
    return;
  }
  for (JsonObject race : raceList) {
    upcomingRound = (int)race["round"];

    if (upcomingRound == currentRound + 1) {
      raceName = (String)race["raceName"];
      const char* date = race["date"];
      const char* time = race["time"];

      const char* qualDate = race["Qualifying"]["date"];
      const char* qualTime = race["Qualifying"]["time"];

      // Parse race time as UTC
      raceStart = parseUtcToLocal(date, time);
      qualiStart = parseUtcToLocal(qualDate, qualTime);

      return;
    }
  }

  raceErrorMsg = "";
}

// parsing json data for drivers and constructors standings
void fetchAndFormatStandings(const char* url, bool isConstructor, std::vector<String>& bodyLines) {
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("HTTP request failed");
    return;
  }

  String json = http.getString();
  DynamicJsonDocument filter(256);
  JsonObject mrData = filter.createNestedObject("MRData");
  JsonObject standingsTable = mrData.createNestedObject("StandingsTable");
  standingsTable["season"] = true;
  standingsTable["round"] = true;
  JsonArray standingsLists = standingsTable.createNestedArray("StandingsLists");
  JsonObject list0 = standingsLists.createNestedObject();
  JsonArray standingsArray = list0.createNestedArray(isConstructor ? "ConstructorStandings" : "DriverStandings");
  JsonObject standing0 = standingsArray.createNestedObject();
  standing0["position"] = true;
  standing0["points"] = true;
  standing0["wins"] = true;

  if (isConstructor) {
    standing0.createNestedObject("Constructor")["name"] = true;
  } else {
    standing0.createNestedObject("Driver")["code"] = true;
  }

  DynamicJsonDocument doc(2048);

  DeserializationError error = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  http.end();

  if (error) {
    Serial.print("Deserialization failed: ");
    Serial.println(error.c_str());
    return;
  }

  JsonObject standings = doc["MRData"]["StandingsTable"];
  seasonYear = (int)standings["season"];
  currentRound = (int)standings["round"];

  JsonArray standingsList = standings["StandingsLists"][0][isConstructor ? "ConstructorStandings" : "DriverStandings"];

  bodyLines.clear();

  for (JsonObject standingData : standingsList) {
    String line = "P" + String((int)standingData["position"]) + " ";

    if (isConstructor) {
      line += String((const char*)standingData["Constructor"]["name"]);
    } else {
      line += String((const char*)standingData["Driver"]["code"]);
    }

    line += " ";
    int wins = (int)standingData["wins"];  //saving space by only showing more then 0 wins
    if (wins > 0) {
      line += String(wins) + "w ";
    }
    line += String((const char*)standingData["points"]) + "pts";

    bodyLines.push_back(line);
  }
}

// for just getting round to know if data needs re-fetching
int fetchCurrentRound(const char* url) {
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("HTTP fetch failed in round check");
    http.end();
    return -1;
  }

  String json = http.getString();
  DynamicJsonDocument doc(255);  // small size just for round/season
  DeserializationError error = deserializeJson(doc, json);
  http.end();

  if (error) {
    Serial.println("Round check JSON error");
    return -1;
  }

  int round = (int)doc["MRData"]["StandingsTable"]["round"];
  return round;
}


// for cleaning up characters not recognised by lcd 1602
String sanitizeForLCD(String input) {
  input.replace("ü", "u");
  input.replace("Ü", "U");
  input.replace("ö", "o");
  input.replace("Ö", "O");
  input.replace("ä", "a");
  input.replace("Ä", "A");
  input.replace("ã", "a");
  input.replace("Ã", "A");
  input.replace("ñ", "n");
  input.replace("Ñ", "N");
  input.replace("é", "e");
  input.replace("É", "E");
  input.replace("è", "e");
  input.replace("È", "E");
  input.replace("á", "a");
  input.replace("Á", "A");
  input.replace("à", "a");
  input.replace("À", "A");
  input.replace("ê", "e");
  input.replace("Ê", "E");
  input.replace("ô", "o");
  input.replace("Ô", "O");
  input.replace("í", "i");
  input.replace("Í", "I");
  input.replace("ó", "o");
  input.replace("Ó", "O");
  input.replace("ç", "c");
  input.replace("Ç", "C");

  return input;
}

// for setting upcomming race time to local - VILNIUS
time_t parseUtcToLocal(const char* dateStr, const char* timeStr) {
  struct tm tm = {};
  strptime(String(dateStr + String("T") + timeStr).c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
  tm.tm_isdst = -1;

  setenv("TZ", "UTC0", 1);
  tzset();
  time_t t = mktime(&tm);

  setenv("TZ", time_zone, 1);
  tzset();
  return t;
}

// setting up wifi and taking in custom season year
void setupWiFiAndSeason() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  // Custom season year field
  WiFiManagerParameter seasonYearParam("season", "Enter F1 season year", "", 5);
  wm.addParameter(&seasonYearParam);

  bool forcePortal = false;

  if (digitalRead(BLUE_BUTTON) == LOW) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Resetting WiFi...");
    delay(1500);
    if (digitalRead(BLUE_BUTTON) == LOW) {
      wm.resetSettings();
      forcePortal = true;
    }
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Checking WiFi...");

  if (!forcePortal) {
    WiFi.begin();
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 8000) delay(500);
  }

  // If not connected, launch portal
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connect WiFi to:");
    lcd.setCursor(0, 1);
    lcd.print(nameForAP);
    wm.startConfigPortal(nameForAP);
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
  const char* yearStr = seasonYearParam.getValue();
  configTzTime(time_zone, ntpServer1, ntpServer2);
  delay(1000);

  if (yearStr != nullptr && strlen(yearStr) > 0) {
    int year = atoi(yearStr);
    if (year >= 1950 && year <= 2125) { 
      ChosenSeasonYear = year; 
    }
  }
}

// for getting button input for switching stages
void handleButtonPress() {
  static bool lastButtonState = HIGH;
  static unsigned long lastDebounceTime = 0;
  static unsigned long buttonDownTime = 0;
  static bool buttonHeld = false;
  static bool buttonWasPressed = false;
  const unsigned long debounceDelay = 80;
  const unsigned long longPressTime = 2000;

  bool reading = digitalRead(BLUE_BUTTON);

  // If the button state changed (due to noise or press), reset debounce timer
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
    lastButtonState = reading;
  }

  // Only proceed if the button state is stable for debounceDelay
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW) {
      // Button is being held
      if (!buttonWasPressed) {
        buttonDownTime = millis();
        buttonWasPressed = true;
        buttonHeld = false;
      } else if (!buttonHeld && millis() - buttonDownTime >= longPressTime) {
        toggleNightMode();  // Trigger night mode immediately
        buttonHeld = true;
      }
    } else {
      // Button released
      if (buttonWasPressed && !buttonHeld) {
        advanceStage();  // Only trigger short press if it wasn’t already a long press
      }
      buttonWasPressed = false;
      buttonHeld = false;
    }
  }
}


// for toggle between night and day mode
void toggleNightMode() {
  isNightMode = !isNightMode;
  if (isNightMode) {
    lcd.noBacklight();
    Serial.println("Night mode ON");
  } else {
    lcd.backlight();
    Serial.println("Night mode OFF");
  }
}

// for cheching for night mode
void checkTimeBasedNightMode() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int hour = timeinfo.tm_hour;

    bool shouldBeNight = (hour >= 23 || hour < 6);

    if (shouldBeNight && !isNightMode) {
      lcd.noBacklight();
      isNightMode = true;
    } else if (!shouldBeNight && isNightMode) {
      lcd.backlight();
      isNightMode = false;
    }
  }
}

// for automaticly switching stages and notification calling
void autoAdvanceStage() {
  time_t now = time(NULL);
  const time_t fiveMinutes = 5 * 60;
  const time_t oneHour = 60 * 60;

  // --- Notification triggers ---
  if (!raceNotified && raceStart - now <= fiveMinutes && raceStart - now > 0) {
    displayStage = STAGE_NOTIFICATION;
    notificationMsg = " Race in 5 min! ";
    raceNotified = true;
    return;
  }

  if (!qualiNotified && qualiStart - now <= fiveMinutes && qualiStart - now > 0) {
    displayStage = STAGE_NOTIFICATION;
    notificationMsg = " Quali in 5 min ";
    qualiNotified = true;
    return;
  }

  if (!predictionNotified && qualiStart - now <= oneHour && qualiStart - now > 55 * 60) {
    displayStage = STAGE_NOTIFICATION;
    notificationMsg = "Make Predictions";
    predictionNotified = true;
    return;
  }

  if (now - raceStart > 60) {
    raceNotified = false;
    qualiNotified = false;
    predictionNotified = false;
  }

  // for switching stages from clock every 15 min
  if (millis() - lastStageChange > stageInterval && displayStage == STAGE_CLOCK) {
    advanceStage();
  }
}

// for moving to the next stage
void advanceStage() {
  lastStageChange = millis();
  switch (displayStage) {
    case STAGE_CLOCK: displayStage = STAGE_CONSTRUCTORS; break;
    case STAGE_CONSTRUCTORS: displayStage = STAGE_DRIVERS; break;
    case STAGE_DRIVERS: displayStage = STAGE_CALENDAR; break;
    case STAGE_CALENDAR: displayStage = STAGE_CLOCK; break;
    case STAGE_NOTIFICATION: displayStage = STAGE_CLOCK; break;
    default: displayStage = STAGE_CLOCK; break;
  }
  resetStageState();
}

// for reseting values to default
void resetStageState() {
  constructorScroll.scrollIndex = 0;
  constructorScroll.state = 0;
  constructorScroll.lastScroll = millis();

  driverScroll.scrollIndex = 0;
  driverScroll.state = 0;
  driverScroll.lastScroll = millis();

  calendarScroll.scrollIndex = 0;
  calendarScroll.state = 0;
  calendarScroll.lastScroll = millis();
  calendarFirstEntry = true;
  calendarScrolling = false;
  calendarDoneScrolling = false;
  calendarStepStartTime = millis();

  buzzerActive = false;
  lastDateStr = "";
  lcd.clear();
}

// for displaying error message on race calendar stage
void renderRaceError() {
  static unsigned long errorStartTime = 0;

  if (errorStartTime == 0) {
    errorStartTime = millis();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Race info error");
    lcd.setCursor(0, 1);
    lcd.print(raceErrorMsg.substring(0, 16));
  }

  if (millis() - errorStartTime > 7000) {
    errorStartTime = 0;
    advanceStage();
  }
}

// for displaying current time and date
void renderClockStage() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    time_t now = time(nullptr);
    struct tm timeinfo;
    char dateBuf[17], timeBuf[17];

    if (localtime_r(&now, &timeinfo)) {
      strftime(dateBuf, sizeof(dateBuf), "%a %b %d", &timeinfo);
      strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);

      String dateStr = String(dateBuf);
      String timeStr = String(timeBuf);

      if (dateStr != lastDateStr) {
        checkTimeBasedNightMode();
        lastDateStr = dateStr;
        lcd.setCursor(0, 0);
        lcd.print("                ");
        int datePadding = (16 - dateStr.length()) / 2;
        lcd.setCursor(datePadding, 0);
        lcd.print(dateStr);
      }
      lcd.setCursor(0, 1);
      lcd.print("                ");
      int timePadding = (16 - timeStr.length()) / 2;
      lcd.setCursor(timePadding, 1);
      lcd.print(timeStr);
      
    } else {
      lcd.setCursor(0, 0);
      lcd.print(" Time not set   ");
      lcd.setCursor(0, 1);
      lcd.print("                ");
    }
  }
}

// for displaying constructors and drivers data scrolling
void renderScrollingStage(const String& header1, const String& header2, std::vector<String>* bodyLines, ScrollState* scroll) {
  static unsigned long showHeaderUntil = 0;

  const char fillChar = (char)255;
  String carSymbol = String((char)1) + String((char)2) + String((char)3) + String((char)4);

  // Create scrolling ticker strings
  String topTicker = "            ";
  String bottomTicker = "                    ";
  for (size_t i = 0; i < bodyLines->size(); ++i) {
    if (i % 2 == 0)
      topTicker += "    " + carSymbol + "  " + bodyLines->at(i);
    else
      bottomTicker += "    " + carSymbol + "  " + bodyLines->at(i);
  }

  if (scroll->state == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(header1);
    lcd.setCursor(0, 1);
    lcd.print(header2);

    showHeaderUntil = millis() + 3000;
    scroll->state = 1;
    scroll->scrollIndex = 0;
  }

  if (millis() < showHeaderUntil) {
    return;
  }

  if (scroll->state == 1) {
    if (millis() - scroll->lastScroll > scroll->scrollSpeed) {
      scroll->lastScroll = millis();

      int topLen = topTicker.length();
      int botLen = bottomTicker.length();
      int maxLen = max(topLen, botLen);

      lcd.setCursor(0, 0);
      String top = (scroll->scrollIndex < topLen) ? topTicker.substring(scroll->scrollIndex) : "";
      while (top.length() < 16) top += ' ';
      lcd.print(top.substring(0, 16));

      lcd.setCursor(0, 1);
      String bot = (scroll->scrollIndex < botLen) ? bottomTicker.substring(scroll->scrollIndex) : "";
      while (bot.length() < 16) bot += ' ';
      lcd.print(bot.substring(0, 16));

      scroll->scrollIndex++;
      if (scroll->scrollIndex > maxLen) {
        advanceStage();
        lcd.clear();
      }
    }
  }
}

// for setting up constructors data
void renderConstructorsStage() {
  const char fillChar = (char)255;
  const String spacer = String(fillChar) + String(fillChar) + String(fillChar);
  String header1 = spacer + " F1 W C C " + spacer;
  String header2 = String(seasonYear) + " Race:" + String(currentRound) + "/" + String(totalRounds);

  renderScrollingStage(header1, header2, &constructorLines, &constructorScroll);
}

// for setting up drivers data
void renderDriversStage() {
  const char fillChar = (char)255;
  const String spacer = String(fillChar) + String(fillChar) + String(fillChar);
  String header1 = spacer + " F1 W D C " + spacer;
  String header2 = String(seasonYear) + " Race:" + String(currentRound) + "/" + String(totalRounds);

  renderScrollingStage(header1, header2, &driverLines, &driverScroll);
}

// for displaying upcoming race data
void renderCalendarStage() {
  const unsigned long initialHold = 2000;
  const unsigned long timeDisplayDuration = 5000;

  if (raceErrorMsg != "") {
    renderRaceError();
    return;
  }

  time_t now = time(nullptr);

  struct tm nowTm;
  localtime_r(&now, &nowTm);
  nowTm.tm_hour = 0;
  nowTm.tm_min = 0;
  nowTm.tm_sec = 0;
  time_t todayMidnight = mktime(&nowTm);

  struct tm raceTm;
  localtime_r(&raceStart, &raceTm);
  raceTm.tm_hour = 0;
  raceTm.tm_min = 0;
  raceTm.tm_sec = 0;
  time_t raceMidnight = mktime(&raceTm);

  long dayDiff = (raceMidnight - todayMidnight) / 86400;  // seconds in a day

  String whenStr;
  if (dayDiff < 0) {
    whenStr = "Passed";
  } else if (dayDiff == 0) {
    whenStr = "Today";
  } else if (dayDiff == 1) {
    whenStr = "Tomorrow";
  } else {
    whenStr = String(dayDiff) + " days";
  }

  if (calendarScroll.state == 0) {
    if (calendarFirstEntry) {
      lcd.clear();
      calendarScroll.scrollIndex = 0;
      calendarScrolling = false;
      calendarDoneScrolling = false;
      calendarStepStartTime = millis();
      calendarFirstEntry = false;
    }

    lcd.setCursor(0, 0);
    lcd.print("Next race: ");
    lcd.print(String(upcomingRound) + "/" + String(totalRounds));

    String Rname = sanitizeForLCD(raceName) + "  |  " + whenStr;
    int maxScroll = max(0, (int)Rname.length() - 16);

    if (!calendarScrolling && millis() - calendarStepStartTime < initialHold) {
      lcd.setCursor(0, 1);
      lcd.print(Rname.substring(0, 16));
      return;
    }

    if (!calendarScrolling) {
      calendarScrolling = true;
      calendarStepStartTime = millis();
    }

    if (calendarScrolling && !calendarDoneScrolling && millis() - calendarStepStartTime >= calendarScroll.scrollSpeed) {
      calendarStepStartTime = millis();
      lcd.setCursor(0, 1);
      lcd.print(Rname.substring(calendarScroll.scrollIndex, calendarScroll.scrollIndex + 16));
      calendarScroll.scrollIndex++;

      if (calendarScroll.scrollIndex > maxScroll) {
        calendarDoneScrolling = true;
        calendarStepStartTime = millis();
      }
      return;
    }

    if (calendarDoneScrolling && millis() - calendarStepStartTime < initialHold) {
      lcd.setCursor(0, 1);
      lcd.print(Rname.substring(maxScroll, maxScroll + 16));
      return;
    }

    if (calendarDoneScrolling && millis() - calendarStepStartTime >= initialHold) {
      lcd.clear();
      calendarScroll.state = 1;
      calendarFirstEntry = true;
      return;
    }
  }

  if (calendarScroll.state == 1) {
    if (calendarFirstEntry) {
      calendarStepStartTime = millis();
      lcd.clear();
      calendarFirstEntry = false;
    }

    struct tm raceLocal, qualiLocal;
    localtime_r(&raceStart, &raceLocal);
    localtime_r(&qualiStart, &qualiLocal);

    char raceTimeStr[17], qualiTimeStr[17];
    strftime(raceTimeStr, sizeof(raceTimeStr), "R: %b %d %H:%M", &raceLocal);
    strftime(qualiTimeStr, sizeof(qualiTimeStr), "Q: %b %d %H:%M", &qualiLocal);

    lcd.setCursor(0, 0);
    lcd.print(raceTimeStr);
    lcd.setCursor(0, 1);
    lcd.print(qualiTimeStr);

    if (millis() - calendarStepStartTime >= timeDisplayDuration) {
      calendarScroll.state = 0;
      calendarFirstEntry = true;
      advanceStage();
    }
  }
}

// for setting up audio buzzer
void startBuzzer() {
  buzzerIndex = 0;
  buzzerStartTime = millis();
  buzzerActive = true;
  pinMode(BUZZER_PIN, OUTPUT);
}

// for playing audio in notification stage
void updateBuzzer() {
  if (!buzzerActive) return;

  const int NUM_NOTES = sizeof(melody) / sizeof(melody[0]);
  unsigned long now = millis();

  if (buzzerIndex >= NUM_NOTES) {
    buzzerActive = false;
    buzzerState = BUZZER_IDLE;
    noTone(BUZZER_PIN);
    return;
  }

  int noteDuration = 1100 / durations[buzzerIndex];
  int gap = 20;  // Padding between notes

  switch (buzzerState) {
    case BUZZER_IDLE:
      buzzerStartTime = now;
      buzzerState = BUZZER_PLAYING_NOTE;
      // Fallthrough to start note immediately

    case BUZZER_PLAYING_NOTE:
      if (melody[buzzerIndex] > 0) {
        tone(BUZZER_PIN, melody[buzzerIndex], noteDuration);
      }
      buzzerStartTime = now;
      buzzerState = BUZZER_RESTING;
      break;

    case BUZZER_RESTING:
      if (now - buzzerStartTime >= noteDuration + gap) {
        noTone(BUZZER_PIN);
        buzzerIndex++;
        buzzerState = BUZZER_IDLE;
      }
      break;
  }
}

// for dispalying notifiaction stage
void renderNotificationStage() {
  static unsigned long startTime = 0;
  static unsigned long lastScrollTime = 0;
  static unsigned long lastBlinkTime = 0;
  static int scrollIndex = 0;
  static bool textVisible = true;

  const unsigned long duration = 30000;
  const unsigned long scrollInterval = 200;
  const unsigned long blinkInterval = 500;

  String carSymbol = String((char)1) + String((char)2) + String((char)3) + String((char)4);
  String carLine = "";

  while (carLine.length() < 48) {
    carLine += carSymbol + "    ";
  }

  if (startTime == 0) {
    startTime = millis();
    lastScrollTime = millis();
    lastBlinkTime = millis();
    scrollIndex = 0;
    textVisible = true;
    lcd.clear();
    startBuzzer();
  }

  updateBuzzer();

  unsigned long now = millis();

  if (now - startTime > duration) {
    startTime = 0;
    notificationMsg = "";
    advanceStage();
    return;
  }

  if (now - lastScrollTime >= scrollInterval) {
    lcd.setCursor(0, 0);
    lcd.print(carLine.substring(scrollIndex, scrollIndex + 16));
    scrollIndex++;
    if (scrollIndex > carLine.length() - 16) {
      scrollIndex = 0;
    }
    lastScrollTime = now;
  }

  if (now - lastBlinkTime >= blinkInterval) {
    lcd.setCursor(0, 1);
    if (textVisible) {
      lcd.print("                ");
    } else {
      lcd.print(notificationMsg.substring(0, 16));
    }
    textVisible = !textVisible;
    lastBlinkTime = now;
  }
  updateBuzzer();
}

// for rendering current stage
void renderCurrentStage() {
  switch (displayStage) {
    case STAGE_CLOCK:
      renderClockStage();
      break;
    case STAGE_CONSTRUCTORS:
      renderConstructorsStage();
      break;
    case STAGE_DRIVERS:
      renderDriversStage();
      break;
    case STAGE_CALENDAR:
      renderCalendarStage();
      break;
    case STAGE_NOTIFICATION:
      renderNotificationStage();
      break;
  }
}

// for getting all data from ergast APIs
void refreshAllData() {

  String constructorURL = "";
  String driverURL = "";
  String calendarURL = "";

  if (ChosenSeasonYear > 0) {
    constructorURL = "https://api.jolpi.ca/ergast/f1/" + String(ChosenSeasonYear) + "/constructorstandings/?format=json";
    driverURL = "https://api.jolpi.ca/ergast/f1/" + String(ChosenSeasonYear) + "/driverstandings/?format=json";
    // for race calendar it probaly does not makes sense to do this way..
    calendarURL = "https://api.jolpi.ca/ergast/f1/" + String(ChosenSeasonYear) + "/races/?format=json";
  } else {
    constructorURL = "https://api.jolpi.ca/ergast/f1/current/constructorstandings/?format=json";
    driverURL = "https://api.jolpi.ca/ergast/f1/current/driverstandings/?format=json";
    calendarURL = "https://api.jolpi.ca/ergast/f1/current/races/?format=json";
  }


  fetchAndFormatStandings(constructorURL.c_str(), true, constructorLines);
  delay(1000);

  fetchAndFormatStandings(driverURL.c_str(), false, driverLines);
  delay(1000);

  fetchUpcomingRace(calendarURL.c_str());

  waitingForNextData = false;
  if (upcomingRound > 0) {
    setNextCheckAfterRace();
  }
}

// for schedulling first next check
void setNextCheckAfterRace() {
  nextDataCheckTime = raceStart + 4 * 60 * 60;
  waitingForNextData = true;
  Serial.print("First standings check scheduled at: ");
  Serial.println(ctime(&nextDataCheckTime));
}

// for checking if there is newer data
void checkForNewDataIfTime() {
  time_t now = time(nullptr);
  if (!waitingForNextData || now < nextDataCheckTime) return;

  String driverURL = "https://api.jolpi.ca/ergast/f1/current/driverstandings/?format=json";
  int fetchedRound = fetchCurrentRound(driverURL.c_str());
  if (fetchedRound == -1) {
    Serial.println("Failed to fetch. Try again in 10 min.");
    nextDataCheckTime = now + 10 * 60;
    return;
  }

  if (fetchedRound > currentRound) {
    Serial.println("New data available!");
    refreshAllData();
  } else {
    Serial.println("No update yet. Retry in 1h.");
    nextDataCheckTime = now + 1 * 60 * 60;
  }
}

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  // creating custom characters for Formula1 car emoji
  lcd.createChar(1, formula1_4);
  lcd.createChar(2, formula2_4);
  lcd.createChar(3, formula3_4);
  lcd.createChar(4, formula4_4);

  pinMode(BLUE_BUTTON, INPUT_PULLUP);

  // Connect and get season year
  setupWiFiAndSeason();

  refreshAllData();

  Serial.println("Constructor Standings:");
  for (String line : constructorLines) Serial.println(line);

  Serial.println("Driver Standings:");
  for (String line : driverLines) Serial.println(line);
}

void loop() {
  handleButtonPress();
  autoAdvanceStage();
  renderCurrentStage();

  checkForNewDataIfTime();
}