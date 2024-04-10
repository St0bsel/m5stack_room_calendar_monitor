// benötigte Librarys einbinden
#include <WiFi.h>
#include <HTTPClient.h>
#include <M5Stack.h>
#include <Base64.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// W-LAN Verbindung definieren
const char* ssid = "";
const char* password = "";

// CalDav Server definieren
const String caldavServer = "";
const String caldavUser = "";
const String caldavPass = "";
const String caldavCalendarName = "personal";
const String caldavRequestURL = caldavServer + "/remote.php/dav/calendars/" + caldavUser + "/" + caldavCalendarName;

// Zeitvariablen definieren und initialisieren
int currentYear = 0;
int currentMonth = 0;
int currentDay = 0;
int currentHour = 0;
int currentMinute = 0;

// Struktur für Eventinformationen
struct EventInfo {
    String summary;
    String dtstart;
    String dtend;
    int startYear;
    int startMonth;
    int startDay;
    int startHour;
    int startMinute;
    int endYear;
    int endMonth;
    int endDay;
    int endHour;
    int endMinute;
    int isHappening;
};

bool eventCurrentlyHappening = false;

EventInfo parsedEvents[100];

// muss aktuell noch manuell bei der Umstellung von Sommer- auf Winterzeit definiert werden
int summerTimeTrue = 1;

String dateDisplayCurrent = "";

// auf true setzen, um Debug-Ausgaben zu erhalten
bool debug = false;

// select the input pin for the potentiometer
int sensorPin = 36;
// last variable to store the value coming from the sensor
int last_sensorValue = 0;
// current variable to store the value coming from the sensor
int cur_sensorValue = 0;

unsigned long startTime = millis(); // Store the start time
unsigned long runTime = 30000; // Duration for which the loop should run, in milliseconds
bool firstRun = true;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ch.pool.ntp.org", 3600, 60000);

// diese Funktion wird beim Start des M5 ausge3führt
void setup() {
  // void begin(bool LCDEnable=true, bool SDEnable=true, bool SerialEnable=true,bool I2CEnable=false)
  M5.begin(true, false, true, true); 
  M5.Power.begin();
  pinMode(sensorPin, INPUT);
  // LCD Einstellungen vornehmen
  M5.Lcd.setTextSize(4);
  M5.Lcd.sleep();
  M5.Lcd.setBrightness(0);
  // Serial verbindung mit einer Geschwindigkeit von 115200 starten
  Serial.begin(115200);
  // eine Sekunde warten
  delay(1000);
  // Funktion zum Aufbau der W-LAN Verbindung aufrufen
  connectWiFi();
  delay(1000);
  // Funktion zum Abfragen der aktuellen Zeit aufrufen
  timeClient.begin();
  getDate();
  // Angaben ausgeben
  printInfo();
  
  M5.Lcd.wakeup();
  M5.Lcd.setBrightness(255);
}

void loop() {

  // only run this code every x seconds
  if ((millis() - startTime) > runTime || firstRun) {
    eventCurrentlyHappening = false;
    for (const auto& eventInfo : parsedEvents) {
      int isHappening = eventHappening(eventInfo);
      if (isHappening != 0 && isHappening != 2) { // event is currently happening
        eventCurrentlyHappening = true;
      }
    }

    // Datum vorbereiten
    // Ensure that month and day are two digits
    String formattedMonth = currentMonth < 10 ? "0" + String(currentMonth) : String(currentMonth);
    String formattedDay = currentDay < 10 ? "0" + String(currentDay) : String(currentDay);
    int nextDay = currentDay + 1;
    String formattedNextDay = nextDay < 10 ? "0" + String(nextDay) : String(nextDay);


    // Concatenate year, month, and day to form the date string
    String startDate = String(currentYear) + formattedMonth + formattedDay;
    String endDate = String(currentYear) + formattedMonth + formattedNextDay;

    String bodyString = "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
                        "<d:prop>"
                          "<d:getetag />"
                          "<c:calendar-data />"
                        "</d:prop>"
                        "<c:filter>"
                          "<c:comp-filter name=\"VCALENDAR\">"
                            "<c:comp-filter name=\"VEVENT\">"
                              "<c:time-range start=\"" + startDate + "T000000Z\" end=\"" + endDate + "T000000Z\"/>"
                            "</c:comp-filter>"
                          "</c:comp-filter>"
                        "</c:filter>"
                      "</c:calendar-query>";

    String response = makeRequest("REPORT", "text/xml; charset=utf-8", "1", bodyString, caldavUser, caldavPass, caldavRequestURL);

    // Convert Arduino String to std::string
    std::string xmlData = response.c_str();

    std::vector<std::string> events;
    size_t startPos = 0;
    while ((startPos = xmlData.find("BEGIN:VEVENT", startPos)) != std::string::npos) {
        size_t endPos = xmlData.find("END:VEVENT", startPos);
        if (endPos == std::string::npos) break; // Malformed XML
        endPos += std::string("END:VEVENT").length(); // Include the tag itself

        // Extract the event data
        std::string eventData = xmlData.substr(startPos, endPos - startPos);
        events.push_back(eventData);

        // Move past this event for the next iteration
        startPos = endPos;
    }

    sortEventsByDTSTART(events);

    eventCurrentlyHappening = false;
    int eventCount = 0; // Keep track of the number of stored events
    // Iterate through each event and parse it
    for (const auto& event : events) {
      if (eventCount >= 100) { // Array is full
        break;
      }
      EventInfo eventInfo = parseVEVENT(String(event.c_str()));
      if (debug){
        Serial.println(eventInfo.summary);
        Serial.println(eventInfo.dtstart);
        Serial.println(eventInfo.dtend);
        Serial.println(eventInfo.isHappening);
      }
    
      parsedEvents[eventCount++] = eventInfo; // Store the parsed event
      if (eventInfo.isHappening != 0 && eventInfo.isHappening != 2) { // event is currently happening
        eventCurrentlyHappening = true;
      }
    }

    M5.Lcd.clear();
    M5.Lcd.setTextSize(6);
    
    if (eventCurrentlyHappening) {
      M5.Lcd.setCursor(35, 20);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
      M5.Lcd.fillScreen(TFT_RED);
      M5.Lcd.print("besetzt");
    }
    else {
      M5.Lcd.setCursor(65, 20);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_GREEN);
      M5.Lcd.fillScreen(TFT_GREEN);
      M5.Lcd.print("frei");
    }

    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 100);
    for (EventInfo eventInfo : parsedEvents){
      if (eventInfo.isHappening == 1 || eventInfo.isHappening == 2) {
        M5.Lcd.println(addZeroIfBelowTen(eventInfo.startDay) + "." + addZeroIfBelowTen(eventInfo.startMonth) + "." + 
                     addZeroIfBelowTen(eventInfo.startYear) + " " + addZeroIfBelowTen(eventInfo.startHour) + ":" + addZeroIfBelowTen(eventInfo.startMinute)
                     + " - " + addZeroIfBelowTen(eventInfo.endHour) + ":" + addZeroIfBelowTen(eventInfo.endMinute));
        M5.Lcd.println(eventInfo.summary);
      }
    }
    startTime = millis();
    firstRun = false;
  }

  M5.update();
  if (M5.BtnA.wasPressed()){
    Serial.println("Button A pressed");
    M5.Lcd.clear();
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.fillScreen(TFT_BLACK);
    if (eventCurrentlyHappening){
      M5.Lcd.print("Termin hinzufuegen\nnicht moeglich\nRaum besetzt");
      delay(300);
    }
    else {
      M5.Lcd.print("Termin\nreservieren");
      M5.Lcd.setCursor(10, 100);
      M5.Lcd.setTextSize(2);
      M5.Lcd.print("Zeit auswählen: ");
      M5.update();
      while (!M5.BtnA.wasPressed()){
        M5.Lcd.setCursor(200, 100);
        M5.Lcd.print("           ");
        M5.Lcd.setCursor(200, 100);
        cur_sensorValue = analogRead(sensorPin);
        cur_sensorValue = mapValue(cur_sensorValue);
        M5.Lcd.print(cur_sensorValue);
        M5.Lcd.print(" min");
        if (debug){
          Serial.println(mapValue(cur_sensorValue));
        }
        M5.update();
      }
      
      // Zeit und Datum aktualisieren
      getDate();

      // Startzeit definieren
      char dtstart[16];
      sprintf(dtstart, "%04d%02d%02dT%02d%02d00", currentYear, currentMonth, currentDay, currentHour, currentMinute);
      // aktuelle Sensorwerte abfragen
      int additionalMinutes = mapValue(cur_sensorValue);
      // Endzeit Minuten definieren
      int combinedMinutes = additionalMinutes + currentMinute;
      int combinedHours = currentHour;

      // Überprüfen, ob die Minuten größer als 60 sind
      // und die Stunden entsprechend anpassen
      while (combinedMinutes >= 60) {
          combinedMinutes -= 60;
          combinedHours++;
      };

      // Überprüfen, ob die Stunden größer als 24 sind
      // und die Tage entsprechend anpassen
      int combinedDays = currentDay;
      if (combinedHours >= 24) {
          combinedHours = 0;
          combinedDays;
      };

      Serial.println("Endzeit: " + String(combinedHours) + ":" + String(combinedMinutes) + " Uhr");

      // Endzeit definieren
      char dtend[16];
      sprintf(dtend, "%04d%02d%02dT%02d%02d00", currentYear, currentMonth, combinedDays, combinedHours, combinedMinutes);

      String uid = String(dtstart)+String(random(1000, 9999))+".ics";
      String url = caldavRequestURL + "/" + uid;
      if (debug){
        Serial.println(dtstart);
        Serial.println(dtend);
      }

      String bodyString = "BEGIN:VCALENDAR\n"
                    "VERSION:2.0\n"
                    "BEGIN:VEVENT\n"
                    "UID:"+uid+"\n"
                    "DTSTART;TZID=Europe/Zurich:"+String(dtstart)+"\n"
                    "DTEND;TZID=Europe/Zurich:"+String(dtend)+"\n"
                    "SUMMARY:booked by m5stack\n"
                    "DESCRIPTION:this event was booked with the m5stack UID: "+uid+"\n"
                    "LOCATION:m5stack meeting-room\n"
                    "END:VEVENT\n"
                    "END:VCALENDAR";

      if (debug){
        Serial.println(bodyString);
      }
      String response2 = makeRequest("PUT", "text/calendar; charset=utf-8", "1", bodyString, caldavUser, caldavPass, url);
      
      M5.update();
      delay(20);
    }
    firstRun = true;
  }
  delay(20);  
}

// Funktion zur W-LAN Verbindung
void connectWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
};

// Funktion, um während dem Setup allgemeine Infos auszugeben
void printInfo() {
  if (debug){
    Serial.println("RequestUrl: " + caldavRequestURL);
    Serial.print("Datum: ");
    Serial.print(currentYear);
    Serial.print("-");
    Serial.print(currentMonth);
    Serial.print("-");
    Serial.print(currentDay);
    Serial.print(" ");
    Serial.print(currentHour);
    Serial.print(":");
    Serial.println(currentMinute);
  }
};

void getEvents(String startDate, String startTime, String endDate, String endTime) {

};

String makeRequest(char* requestType, char* contentType, char* Depth, String bodyString, String username, String password, String caldavRequestURL) {
  
  HTTPClient http;

  http.begin(caldavRequestURL); // Specify the URL
  http.addHeader("Content-Type", contentType);
  http.addHeader("Depth", Depth); // Depth header: 0, 1, or infinity

  if (username && password){
    String authValue = String(username) + ":" + String(password);
    authValue = base64::encode(authValue); // Encode credentials to Base64
    http.addHeader("Authorization", "Basic " + authValue); // Add the Authorization header
  }

  // Make the request
  int httpCode = http.sendRequest(requestType, bodyString);

  // Check the returning http response code
  if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("HTTP Code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == 207 || httpCode == 201 || httpCode == 204 || httpCode == 200) {
          String payload = http.getString();
          if (debug){
            Serial.println(payload);
          }
          
          return payload;
      }
  } else {
      Serial.printf("PROPFIND failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end(); //Close connection
};

void getDate() {
  timeClient.update();
  // Get the current date
  unsigned long epochTime = timeClient.getEpochTime();
  //Serial.println(epochTime);
  struct tm *ptm = gmtime((time_t *)&epochTime); 

  currentYear = ptm->tm_year+1900; // Year is # years since 1900
  currentMonth = ptm->tm_mon+1; // Month range is 0-11
  currentDay = ptm->tm_mday; // Day of month
  currentHour = ptm->tm_hour+summerTimeTrue; // Hours since midnight - [0,23]
  currentMinute = ptm->tm_min; // Minutes after the hour - [0,59]
}

EventInfo parseVEVENT(const String& vevent) {
  EventInfo eventInfo;
  eventInfo.summary = extractData(vevent, "SUMMARY:", "\n");
  eventInfo.dtstart = extractData(vevent, "DTSTART", "\n");
  eventInfo.dtend = extractData(vevent, "DTEND", "\n");

  // Extract the datetime part after the timezone information
  int startIndex = eventInfo.dtstart.lastIndexOf(':') + 1; // Find the last occurrence of ':' and move one character ahead
  String startDate = eventInfo.dtstart.substring(startIndex); // "20240401T130000"
  
  startIndex = eventInfo.dtend.lastIndexOf(':') + 1;
  String endDate = eventInfo.dtend.substring(startIndex); // "20240401T140000"
  
  // Now parse the datetime strings as before
  eventInfo.startYear = startDate.substring(0, 4).toInt();
  eventInfo.startMonth = startDate.substring(4, 6).toInt();
  eventInfo.startDay = startDate.substring(6, 8).toInt();
  eventInfo.startHour = startDate.substring(9, 11).toInt();
  eventInfo.startMinute = startDate.substring(11, 13).toInt();

  eventInfo.endYear = endDate.substring(0, 4).toInt();
  eventInfo.endMonth = endDate.substring(4, 6).toInt();
  eventInfo.endDay = endDate.substring(6, 8).toInt();
  eventInfo.endHour = endDate.substring(9, 11).toInt();
  eventInfo.endMinute = endDate.substring(11, 13).toInt();

  eventInfo.isHappening = eventHappening(eventInfo);

  return eventInfo;
}

String extractData(const String& source, const String& startDelimiter, const String& endDelimiter) {
    int start = source.indexOf(startDelimiter) + startDelimiter.length();
    if (start == startDelimiter.length() - 1) return ""; // startDelimiter not found

    int end = source.indexOf(endDelimiter, start);
    if (end == -1) return ""; // endDelimiter not found

    String result = source.substring(start, end); // Extract the substring
    result.trim(); // Trim whitespace from the result string
    return result; // Return the trimmed string
}

int eventHappening(EventInfo eventInfo) {
  
  // Update the current date and time
  getDate(); // This will update global variables with the current date and time

  // Update comparison logic to include minutes for 'isCurrent'
  bool isCurrent = (currentYear == eventInfo.startYear && currentMonth == eventInfo.startMonth &&
                    currentDay == eventInfo.startDay && (currentHour > eventInfo.startHour || 
                    (currentHour == eventInfo.startHour && currentMinute >= eventInfo.startMinute)) &&
                    (currentHour < eventInfo.endHour || 
                    (currentHour == eventInfo.endHour && currentMinute <= eventInfo.endMinute)));

  // This logic checks if the current date-time is before the event start date-time
  bool isUpcoming = (currentYear < eventInfo.endYear || 
                    (currentYear == eventInfo.endYear && currentMonth < eventInfo.endMonth) || 
                    (currentYear == eventInfo.endYear && currentMonth == eventInfo.endMonth && currentDay < eventInfo.endDay) ||
                    (currentYear == eventInfo.endYear && currentMonth == eventInfo.endMonth && currentDay == eventInfo.endDay && 
                    (currentHour < eventInfo.endHour || (currentHour == eventInfo.endHour && currentMinute < eventInfo.endMinute))));

  // Set color based on event status
  if (isCurrent) {
      return 1;
  } else if (isUpcoming) {
      return 2;
  } else {
      return 0;
  }

}

// Helper function to extract DTSTART in a sortable format (YYYYMMDDHHMMSS)
std::string extractSortableDTSTART(const std::string& event) {
    size_t dtstartPos = event.find("DTSTART");
    size_t colonPos = event.find(":", dtstartPos);
    if (colonPos != std::string::npos) {
        size_t endPos = event.find("\n", colonPos);
        std::string dtstart = event.substr(colonPos + 1, endPos - colonPos - 1);
        // Remove any 'T' and '-' characters for direct comparison
        dtstart.erase(remove(dtstart.begin(), dtstart.end(), 'T'), dtstart.end());
        dtstart.erase(remove(dtstart.begin(), dtstart.end(), '-'), dtstart.end());
        return dtstart;
    }
    return "";
}

// Custom comparison function for sorting events by DTSTART
bool compareByDTSTART(const std::string& event1, const std::string& event2) {
    std::string dtstart1 = extractSortableDTSTART(event1);
    std::string dtstart2 = extractSortableDTSTART(event2);
    return dtstart1 < dtstart2;
}

void sortEventsByDTSTART(std::vector<std::string>& events) {
    std::sort(events.begin(), events.end(), compareByDTSTART);
}

int mapValue(int inputValue) {
  // Stellen Sie sicher, dass der inputValue innerhalb des erwarteten Bereichs liegt
  inputValue = constrain(inputValue, 0, 4096);

  // Map von 0-4096 auf 0-120
  int mappedValue = round((float)(inputValue * 120) / 4096);

  // Runden auf das nächste Vielfache von 5
  mappedValue = round((float)mappedValue / 5) * 5;
  
  if (mappedValue == 0){
    mappedValue = 5;
  }
  
  return mappedValue;
}

String addZeroIfBelowTen(int value) {
  char buffer[5];
  sprintf(buffer, "%02d", value);
  return String(buffer);
}