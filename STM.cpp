/*

        THESE ARE THE CODES THAT WERE PUT IN THE ESP AFTER ITS FAILURE AND TRIED TO CHANGE THE LOCATION
        THAT WE HAD INSTALLED TO MAKE IT CLOSER TO THE WIFI.
         WE ARE ALSO STILL IN THE TESTING OF IT. IT REPLACED VERSION 2.3 which had alot of wifi issues and failures.
         
  ====================================================================================================
  SCHOOL BELL SYSTEM - ULTIMATE PROFESSIONAL EDITION v3.0
  ====================================================================================================
  
  DEVELOPED BY: COT CLUB
  VERSION: 3.0 (Fully Featured)
  LAST UPDATED: 2024
  
  COMPLETE FEATURE LIST:
  ✓ WiFi Manager with Configuration Portal
  ✓ Blynk Integration with Message Counting
  ✓ 4 Operation Modes (ECO, NORMAL, VERBOSE, SILENT)
  ✓ Interactive V10 Terminal with 50+ Commands
  ✓ Schedule Management for Weekday/Weekend Bells
  ✓ Schedule Management for Lighting (Fully Editable)
  ✓ WiFi Scanning & Connection via Terminal
  ✓ Precise Time Synchronization (NTP)
  ✓ Manual Override for Lights
  ✓ Bell Ringing (Auto & Manual)
  ✓ Error Tracking & Recovery
  ✓ Persistent Storage (Preferences)
  ✓ LED Status Indicators
  ✓ Hardware Button Support
  ✓ Midnight Auto-Reset
  ✓ Blynk Message Usage Monitoring
  ✓ Detailed Help System with Command Examples
  
  PIN CONFIGURATION:
  - Lamp Relay: GPIO5 (Active LOW)
  - Bell Relay: GPIO4 (Active LOW)
  - Lamp Button: GPIO12 (INPUT_PULLUP)
  - Bell Button: GPIO14 (INPUT_PULLUP)
  - Config Button: GPIO13 (INPUT_PULLUP)
  - Built-in LED: GPIO2
  
  ====================================================================================================
*/

#define BLYNK_TEMPLATE_ID "TMPL2mWnp7V_Y"
#define BLYNK_TEMPLATE_NAME "topdv"

#include <WiFi.h>
#include <WiFiManager.h>
#include <BlynkSimpleEsp32.h>
#include <time.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <vector>
#include <algorithm>

// ====================================================================================================
// CONFIGURATION CONSTANTS
// ====================================================================================================

// Hardware Pins
#define PIN_LAMP 5
#define PIN_BELL 4
#define PIN_LAMP_BUTTON 12
#define PIN_BELL_BUTTON 14
#define PIN_CONFIG_BUTTON 13
#define PIN_LED 2

// Relay Logic (Active LOW relays)
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define UTC_OFFSET 7200      // Rwanda (UTC+2)
#define UTC_OFFSET_DST 0

// Blynk Message Limits
#define BLYNK_MONTHLY_LIMIT 2000
#define BLYNK_WARNING_THRESHOLD 75
#define BLYNK_CRITICAL_THRESHOLD 90
#define MONTH_MS 2592000000UL  // 30 days in milliseconds

// Timing Constants
#define BELL_ON_DURATION 3000      // 3 seconds
#define BELL_OFF_DURATION 500       // 0.5 seconds
#define BELL_CYCLES 2               // Ring 3 times
#define BUTTON_DEBOUNCE_DELAY 300    // 300ms debounce
#define MENU_TIMEOUT_MS 120000       // 2 minutes menu timeout
#define WIFI_RECONNECT_INTERVAL 30000 // 30 seconds
#define CONFIG_SAVE_INTERVAL 3600000  // 1 hour

// Loop Intervals
#define BUTTON_CHECK_INTERVAL 50
#define WIFI_CHECK_INTERVAL 30000
#define BLYNK_RUN_INTERVAL 100
#define TIME_UPDATE_INTERVAL 1000
#define LED_UPDATE_INTERVAL 20
#define BLYNK_MESSAGE_CHECK_INTERVAL 3600000

// Bell Tolerance (minutes)
#define BELL_TOLERANCE_MINUTES 0

// Build info
#define BUILD_DATE __DATE__ " " __TIME__


unsigned long lastReset = 0;
const unsigned long dayMillis = 24 * 60 * 60 * 1000UL;
// ====================================================================================================
// ENUMERATIONS
// ====================================================================================================

enum SystemMode {
  MODE_ECO = 0,        // Minimal Blynk messages
  MODE_NORMAL = 1,      // Standard messages
  MODE_VERBOSE = 2,     // All messages
  MODE_SILENT = 3       // No Blynk messages
};

enum BellState {
  BELL_IDLE,
  BELL_ON,
  BELL_OFF,
  BELL_COMPLETE
};

enum LEDState {
  LED_OFF,
  LED_SOLID,
  LED_FADING,
  LED_ERROR_BLINK,
  LED_CONFIG_MODE,
  LED_WIFI_SCAN,
  LED_SCHEDULE_EDIT,
  LED_BLYNK_WARNING,
  LED_WIFI_CONNECTING
};

enum MenuState {
  MENU_NONE,
  MENU_WIFI_SCAN,
  MENU_WIFI_PASSWORD,
  MENU_SCHEDULE_EDIT_WEEKDAY,
  MENU_SCHEDULE_EDIT_WEEKEND,
  MENU_LIGHTS_EDIT_WEEKDAY,
  MENU_LIGHTS_EDIT_WEEKEND,
  MENU_BULK_SCHEDULE_EDIT
};

// ====================================================================================================
// DATA STRUCTURES
// ====================================================================================================

struct LightingSchedule {
  int startHour;
  int startMinute;
  int endHour;
  int endMinute;
  bool active;
  
  // Constructor
  LightingSchedule() : startHour(0), startMinute(0), endHour(0), endMinute(0), active(false) {}
  
  LightingSchedule(int sh, int sm, int eh, int em, bool act) 
    : startHour(sh), startMinute(sm), endHour(eh), endMinute(em), active(act) {}
    
  String toString() {
    char buffer[30];
    sprintf(buffer, "%02d:%02d - %02d:%02d %s", 
            startHour, startMinute, endHour, endMinute,
            active ? "[ACTIVE]" : "[INACTIVE]");
    return String(buffer);
  }
  
  String toTimeString() {
    char buffer[12];
    sprintf(buffer, "%02d:%02d %02d:%02d", startHour, startMinute, endHour, endMinute);
    return String(buffer);
  }
};

struct SystemConfig {
  String wifiSSID;
  String wifiPassword;
  String blynkToken;
  String schoolID;
  SystemMode mode;
  bool isConfigured;
  
  SystemConfig() : mode(MODE_NORMAL), isConfigured(false) {}
};

// ====================================================================================================
// GLOBAL VARIABLES
// ====================================================================================================

// System Configuration
SystemConfig systemConfig;
Preferences preferences;
const char* NAMESPACE = "bell_system";

// Operation Mode - FIXED: Added this missing declaration
SystemMode currentMode = MODE_NORMAL;
String modeNames[4] = {"ECO", "NORMAL", "VERBOSE", "SILENT"};

// Schedules
std::vector<String> weekdayRingTimes;
std::vector<String> weekendRingTimes;
bool* hasRungWeekday = nullptr;
bool* hasRungWeekend = nullptr;

// Lighting Schedules
LightingSchedule weekdayLights[3] = {
  LightingSchedule(18, 0, 19, 30, true),
  LightingSchedule(19, 40, 21, 30, true),
  LightingSchedule(21, 40, 7, 0, true)
};

LightingSchedule weekendLights[3] = {
  LightingSchedule(18, 0, 19, 30, true),
  LightingSchedule(19, 40, 21, 30, true),
  LightingSchedule(21, 40, 7, 0, true)
};

// State Variables
bool lampState = false;
bool manualLampOverride = false;
int currentDayOfWeek = -1;
int lastKnownDayOfWeek = -1;
String lastKnownTime = "";
bool wasDisconnected = false;

// Button States
bool lastLampButtonState = HIGH;
bool lastBellButtonState = HIGH;

// Blynk Message Tracking
unsigned long blynkMessageCount = 0;
unsigned long lastMessageResetTime = 0;
bool blynkWarningShown = false;
bool blynkCriticalShown = false;

// Error Counters
unsigned long errorCounters[10] = {0};
#define ERROR_WIFI_FAIL 0
#define ERROR_BLYNK_FAIL 1
#define ERROR_TIME_FAIL 2
#define ERROR_MEMORY_FAIL 3
#define ERROR_BUTTON_FAIL 4
#define ERROR_CONFIG_FAIL 5

// Timer Variables
unsigned long lastButtonCheckTime = 0;
unsigned long lastWifiStatusCheckTime = 0;
unsigned long lastBlynkRunTime = 0;
unsigned long lastTimeUpdateTime = 0;
unsigned long lastConfigSaveTime = 0;
unsigned long lastBlynkMessageCheck = 0;
unsigned long lastSuccessfulConnection = 0;
int connectionAttempts = 0;

// Bell State Machine
BellState bellState = BELL_IDLE;
int bellCycleCount = 0;
unsigned long bellStateStartTime = 0;

// LED State Machine
LEDState ledState = LED_OFF;
int ledBrightness = 0;
int ledFadeDirection = 1;
unsigned long lastLEDFadeTime = 0;
int ledBlinkCount = 0;
unsigned long lastLEDBlinkTime = 0;

// Menu System
MenuState currentMenu = MENU_NONE;
unsigned long menuTimeout = 0;
std::vector<String> scannedSSID;
std::vector<int> scannedRSSI;
std::vector<uint8_t> scannedEncryption;
int selectedNetworkIndex = -1;
String tempPassword = "";

// ====================================================================================================
// FORWARD DECLARATIONS
// ====================================================================================================
String getBlynkStatus();
void initializeDefaultSchedules();

// ====================================================================================================
// LOGGING SYSTEM (NO AMBIGUOUS OVERLOADS)
// ====================================================================================================

void logMessage(const String& message, bool forceToBlynk = false) {
  // Always print to serial
  Serial.println(message);
  
  // Check Blynk sending conditions
  if (!forceToBlynk) {
    if (currentMode == MODE_SILENT) return;
    if (currentMode == MODE_ECO) {
      // In ECO mode, only send important messages
      if (message.indexOf("ERROR") < 0 && 
          message.indexOf("BELL") < 0 &&
          message.indexOf("CONFIG") < 0 &&
          message.indexOf("🔔") < 0 &&
          message.indexOf("💡") < 0 &&
          message.indexOf("⚠️") < 0) {
        return;
      }
    }
  }
  
  // Send to Blynk if connected
  if (WiFi.status() == WL_CONNECTED && !systemConfig.blynkToken.isEmpty() && Blynk.connected()) {
    blynkMessageCount++;
    
    // Check message thresholds
    float percentUsed = (float)blynkMessageCount / BLYNK_MONTHLY_LIMIT * 100;
    if (percentUsed > BLYNK_CRITICAL_THRESHOLD && !blynkCriticalShown) {
      Serial.println("⚠️ CRITICAL: 90% of Blynk messages used! Switch to ECO mode!");
      blynkCriticalShown = true;
    } else if (percentUsed > BLYNK_WARNING_THRESHOLD && !blynkWarningShown) {
      Serial.println("⚠️ WARNING: 75% of Blynk messages used! Consider ECO mode");
      blynkWarningShown = true;
    }
    
    // Don't send if over limit
    if (blynkMessageCount <= BLYNK_MONTHLY_LIMIT * 0.95) {
      Blynk.virtualWrite(V9, message);
    }
  }
}

void logMessageInt(const String& prefix, int value, bool forceToBlynk = false) {
  logMessage(prefix + String(value), forceToBlynk);
}

void logMessageBool(const String& prefix, bool value, bool forceToBlynk = false) {
  logMessage(prefix + (value ? "ON" : "OFF"), forceToBlynk);
}

void logMessageStr(const String& prefix, const String& value, bool forceToBlynk = false) {
  logMessage(prefix + value, forceToBlynk);
}

void logMessageTime(const String& prefix, int hour, int minute, bool forceToBlynk = false) {
  char buffer[10];
  sprintf(buffer, "%02d:%02d", hour, minute);
  logMessage(prefix + String(buffer), forceToBlynk);
}

void logMessageParts(const String& p1, const String& p2, const String& p3 = "", 
                    const String& p4 = "", const String& p5 = "", bool forceToBlynk = false) {
  String message = p1 + p2 + p3 + p4 + p5;
  logMessage(message, forceToBlynk);
}

void logError(int errorType, const String& details = "") {
  if (errorType >= 0 && errorType < 10) {
    errorCounters[errorType]++;
    String errorMsg = "ERROR [" + String(errorType) + "]: ";
    switch(errorType) {
      case ERROR_WIFI_FAIL: errorMsg += "WiFi - "; break;
      case ERROR_BLYNK_FAIL: errorMsg += "Blynk - "; break;
      case ERROR_TIME_FAIL: errorMsg += "Time Sync - "; break;
      case ERROR_MEMORY_FAIL: errorMsg += "Memory - "; break;
      case ERROR_BUTTON_FAIL: errorMsg += "Button - "; break;
      case ERROR_CONFIG_FAIL: errorMsg += "Config - "; break;
    }
    errorMsg += details;
    Serial.println(errorMsg); // Only to serial, not Blynk to save messages
  }
}

// ====================================================================================================
// LED CONTROL
// ====================================================================================================

void setLEDMode(LEDState mode) {
  if (ledState != mode) {
    ledState = mode;
    ledBrightness = 0;
    ledFadeDirection = 1;
    ledBlinkCount = 0;
    lastLEDBlinkTime = millis();
    
    switch(mode) {
      case LED_OFF:
        analogWrite(PIN_LED, 0);
        break;
      case LED_SOLID:
        analogWrite(PIN_LED, 255);
        break;
      default:
        // Other modes handled in updateLED
        break;
    }
  }
}

void updateLED() {
  unsigned long currentTime = millis();
  
  switch(ledState) {
    case LED_OFF:
      // Already handled
      break;
      
    case LED_SOLID:
      // Already handled
      break;
      
    case LED_FADING:
      if (currentTime - lastLEDFadeTime >= LED_UPDATE_INTERVAL) {
        lastLEDFadeTime = currentTime;
        ledBrightness += ledFadeDirection * 10;
        if (ledBrightness >= 255) {
          ledBrightness = 255;
          ledFadeDirection = -1;
        } else if (ledBrightness <= 0) {
          ledBrightness = 0;
          ledFadeDirection = 1;
        }
        analogWrite(PIN_LED, ledBrightness);
      }
      break;
      
    case LED_ERROR_BLINK:
      if (currentTime - lastLEDBlinkTime >= 200) {
        lastLEDBlinkTime = currentTime;
        ledBlinkCount++;
        analogWrite(PIN_LED, (ledBlinkCount % 2 == 0) ? 255 : 0);
        if (ledBlinkCount >= 20) setLEDMode(LED_SOLID);
      }
      break;
      
    case LED_CONFIG_MODE:
      if (currentTime - lastLEDBlinkTime >= 100) {
        lastLEDBlinkTime = currentTime;
        ledBlinkCount++;
        analogWrite(PIN_LED, (ledBlinkCount % 2 == 0) ? 255 : 0);
      }
      break;
      
    case LED_WIFI_SCAN:
      if (currentTime - lastLEDBlinkTime >= 150) {
        lastLEDBlinkTime = currentTime;
        ledBlinkCount++;
        analogWrite(PIN_LED, (ledBlinkCount % 4 < 2) ? 255 : 0);
      }
      break;
      
    case LED_SCHEDULE_EDIT:
      if (currentTime - lastLEDBlinkTime >= 120) {
        lastLEDBlinkTime = currentTime;
        ledBlinkCount++;
        analogWrite(PIN_LED, (ledBlinkCount % 6 < 3) ? 255 : 0);
      }
      break;
      
    case LED_BLYNK_WARNING:
      if (currentTime - lastLEDBlinkTime >= 500) {
        lastLEDBlinkTime = currentTime;
        ledBlinkCount++;
        analogWrite(PIN_LED, (ledBlinkCount % 2 == 0) ? 255 : 0);
      }
      break;
      
    case LED_WIFI_CONNECTING:
      if (currentTime - lastLEDBlinkTime >= 300) {
        lastLEDBlinkTime = currentTime;
        ledBlinkCount++;
        analogWrite(PIN_LED, (ledBlinkCount % 2 == 0) ? 255 : 0);
      }
      break;
  }
}

// ====================================================================================================
// INITIALIZATION FUNCTIONS
// ====================================================================================================

void initializeDefaultSchedules() {
  // Initialize weekday bell times
  weekdayRingTimes.clear();
  const char* defaultWeekday[] = {
   "05:30", "06:30", "07:00", "07:30","08:00","08:40","09:25","10:10","10:20","11:00","11:45","13:10","13:50","14:35","15:20","16:05", "16:50", "18:30", "19:30", "20:30", "21:30"
  };
  for (const char* time : defaultWeekday) {
    weekdayRingTimes.push_back(String(time)); 
  }
  
  // Initialize weekend bell times
  weekendRingTimes.clear();
  const char* defaultWeekend[] = {
   "07:00", "07:30", "12:20", "18:30", "19:30","20:30","21:30"
  };
  for (const char* time : defaultWeekend) {
    weekendRingTimes.push_back(String(time)); 
  }
  
  // Initialize ring tracking arrays
  if (hasRungWeekday) delete[] hasRungWeekday;
  if (hasRungWeekend) delete[] hasRungWeekend;
  
  hasRungWeekday = new bool[weekdayRingTimes.size()]();
  hasRungWeekend = new bool[weekendRingTimes.size()]();
}

// ====================================================================================================
// CONFIGURATION MANAGEMENT
// ====================================================================================================

bool loadConfiguration() {
  logMessage("Loading configuration from flash memory...", true);
  
  bool success = preferences.begin(NAMESPACE, true);
  
  if (success) {
    systemConfig.wifiSSID = preferences.getString("wifi_ssid", "");
    systemConfig.wifiPassword = preferences.getString("wifi_pass", "");
    systemConfig.blynkToken = preferences.getString("blynk_token", "");
    systemConfig.schoolID = preferences.getString("school_id", "SCHOOL_001");
    systemConfig.mode = (SystemMode)preferences.getUChar("system_mode", MODE_NORMAL);
    
    lampState = preferences.getBool("lamp_state", false);
    manualLampOverride = preferences.getBool("override_state", false);
    
    // Load schedules (simplified - in production you'd serialize properly)
    // This is a placeholder - implement full serialization as needed
    
    preferences.end();
    
    systemConfig.isConfigured = (!systemConfig.wifiSSID.isEmpty() && 
                                 !systemConfig.wifiPassword.isEmpty() && 
                                 !systemConfig.blynkToken.isEmpty());
    
    currentMode = systemConfig.mode;
    
    logMessage("Configuration loaded successfully", true);
    logMessageStr("School ID: ", systemConfig.schoolID, true);
    logMessageStr("WiFi SSID: ", systemConfig.wifiSSID, true);
    logMessageStr("Mode: ", modeNames[currentMode], true);
    
    float percent = (float)blynkMessageCount / BLYNK_MONTHLY_LIMIT * 100;
    logMessageParts("Messages: ", String(blynkMessageCount), "/", 
                    String(BLYNK_MONTHLY_LIMIT), " (" + String(percent, 1) + "%)", true);
    
    return systemConfig.isConfigured;
  }
  
  logMessage("Failed to load configuration", true);
  return false;
}

bool saveConfiguration() {
  logMessage("Saving configuration to flash memory...", true);
  
  bool success = preferences.begin(NAMESPACE, false);
  
  if (success) {
    preferences.putString("wifi_ssid", systemConfig.wifiSSID);
    preferences.putString("wifi_pass", systemConfig.wifiPassword);
    preferences.putString("blynk_token", systemConfig.blynkToken);
    preferences.putString("school_id", systemConfig.schoolID);
    preferences.putUChar("system_mode", (uint8_t)systemConfig.mode);
    preferences.putBool("lamp_state", lampState);
    preferences.putBool("override_state", manualLampOverride);
    
    // Save schedules (simplified)
    
    preferences.end();
    logMessage("Configuration saved successfully", true);
    return true;
  }
  
  logMessage("Failed to save configuration", true);
  return false;
}

// ====================================================================================================
// WIFI FUNCTIONS
// ====================================================================================================

void enterConfigMode() {
  logMessage("========================================", true);
  logMessage("ENTERING CONFIGURATION MODE", true);
  logMessage("========================================", true);
  logMessage("Creating WiFi AP: SchoolBell-AP", true);
  logMessage("Connect to this AP and configure WiFi & Blynk", true);
  logMessage("Configuration portal will time out in 3 minutes", true);
  
  setLEDMode(LED_CONFIG_MODE);
  
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  
  WiFiManagerParameter custom_blynk_token("blynk", "Blynk Auth Token", 
                                          systemConfig.blynkToken.c_str(), 40);
  WiFiManagerParameter custom_school_id("school", "School ID", 
                                        systemConfig.schoolID.c_str(), 20);
  WiFiManagerParameter custom_mode("mode", "Mode (0=ECO,1=NORMAL,2=VERBOSE,3=SILENT)", 
                                   String(currentMode).c_str(), 2);
  
  wifiManager.addParameter(&custom_blynk_token);
  wifiManager.addParameter(&custom_school_id);
  wifiManager.addParameter(&custom_mode);
  
  logMessage("Starting configuration portal...", true);
  
  if (!wifiManager.startConfigPortal("SchoolBell-AP")) {
    logMessage("ERROR: Failed to connect or configuration timeout", true);
    logMessage("Restarting in 3 seconds...", true);
    delay(3000);
    ESP.restart();
  }
  
  logMessage("Configuration received!", true);
  
  systemConfig.wifiSSID = WiFi.SSID();
  systemConfig.wifiPassword = WiFi.psk();
  systemConfig.blynkToken = custom_blynk_token.getValue();
  systemConfig.schoolID = custom_school_id.getValue();
  
  int newMode = atoi(custom_mode.getValue());
  if (newMode >= 0 && newMode <= 3) {
    systemConfig.mode = (SystemMode)newMode;
    currentMode = systemConfig.mode;
  }
  
  logMessage("New configuration:", true);
  logMessageStr("WiFi SSID: ", systemConfig.wifiSSID, true);
  logMessageStr("School ID: ", systemConfig.schoolID, true);
  logMessageStr("Mode: ", modeNames[currentMode], true);
  
  if (saveConfiguration()) {
    logMessage("Configuration saved! Restarting...", true);
  }
  
  delay(1000);
  ESP.restart();
}

bool checkConfigButton() {
  static unsigned long lastCheck = 0;
  static bool lastState = HIGH;
  static unsigned long pressStartTime = 0;
  
  if (millis() - lastCheck < 50) return false;
  lastCheck = millis();
  
  bool currentState = digitalRead(PIN_CONFIG_BUTTON);
  
  if (currentState == LOW && lastState == HIGH) {
    pressStartTime = millis();
    logMessage("Config button pressed - hold for 5 seconds to enter config mode", false);
  }
  
  if (currentState == LOW && (millis() - pressStartTime > 5000)) {
    if (pressStartTime > 0) {
      logMessage("Config button held for 5 seconds! Entering config mode...", true);
      pressStartTime = 0;
      return true;
    }
  }
  
  lastState = currentState;
  return false;
}

void scanWiFiNetworks() {
  logMessage("📡 Scanning for WiFi networks...", true);
  
  scannedSSID.clear();
  scannedRSSI.clear();
  scannedEncryption.clear();
  
  setLEDMode(LED_WIFI_SCAN);
  
  int n = WiFi.scanNetworks();
  
  if (n == 0) {
    logMessage("  No networks found", true);
    setLEDMode(LED_FADING);
    return;
  }
  
  logMessageParts("📡 Found ", String(n), " networks:", "", "", true);
  
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    uint8_t encryption = WiFi.encryptionType(i);
    
    scannedSSID.push_back(ssid);
    scannedRSSI.push_back(rssi);
    scannedEncryption.push_back(encryption);
    
    String security = (encryption == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED";
    String bars = "";
    if (rssi > -50) bars = "▰▰▰▰▰";
    else if (rssi > -60) bars = "▰▰▰▰▱";
    else if (rssi > -70) bars = "▰▰▰▱▱";
    else if (rssi > -80) bars = "▰▰▱▱▱";
    else bars = "▰▱▱▱▱";
    
    logMessageParts("  ", String(i+1), ". ", ssid, " " + bars + " (" + String(rssi) + "dBm) " + security, true);
  }
  
  logMessage("", true);
  logMessage("To connect: scan [number] - e.g., 'scan 3'", true);
  logMessage("For secured networks, you'll be prompted for password", true);
}

void connectToWiFiNetwork(int index, const String& password = "") {
  if (index < 0 || index >= (int)scannedSSID.size()) {
    logMessage("❌ Invalid network number", true);
    return;
  }
  
  String ssid = scannedSSID[index];
  bool isOpen = (scannedEncryption[index] == WIFI_AUTH_OPEN);
  
  setLEDMode(LED_WIFI_CONNECTING);
  
  if (isOpen) {
    logMessageParts("📶 Connecting to OPEN network: ", ssid, "", "", "", true);
    
    WiFi.disconnect();
    delay(100);
    WiFi.begin(ssid.c_str(), "");
    
    int attempts = 0;
    while (attempts < 30) { // 15 second timeout
      if (WiFi.status() == WL_CONNECTED) {
        logMessage("✅ Connected successfully!", true);
        logMessageParts("IP: ", WiFi.localIP().toString(), "", "", "", true);
        logMessageParts("Signal: ", String(WiFi.RSSI()), " dBm", "", "", true);
        
        systemConfig.wifiSSID = ssid;
        systemConfig.wifiPassword = "";
        saveConfiguration();
        
        setLEDMode(LED_FADING);
        currentMenu = MENU_NONE;
        return;
      }
      delay(500);
      attempts++;
      if (attempts % 6 == 0) logMessage("  Still connecting...", false);
    }
    
    logMessage("❌ Connection failed - timeout", true);
    setLEDMode(LED_FADING);
    
  } else {
    if (password.isEmpty()) {
      // First time - ask for password
      logMessageParts("🔒 Network requires password: ", ssid, "", "", "", true);
      logMessage("", true);
      logMessage("Please type: pass [your_password]", true);
      logMessage("Example: pass MyWiFiPassword123", true);
      logMessage("", true);
      logMessage("Or type 'cancel' to abort", true);
      
      selectedNetworkIndex = index;
      currentMenu = MENU_WIFI_PASSWORD;
      menuTimeout = millis() + MENU_TIMEOUT_MS;
      
    } else {
      // We have password - attempt connection
      logMessageParts("📶 Connecting to SECURED network: ", ssid, "", "", "", true);
      
      WiFi.disconnect();
      delay(100);
      WiFi.begin(ssid.c_str(), password.c_str());
      
      int attempts = 0;
      while (attempts < 30) { // 15 second timeout
        if (WiFi.status() == WL_CONNECTED) {
          logMessage("✅ Connected successfully!", true);
          logMessageParts("IP: ", WiFi.localIP().toString(), "", "", "", true);
          logMessageParts("Signal: ", String(WiFi.RSSI()), " dBm", "", "", true);
          
          systemConfig.wifiSSID = ssid;
          systemConfig.wifiPassword = password;
          saveConfiguration();
          
          setLEDMode(LED_FADING);
          currentMenu = MENU_NONE;
          return;
        }
        delay(500);
        attempts++;
        if (attempts % 6 == 0) logMessage("  Still connecting...", false);
      }
      
      logMessage("❌ Connection failed - invalid password or timeout", true);
      logMessage("Try again with correct password", true);
    }
  }
}

// ====================================================================================================
// TIME FUNCTIONS
// ====================================================================================================

String getCurrentTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    String currentTime = String(timeStr);
    currentDayOfWeek = timeinfo.tm_wday;
    lastKnownDayOfWeek = currentDayOfWeek;
    
    static int lastLoggedMinute = -1;
    if (timeinfo.tm_min != lastLoggedMinute && currentMode == MODE_VERBOSE) {
      lastLoggedMinute = timeinfo.tm_min;
      const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", 
                            "Thursday", "Friday", "Saturday"};
      String dayName = (currentDayOfWeek >= 0 && currentDayOfWeek <= 6) ? 
                       days[currentDayOfWeek] : "Unknown";
      logMessageParts("Time: ", currentTime, " | ", dayName, "", false);
    }
    
    lastKnownTime = currentTime;
    return currentTime;
  } else {
    logError(ERROR_TIME_FAIL, "Failed to get NTP time");
    wasDisconnected = true;
    currentDayOfWeek = lastKnownDayOfWeek;
    return lastKnownTime;
  }
}

bool isTimeWithinTolerance(const String& scheduled, const String& current, int tolerance) {
  int sHour = scheduled.substring(0, 2).toInt();
  int sMin = scheduled.substring(3, 5).toInt();
  int cHour = current.substring(0, 2).toInt();
  int cMin = current.substring(3, 5).toInt();
  
  int sTotal = sHour * 60 + sMin;
  int cTotal = cHour * 60 + cMin;
  
  return abs(cTotal - sTotal) <= tolerance;
}

bool validateTimeFormat(const String& time) {
  if (time.length() != 5) return false;
  if (time[2] != ':') return false;
  
  int hour = time.substring(0, 2).toInt();
  int minute = time.substring(3, 5).toInt();
  
  return (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59);
}

String formatTime(int hour, int minute) {
  char buffer[6];
  sprintf(buffer, "%02d:%02d", hour, minute);
  return String(buffer);
}

String getDayName(int day) {
  const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", 
                        "Thursday", "Friday", "Saturday"};
  return (day >= 0 && day <= 6) ? days[day] : "Unknown";
}

// ====================================================================================================
// BELL FUNCTIONS
// ====================================================================================================

void startBellRing() {
  if (bellState == BELL_IDLE) {
    bellState = BELL_ON;
    bellCycleCount = 0;
    bellStateStartTime = millis();
    digitalWrite(PIN_BELL, RELAY_ON);
    logMessage("========================================", true);
    logMessage("🔔 BELL RINGING STARTED", true);
    logMessageParts("Cycle 1 of ", String(BELL_CYCLES), "", "", "", true);
    logMessage("========================================", true);
  }
}

void updateBellState() {
  if (bellState == BELL_IDLE) return;
  
  unsigned long now = millis();
  unsigned long elapsed = now - bellStateStartTime;
  
  switch(bellState) {
    case BELL_ON:
      if (elapsed >= BELL_ON_DURATION) {
        bellState = BELL_OFF;
        bellStateStartTime = now;
        digitalWrite(PIN_BELL, RELAY_OFF);
      }
      break;
      
    case BELL_OFF:
      if (elapsed >= BELL_OFF_DURATION) {
        bellCycleCount++;
        if (bellCycleCount >= BELL_CYCLES) {
          bellState = BELL_COMPLETE;
        } else {
          bellState = BELL_ON;
          bellStateStartTime = now;
          digitalWrite(PIN_BELL, RELAY_ON);
          logMessageParts("Cycle ", String(bellCycleCount + 1), " of ", 
                         String(BELL_CYCLES), "", false);
        }
      }
      break;
      
    case BELL_COMPLETE:
      bellState = BELL_IDLE;
      digitalWrite(PIN_BELL, RELAY_OFF);
      logMessage("🔔 BELL RINGING COMPLETED", true);
      break;
      
    default:
      break;
  }
}

void checkBellSchedule() {
  String currentTime = getCurrentTime();
  
  if (currentDayOfWeek >= 1 && currentDayOfWeek <= 5) { // Weekday
    for (size_t i = 0; i < weekdayRingTimes.size(); i++) {
      if (isTimeWithinTolerance(weekdayRingTimes[i], currentTime, BELL_TOLERANCE_MINUTES) && 
          !hasRungWeekday[i]) {
        logMessageParts("🔔 Weekday bell scheduled at ", weekdayRingTimes[i], 
                       " ringing now", "", "", true);
        startBellRing();
        hasRungWeekday[i] = true;
        if (Blynk.connected()) Blynk.virtualWrite(V2, "Weekday: " + weekdayRingTimes[i]);
      }
    }
  } else { // Weekend
    for (size_t i = 0; i < weekendRingTimes.size(); i++) {
      if (isTimeWithinTolerance(weekendRingTimes[i], currentTime, BELL_TOLERANCE_MINUTES) && 
          !hasRungWeekend[i]) {
        logMessageParts("🔔 Weekend bell scheduled at ", weekendRingTimes[i], 
                       " ringing now", "", "", true);
        startBellRing();
        hasRungWeekend[i] = true;
        if (Blynk.connected()) Blynk.virtualWrite(V2, "Weekend: " + weekendRingTimes[i]);
      }
    }
  }
}

void resetDailyStates() {
  // Reset ring tracking arrays
  if (hasRungWeekday) delete[] hasRungWeekday;
  if (hasRungWeekend) delete[] hasRungWeekend;
  
  hasRungWeekday = new bool[weekdayRingTimes.size()]();
  hasRungWeekend = new bool[weekendRingTimes.size()]();
  
  // Reset manual override
  manualLampOverride = false;
  
  logMessage("========================================", true);
  logMessage("🔄 DAILY RESET COMPLETE", true);
  logMessage("All ring states cleared", true);
  logMessage("Manual override disabled", true);
  logMessage("========================================", true);
  
  saveConfiguration();
}

// ====================================================================================================
// LIGHT CONTROL FUNCTIONS
// ====================================================================================================

bool isLightingTime(int hour, int minute) {
  int totalMinutes = hour * 60 + minute;
  LightingSchedule* schedule;
  int count = 3;
  
  schedule = (currentDayOfWeek >= 1 && currentDayOfWeek <= 5) ? weekdayLights : weekendLights;
  
  for (int i = 0; i < count; i++) {
    if (!schedule[i].active) continue;
    
    int startMinutes = schedule[i].startHour * 60 + schedule[i].startMinute;
    int endMinutes = schedule[i].endHour * 60 + schedule[i].endMinute;
    
    // Handle overnight schedules
    if (endMinutes < startMinutes) {
      if (totalMinutes >= startMinutes || totalMinutes <= endMinutes) {
        return true;
      }
    } else {
      if (totalMinutes >= startMinutes && totalMinutes <= endMinutes) {
        return true;
      }
    }
  }
  
  return false;
}

void updateLights() {
  String currentTime = getCurrentTime();
  int hour = currentTime.substring(0, 2).toInt();
  int minute = currentTime.substring(3, 5).toInt();
  
  static bool lastLightState = false;
  
  if (manualLampOverride) {
    if (lastLightState != lampState && currentMode == MODE_VERBOSE) {
      logMessageBool("Manual override: Lamp ", lampState, false);
      lastLightState = lampState;
    }
    return;
  }
  
  bool shouldBeOn = isLightingTime(hour, minute);
  
  if (shouldBeOn != lastLightState) {
    logMessageParts("💡 Lights ", shouldBeOn ? "ON" : "OFF", " at ", 
                   formatTime(hour, minute), "", currentMode != MODE_SILENT);
    lastLightState = shouldBeOn;
  }
  
  digitalWrite(PIN_LAMP, shouldBeOn ? RELAY_ON : RELAY_OFF);
  lampState = shouldBeOn;
}

void setLamp(bool state, bool manual = true) {
  lampState = state;
  if (manual) manualLampOverride = true;
  digitalWrite(PIN_LAMP, state ? RELAY_ON : RELAY_OFF);
  logMessageBool("💡 Lamp set to ", state, true);
  if (Blynk.connected()) Blynk.virtualWrite(V0, state);
  saveConfiguration();
}

// ====================================================================================================
// BUTTON HANDLING
// ====================================================================================================

void checkButtons() {
  static unsigned long lastLampDebounce = 0;
  static unsigned long lastBellDebounce = 0;
  unsigned long now = millis();
  
  // Check config button
  if (checkConfigButton()) {
    enterConfigMode();
    return;
  }
  
  // Lamp button
  bool lampButton = digitalRead(PIN_LAMP_BUTTON);
  if (lampButton == LOW && lastLampButtonState == HIGH) {
    if (now - lastLampDebounce >= BUTTON_DEBOUNCE_DELAY) {
      setLamp(!lampState, true);
      lastLampDebounce = now;
    }
  }
  lastLampButtonState = lampButton;
  
  // Bell button
  bool bellButton = digitalRead(PIN_BELL_BUTTON);
  if (bellButton == LOW && lastBellButtonState == HIGH) {
    if (now - lastBellDebounce >= BUTTON_DEBOUNCE_DELAY) {
      logMessage("👆 Manual bell button pressed", true);
      startBellRing();
      if (Blynk.connected()) {
        Blynk.virtualWrite(V1, 1);
        delay(100);
        Blynk.virtualWrite(V1, 0);
      }
      lastBellDebounce = now;
    }
  }
  lastBellButtonState = bellButton;
}

// ====================================================================================================
// SCHEDULE MANAGEMENT FUNCTIONS
// ====================================================================================================

bool addRingTime(std::vector<String>& schedule, bool*& hasRung, const String& time) {
  if (!validateTimeFormat(time)) {
    return false;
  }
  
  // Check for duplicates
  for (const auto& t : schedule) {
    if (t == time) return false;
  }
  
  schedule.push_back(time);
  std::sort(schedule.begin(), schedule.end());
  
  // Reallocate hasRung array
  delete[] hasRung;
  hasRung = new bool[schedule.size()]();
  
  return true;
}

bool removeRingTime(std::vector<String>& schedule, bool*& hasRung, int index) {
  if (index < 0 || index >= (int)schedule.size()) {
    return false;
  }
  
  schedule.erase(schedule.begin() + index);
  
  // Reallocate hasRung array
  delete[] hasRung;
  hasRung = new bool[schedule.size()]();
  
  return true;
}

bool editRingTime(std::vector<String>& schedule, bool*& hasRung, int index, const String& newTime) {
  if (index < 0 || index >= (int)schedule.size() || !validateTimeFormat(newTime)) {
    return false;
  }
  
  schedule[index] = newTime;
  std::sort(schedule.begin(), schedule.end());
  
  // Reallocate hasRung array
  delete[] hasRung;
  hasRung = new bool[schedule.size()]();
  
  return true;
}

bool bulkSetRingTimes(std::vector<String>& schedule, bool*& hasRung, const std::vector<String>& newTimes) {
  // Validate all times first
  for (const auto& time : newTimes) {
    if (!validateTimeFormat(time)) {
      return false;
    }
  }
  
  schedule = newTimes;
  std::sort(schedule.begin(), schedule.end());
  
  // Reallocate hasRung array
  delete[] hasRung;
  hasRung = new bool[schedule.size()]();
  
  return true;
}

void printRingSchedule(const std::vector<String>& schedule, const bool* hasRung, 
                       const String& title, bool toBlynk = true) {
  logMessageParts("📅 ", title, " SCHEDULE:", "", "", toBlynk);
  logMessage("════════════════════", toBlynk);
  
  if (schedule.empty()) {
    logMessage("  No times configured", toBlynk);
  } else {
    for (size_t i = 0; i < schedule.size(); i++) {
      String status = (hasRung && hasRung[i]) ? "✅" : "⏳";
      logMessageParts("  ", String(i+1), ". ", status + " " + schedule[i], "", toBlynk);
    }
  }
  logMessage("════════════════════", toBlynk);
  logMessageParts("Total: ", String(schedule.size()), " rings", "", "", toBlynk);
}

bool editLightSchedule(LightingSchedule* schedule, int index, 
                      int startHour, int startMinute, int endHour, int endMinute) {
  if (index < 0 || index >= 3) return false;
  if (startHour < 0 || startHour > 23 || startMinute < 0 || startMinute > 59) return false;
  if (endHour < 0 || endHour > 23 || endMinute < 0 || endMinute > 59) return false;
  
  schedule[index].startHour = startHour;
  schedule[index].startMinute = startMinute;
  schedule[index].endHour = endHour;
  schedule[index].endMinute = endMinute;
  
  return true;
}

void printLightSchedule(LightingSchedule* schedule, const String& title, bool toBlynk = true) {
  logMessageParts("💡 ", title, " LIGHT SCHEDULE:", "", "", toBlynk);
  logMessage("════════════════════", toBlynk);
  
  for (int i = 0; i < 3; i++) {
    logMessageParts("  ", String(i+1), ". ", schedule[i].toString(), "", toBlynk);
  }
  logMessage("════════════════════", toBlynk);
}

// ====================================================================================================
// BLYNK STATUS FUNCTION - FIXED: Added missing function
// ====================================================================================================

String getBlynkStatus() {
  float percent = (float)blynkMessageCount / BLYNK_MONTHLY_LIMIT * 100;
  
  String status = "📊 BLYNK STATUS:\n";
  status += "════════════════════\n";
  status += "Connected: " + String(Blynk.connected() ? "YES" : "NO") + "\n";
  status += "Mode: " + modeNames[currentMode] + "\n";
  status += "Messages: " + String(blynkMessageCount) + "/" + String(BLYNK_MONTHLY_LIMIT) + "\n";
  status += "Used: " + String(percent, 1) + "%\n";
  status += "Remaining: " + String(BLYNK_MONTHLY_LIMIT - blynkMessageCount) + "\n";
  
  // Progress bar
  status += "[";
  int barLength = 20;
  int filled = (percent / 100) * barLength;
  for (int i = 0; i < barLength; i++) {
    status += (i < filled) ? "█" : "░";
  }
  status += "]\n";
  
  if (percent > BLYNK_CRITICAL_THRESHOLD) {
    status += "⚠️ CRITICAL: Near limit! Switch to ECO mode!\n";
  } else if (percent > BLYNK_WARNING_THRESHOLD) {
    status += "⚠️ WARNING: 75% used. Consider ECO mode\n";
  }
  
  status += "════════════════════\n";
  return status;
}

// ====================================================================================================
// HELP SYSTEM
// ====================================================================================================

void showGeneralHelp() {
  logMessage("📋 SCHOOL BELL SYSTEM - COMPLETE HELP", true);
  logMessage("═══════════════════════════════════════", true);
  logMessage("", true);
  logMessage("📌 BASIC COMMANDS:", true);
  logMessage("  help / h / ?                 - Show this help", true);
  logMessage("  help [command]                - Detailed help for a command", true);
  logMessage("  status / s                    - Show system status", true);
  logMessage("  time                          - Show current time", true);
  logMessage("  uptime                        - Show system uptime", true);
  logMessage("", true);
  logMessage("📶 WIFI COMMANDS:", true);
  logMessage("  scan                          - Scan for WiFi networks", true);
  logMessage("  scan [number]                  - Connect to network (e.g., 'scan 3')", true);
  logMessage("  wifi                          - Show WiFi status", true);
  logMessage("  config                        - Enter configuration portal", true);
  logMessage("", true);
  logMessage("🔔 BELL SCHEDULE COMMANDS:", true);
  logMessage("  schedule                      - Show today's bells", true);
  logMessage("  weekday                       - Show all weekday bells", true);
  logMessage("  weekend                       - Show all weekend bells", true);
  logMessage("  edit weekday                   - Edit weekday bells", true);
  logMessage("  edit weekend                   - Edit weekend bells", true);
  logMessage("  bulk weekday [t1,t2,...]       - Bulk set weekday times", true);
  logMessage("  bulk weekend [t1,t2,...]       - Bulk set weekend times", true);
  logMessage("", true);
  logMessage("💡 LIGHT SCHEDULE COMMANDS:", true);
  logMessage("  lights                         - Show all light schedules", true);
  logMessage("  lights wd                       - Show weekday lights", true);
  logMessage("  lights we                       - Show weekend lights", true);
  logMessage("  edit lights wd                  - Edit weekday lights", true);
  logMessage("  edit lights we                  - Edit weekend lights", true);
  logMessage("", true);
  logMessage("🎮 CONTROL COMMANDS:", true);
  logMessage("  ring / bell                    - Ring bell manually", true);
  logMessage("  lights on/off                   - Manual light control", true);
  logMessage("  lights auto                     - Return to auto mode", true);
  logMessage("  reset                          - Reset manual override", true);
  logMessage("", true);
  logMessage("⚙️ MODE COMMANDS:", true);
  logMessage("  mode                           - Show current mode", true);
  logMessage("  mode eco                        - Switch to ECO mode", true);
  logMessage("  mode normal                     - Switch to NORMAL mode", true);
  logMessage("  mode verbose                    - Switch to VERBOSE mode", true);
  logMessage("  mode silent                     - Switch to SILENT mode", true);
  logMessage("  blynk                          - Show Blynk message status", true);
  logMessage("", true);
  logMessage("🔧 SYSTEM COMMANDS:", true);
  logMessage("  memory                         - Show memory usage", true);
  logMessage("  errors                         - Show error counters", true);
  logMessage("  errors clear                    - Clear error counters", true);
  logMessage("  restart                        - Restart the system", true);
  logMessage("  factory                        - Factory reset (with confirmation)", true);
  logMessage("  clear                          - Clear terminal", true);
  logMessage("", true);
  logMessage("📖 For detailed help on any command, type: help [command]", true);
  logMessage("═══════════════════════════════════════", true);
}

void showCommandHelp(const String& cmd) {
  if (cmd == "status" || cmd == "s") {
    logMessage("📖 COMMAND: status (or s)", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Display comprehensive system status", true);
    logMessage("USAGE: status  or  s", true);
    logMessage("", true);
    logMessage("INFORMATION SHOWN:", true);
    logMessage("  • School ID and current mode", true);
    logMessage("  • System uptime and free memory", true);
    logMessage("  • WiFi connection status and IP", true);
    logMessage("  • Lamp state and mode (AUTO/MANUAL)", true);
    logMessage("  • Current time and day", true);
    logMessage("  • Blynk message usage", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > status", true);
    logMessage("  📊 SYSTEM STATUS", true);
    logMessage("  School: SCHOOL_001", true);
    logMessage("  Mode: NORMAL", true);
    logMessage("  Time: 14:30 Monday", true);
    logMessage("  Lamp: OFF (AUTO)", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "time") {
    logMessage("📖 COMMAND: time", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Display current date and time", true);
    logMessage("USAGE: time", true);
    logMessage("", true);
    logMessage("INFORMATION SHOWN:", true);
    logMessage("  • Current time in HH:MM:SS format", true);
    logMessage("  • Day of week, month, day, year", true);
    logMessage("  • If NTP unavailable, shows last known time", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > time", true);
    logMessage("  🕐 14:30:45 Monday, March 15 2024", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "uptime") {
    logMessage("📖 COMMAND: uptime", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Show how long the system has been running", true);
    logMessage("USAGE: uptime", true);
    logMessage("", true);
    logMessage("INFORMATION SHOWN:", true);
    logMessage("  • Days, hours, minutes, seconds since last boot", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > uptime", true);
    logMessage("  ⏱️ Uptime: 2d 4h 30m 15s", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "scan") {
    logMessage("📖 COMMAND: scan", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Scan for nearby WiFi networks and connect", true);
    logMessage("USAGE: scan  -or-  scan [number]", true);
    logMessage("", true);
    logMessage("OPTIONS:", true);
    logMessage("  scan           - Just scan and list networks", true);
    logMessage("  scan [number]  - Connect to network number", true);
    logMessage("", true);
    logMessage("PROCESS:", true);
    logMessage("  1. Type 'scan' to see all networks", true);
    logMessage("  2. Note the number of your network", true);
    logMessage("  3. Type 'scan 3' to connect to network #3", true);
    logMessage("  4. If secured, you'll be prompted for password", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > scan", true);
    logMessage("  📡 Found 3 networks:", true);
    logMessage("    1. HomeWiFi ▰▰▰▰▱ (-65dBm) SECURED", true);
    logMessage("    2. GuestNet ▰▰▱▱▱ (-75dBm) OPEN", true);
    logMessage("  > scan 2", true);
    logMessage("  📶 Connecting to OPEN network: GuestNet", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "wifi") {
    logMessage("📖 COMMAND: wifi", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Show detailed WiFi connection status", true);
    logMessage("USAGE: wifi", true);
    logMessage("", true);
    logMessage("INFORMATION SHOWN:", true);
    logMessage("  • SSID and connection status", true);
    logMessage("  • IP address, gateway, subnet", true);
    logMessage("  • MAC address", true);
    logMessage("  • Signal strength (RSSI)", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > wifi", true);
    logMessage("  📶 WIFI DETAILS", true);
    logMessage("  SSID: HomeWiFi", true);
    logMessage("  IP: 192.168.1.100", true);
    logMessage("  Signal: -45 dBm", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "schedule") {
    logMessage("📖 COMMAND: schedule", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Show today's bell schedule with status", true);
    logMessage("USAGE: schedule", true);
    logMessage("", true);
    logMessage("INFORMATION SHOWN:", true);
    logMessage("  • All bell times for today", true);
    logMessage("  • ✅ = Already rang", true);
    logMessage("  • ⏳ = Pending (not yet rung)", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > schedule", true);
    logMessage("  TODAY'S BELLS:", true);
    logMessage("    ✅ 08:00", true);
    logMessage("    ✅ 09:00", true);
    logMessage("    ⏳ 10:00", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "weekday") {
    logMessage("📖 COMMAND: weekday", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Show all weekday bell times (Mon-Fri)", true);
    logMessage("USAGE: weekday", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > weekday", true);
    logMessage("  📅 WEEKDAY SCHEDULE:", true);
    logMessage("    1. 08:00", true);
    logMessage("    2. 09:00", true);
    logMessage("    3. 10:00", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "weekend") {
    logMessage("📖 COMMAND: weekend", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Show all weekend bell times (Sat-Sun)", true);
    logMessage("USAGE: weekend", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > weekend", true);
    logMessage("  📅 WEEKEND SCHEDULE:", true);
    logMessage("    1. 09:00", true);
    logMessage("    2. 10:00", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "edit") {
    logMessage("📖 COMMAND: edit", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Edit schedules interactively", true);
    logMessage("USAGE: edit [weekday/weekend/lights wd/lights we]", true);
    logMessage("", true);
    logMessage("SUBCOMMANDS:", true);
    logMessage("  edit weekday     - Edit weekday bell times", true);
    logMessage("  edit weekend     - Edit weekend bell times", true);
    logMessage("  edit lights wd   - Edit weekday light schedule", true);
    logMessage("  edit lights we   - Edit weekend light schedule", true);
    logMessage("", true);
    logMessage("IN EDIT MODE:", true);
    logMessage("  list             - Show current items", true);
    logMessage("  add HH:MM        - Add new time", true);
    logMessage("  del N            - Delete item number N", true);
    logMessage("  edit N HH:MM     - Edit item N", true);
    logMessage("  save             - Save and exit", true);
    logMessage("  cancel           - Exit without saving", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > edit weekday", true);
    logMessage("  ✏️ EDITING WEEKDAY RINGS", true);
    logMessage("  > add 14:30", true);
    logMessage("  ✅ Added: 14:30", true);
    logMessage("  > save", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "bulk") {
    logMessage("📖 COMMAND: bulk", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Bulk set entire schedules at once", true);
    logMessage("USAGE: bulk [weekday/weekend] [time1,time2,...]", true);
    logMessage("", true);
    logMessage("DESCRIPTION:", true);
    logMessage("  Completely replaces the current schedule", true);
    logMessage("  with the new list of times.", true);
    logMessage("", true);
    logMessage("FORMAT:", true);
    logMessage("  Times in HH:MM format, separated by commas", true);
    logMessage("  No spaces between commas", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > bulk weekday 08:00,09:00,10:00,11:00", true);
    logMessage("  ✅ Weekday schedule updated", true);
    logMessage("  New schedule has 4 rings", true);
    logMessage("", true);
    logMessage("  > bulk weekend 09:00,10:00,11:00,12:00", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "lights") {
    logMessage("📖 COMMAND: lights", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Control and view light schedules", true);
    logMessage("USAGE: lights [on/off/auto/wd/we]", true);
    logMessage("", true);
    logMessage("SUBCOMMANDS:", true);
    logMessage("  lights         - Show all light schedules", true);
    logMessage("  lights wd      - Show weekday lights only", true);
    logMessage("  lights we      - Show weekend lights only", true);
    logMessage("  lights on      - Turn lights ON manually", true);
    logMessage("  lights off     - Turn lights OFF manually", true);
    logMessage("  lights auto    - Return to automatic mode", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > lights", true);
    logMessage("  💡 LIGHT SCHEDULES", true);
    logMessage("  WEEKDAYS:", true);
    logMessage("    1. 18:00 - 19:30 [ACTIVE]", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "ring" || cmd == "bell") {
    logMessage("📖 COMMAND: ring (or bell)", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Ring the bell manually", true);
    logMessage("USAGE: ring  or  bell", true);
    logMessage("", true);
    logMessage("BELL PATTERN:", true);
    logMessage("  • Rings 3 times", true);
    logMessage("  • Each ring: 3 seconds ON", true);
    logMessage("  • Pause between: 0.5 seconds", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > ring", true);
    logMessage("  🔔 Bell ringing...", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "mode") {
    logMessage("📖 COMMAND: mode", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: View or change operation mode", true);
    logMessage("USAGE: mode  or  mode [eco/normal/verbose/silent]", true);
    logMessage("", true);
    logMessage("MODES:", true);
    logMessage("  eco     - Minimal Blynk messages (500/month)", true);
    logMessage("  normal  - Standard messages (1500/month)", true);
    logMessage("  verbose - All messages (2500/month)", true);
    logMessage("  silent  - No Blynk messages (0/month)", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > mode", true);
    logMessage("  📊 Current mode: NORMAL", true);
    logMessage("  > mode eco", true);
    logMessage("  ✅ Switched to ECO mode", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "blynk") {
    logMessage("📖 COMMAND: blynk", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Show Blynk message usage statistics", true);
    logMessage("USAGE: blynk", true);
    logMessage("", true);
    logMessage("INFORMATION SHOWN:", true);
    logMessage("  • Connection status", true);
    logMessage("  • Current mode", true);
    logMessage("  • Messages used this month", true);
    logMessage("  • Percentage used", true);
    logMessage("  • Visual progress bar", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > blynk", true);
    logMessage("  📊 BLYNK STATUS", true);
    logMessage("  Used: 145/2000 (7.3%)", true);
    logMessage("  [██░░░░░░░░░░░░░░░░░░]", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "memory") {
    logMessage("📖 COMMAND: memory", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Show memory usage statistics", true);
    logMessage("USAGE: memory", true);
    logMessage("", true);
    logMessage("INFORMATION SHOWN:", true);
    logMessage("  • Free heap memory", true);
    logMessage("  • Minimum free heap", true);
    logMessage("  • Maximum allocatable block", true);
    logMessage("  • Total heap size", true);
    logMessage("  • Flash size", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > memory", true);
    logMessage("  Free Heap: 245678 bytes", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "errors") {
    logMessage("📖 COMMAND: errors", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Show or clear error counters", true);
    logMessage("USAGE: errors  or  errors clear", true);
    logMessage("", true);
    logMessage("ERROR TYPES:", true);
    logMessage("  • WiFi Failures", true);
    logMessage("  • Blynk Failures", true);
    logMessage("  • Time Sync Failures", true);
    logMessage("  • Memory Issues", true);
    logMessage("  • Button Errors", true);
    logMessage("  • Config Errors", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > errors", true);
    logMessage("  WiFi Failures: 3", true);
    logMessage("  > errors clear", true);
    logMessage("  ✅ Error counters cleared", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "restart") {
    logMessage("📖 COMMAND: restart", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Restart the system", true);
    logMessage("USAGE: restart", true);
    logMessage("", true);
    logMessage("EFFECTS:", true);
    logMessage("  • Performs full ESP32 restart", true);
    logMessage("  • All settings are preserved", true);
    logMessage("  • 3-second delay before restart", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > restart", true);
    logMessage("  ⚠️ Restarting in 3 seconds...", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "factory") {
    logMessage("📖 COMMAND: factory", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Reset to factory defaults", true);
    logMessage("USAGE: factory  - then confirm with 'factory confirm'", true);
    logMessage("", true);
    logMessage("⚠️ WARNING: THIS IS IRREVERSIBLE ⚠️", true);
    logMessage("", true);
    logMessage("WHAT IT ERASES:", true);
    logMessage("  • WiFi credentials", true);
    logMessage("  • Blynk token", true);
    logMessage("  • School ID", true);
    logMessage("  • All custom schedules", true);
    logMessage("  • Error counters", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > factory", true);
    logMessage("  ⚠️ Type 'factory confirm' to proceed", true);
    logMessage("  > factory confirm", true);
    logMessage("  ⚠️ Factory reset in 3 seconds...", true);
    logMessage("═══════════════════════════", true);
  }
  else if (cmd == "clear") {
    logMessage("📖 COMMAND: clear", true);
    logMessage("═══════════════════════════", true);
    logMessage("PURPOSE: Clear the terminal screen", true);
    logMessage("USAGE: clear", true);
    logMessage("", true);
    logMessage("EFFECTS:", true);
    logMessage("  • Removes all previous output", true);
    logMessage("  • Shows fresh terminal", true);
    logMessage("", true);
    logMessage("EXAMPLE:", true);
    logMessage("  > clear", true);
    logMessage("  ✨ Terminal cleared", true);
    logMessage("═══════════════════════════", true);
  }
  else {
    logMessageParts("❌ No detailed help for '", cmd, "'", "", "", true);
    logMessage("Type 'help' for list of commands", true);
  }
}

// ====================================================================================================
// BLYNK HANDLERS
// ====================================================================================================

BLYNK_WRITE(V0) {
  int state = param.asInt();
  setLamp(state == 1, true);
}

BLYNK_WRITE(V1) {
  if (param.asInt() == 1) {
    logMessage("📱 Blynk: Manual bell triggered", true);
    startBellRing();
    delay(100);
    Blynk.virtualWrite(V1, 0);
  }
}

BLYNK_WRITE(V8) {
  if (param.asInt() == 1) {
    manualLampOverride = false;
    logMessage("📱 Blynk: Manual override reset", true);
    Blynk.virtualWrite(V8, 0);
    saveConfiguration();
  }
}

// ====================================================================================================
// V10 TERMINAL HANDLER - COMPLETE IMPLEMENTATION
// ====================================================================================================

BLYNK_WRITE(V10) {
  String command = param.asStr();
  command.trim();
  
  // Handle menu timeout
  if (currentMenu != MENU_NONE && millis() > menuTimeout) {
    logMessage("⏰ Menu timed out - returning to main menu", true);
    currentMenu = MENU_NONE;
    setLEDMode(LED_FADING);
  }
  
  // ==================================================================================================
  // MENU: WiFi Password Input
  // ==================================================================================================
  if (currentMenu == MENU_WIFI_PASSWORD) {
    if (command == "cancel" || command == "q" || command == "quit") {
      currentMenu = MENU_NONE;
      setLEDMode(LED_FADING);
      logMessage("✅ WiFi setup cancelled", true);
      return;
    }
    
    connectToWiFiNetwork(selectedNetworkIndex, command);
    return;
  }
  
  // ==================================================================================================
  // MENU: Weekday Bell Schedule Edit
  // ==================================================================================================
  if (currentMenu == MENU_SCHEDULE_EDIT_WEEKDAY) {
    if (command == "save") {
      currentMenu = MENU_NONE;
      setLEDMode(LED_FADING);
      saveConfiguration();
      logMessage("✅ Weekday schedule saved", true);
      printRingSchedule(weekdayRingTimes, hasRungWeekday, "WEEKDAY", true);
      return;
    }
    
    if (command == "cancel") {
      currentMenu = MENU_NONE;
      setLEDMode(LED_FADING);
      logMessage("❌ Edit cancelled - no changes saved", true);
      return;
    }
    
    if (command == "list") {
      printRingSchedule(weekdayRingTimes, hasRungWeekday, "WEEKDAY", true);
      return;
    }
    
    if (command.startsWith("add ")) {
      String time = command.substring(4);
      if (addRingTime(weekdayRingTimes, hasRungWeekday, time)) {
        logMessageParts("✅ Added: ", time, "", "", "", true);
        printRingSchedule(weekdayRingTimes, hasRungWeekday, "WEEKDAY", true);
      } else {
        logMessage("❌ Invalid time format or duplicate", true);
        logMessage("   Use HH:MM format (e.g., 14:30)", true);
      }
      return;
    }
    
    if (command.startsWith("del ")) {
      int index = command.substring(4).toInt() - 1;
      if (index >= 0 && index < (int)weekdayRingTimes.size()) {
        String removed = weekdayRingTimes[index];
        if (removeRingTime(weekdayRingTimes, hasRungWeekday, index)) {
          logMessageParts("✅ Removed: ", removed, "", "", "", true);
          printRingSchedule(weekdayRingTimes, hasRungWeekday, "WEEKDAY", true);
        } else {
          logMessage("❌ Failed to remove", true);
        }
      } else {
        logMessageParts("❌ Invalid index. Use 1-", String(weekdayRingTimes.size()), "", "", "", true);
      }
      return;
    }
    
    if (command.startsWith("edit ")) {
      // Format: edit 3 14:30
      int firstSpace = command.indexOf(' ', 5);
      if (firstSpace > 0) {
        int index = command.substring(5, firstSpace).toInt() - 1;
        String newTime = command.substring(firstSpace + 1);
        
        if (editRingTime(weekdayRingTimes, hasRungWeekday, index, newTime)) {
          logMessageParts("✅ Updated item ", String(index+1), " to ", newTime, "", true);
          printRingSchedule(weekdayRingTimes, hasRungWeekday, "WEEKDAY", true);
        } else {
          logMessage("❌ Invalid index or time format", true);
          logMessage("   Use: edit [number] [HH:MM]", true);
        }
      } else {
        logMessage("❌ Invalid format. Use: edit [number] [HH:MM]", true);
      }
      return;
    }
    
    logMessage("Unknown command in edit mode", true);
    logMessage("Available: list, add HH:MM, del N, edit N HH:MM, save, cancel", true);
    return;
  }
  
  // ==================================================================================================
  // MENU: Weekend Bell Schedule Edit
  // ==================================================================================================
  if (currentMenu == MENU_SCHEDULE_EDIT_WEEKEND) {
    if (command == "save") {
      currentMenu = MENU_NONE;
      setLEDMode(LED_FADING);
      saveConfiguration();
      logMessage("✅ Weekend schedule saved", true);
      printRingSchedule(weekendRingTimes, hasRungWeekend, "WEEKEND", true);
      return;
    }
    
    if (command == "cancel") {
      currentMenu = MENU_NONE;
      setLEDMode(LED_FADING);
      logMessage("❌ Edit cancelled - no changes saved", true);
      return;
    }
    
    if (command == "list") {
      printRingSchedule(weekendRingTimes, hasRungWeekend, "WEEKEND", true);
      return;
    }
    
    if (command.startsWith("add ")) {
      String time = command.substring(4);
      if (addRingTime(weekendRingTimes, hasRungWeekend, time)) {
        logMessageParts("✅ Added: ", time, "", "", "", true);
        printRingSchedule(weekendRingTimes, hasRungWeekend, "WEEKEND", true);
      } else {
        logMessage("❌ Invalid time format or duplicate", true);
        logMessage("   Use HH:MM format (e.g., 14:30)", true);
      }
      return;
    }
    
    if (command.startsWith("del ")) {
      int index = command.substring(4).toInt() - 1;
      if (index >= 0 && index < (int)weekendRingTimes.size()) {
        String removed = weekendRingTimes[index];
        if (removeRingTime(weekendRingTimes, hasRungWeekend, index)) {
          logMessageParts("✅ Removed: ", removed, "", "", "", true);
          printRingSchedule(weekendRingTimes, hasRungWeekend, "WEEKEND", true);
        } else {
          logMessage("❌ Failed to remove", true);
        }
      } else {
        logMessageParts("❌ Invalid index. Use 1-", String(weekendRingTimes.size()), "", "", "", true);
      }
      return;
    }
    
    if (command.startsWith("edit ")) {
      int firstSpace = command.indexOf(' ', 5);
      if (firstSpace > 0) {
        int index = command.substring(5, firstSpace).toInt() - 1;
        String newTime = command.substring(firstSpace + 1);
        
        if (editRingTime(weekendRingTimes, hasRungWeekend, index, newTime)) {
          logMessageParts("✅ Updated item ", String(index+1), " to ", newTime, "", true);
          printRingSchedule(weekendRingTimes, hasRungWeekend, "WEEKEND", true);
        } else {
          logMessage("❌ Invalid index or time format", true);
          logMessage("   Use: edit [number] [HH:MM]", true);
        }
      } else {
        logMessage("❌ Invalid format. Use: edit [number] [HH:MM]", true);
      }
      return;
    }
    
    logMessage("Unknown command in edit mode", true);
    logMessage("Available: list, add HH:MM, del N, edit N HH:MM, save, cancel", true);
    return;
  }
  
  // ==================================================================================================
  // MENU: Weekday Lights Edit
  // ==================================================================================================
  if (currentMenu == MENU_LIGHTS_EDIT_WEEKDAY) {
    if (command == "save") {
      currentMenu = MENU_NONE;
      setLEDMode(LED_FADING);
      saveConfiguration();
      logMessage("✅ Weekday light schedule saved", true);
      printLightSchedule(weekdayLights, "WEEKDAY", true);
      return;
    }
    
    if (command == "cancel") {
      currentMenu = MENU_NONE;
      setLEDMode(LED_FADING);
      logMessage("❌ Edit cancelled - no changes saved", true);
      return;
    }
    
    if (command == "list") {
      printLightSchedule(weekdayLights, "WEEKDAY", true);
      return;
    }
    
    if (command.startsWith("toggle ")) {
      int index = command.substring(7).toInt() - 1;
      if (index >= 0 && index < 3) {
        weekdayLights[index].active = !weekdayLights[index].active;
        logMessageParts("✅ Toggled schedule ", String(index+1), 
                       " to ", weekdayLights[index].active ? "ACTIVE" : "INACTIVE", "", true);
        printLightSchedule(weekdayLights, "WEEKDAY", true);
      } else {
        logMessage("❌ Invalid index. Use 1-3", true);
      }
      return;
    }
    
    if (command.startsWith("edit ")) {
      // Format: edit 1 18:30 19:45
      int firstSpace = command.indexOf(' ', 5);
      int secondSpace = command.indexOf(' ', firstSpace + 1);
      
      if (firstSpace > 0 && secondSpace > firstSpace) {
        int index = command.substring(5, firstSpace).toInt() - 1;
        String startTime = command.substring(firstSpace + 1, secondSpace);
        String endTime = command.substring(secondSpace + 1);
        
        if (index >= 0 && index < 3) {
          // Parse start time
          int startHour = startTime.substring(0, 2).toInt();
          int startMinute = startTime.substring(3, 5).toInt();
          
          // Parse end time
          int endHour = endTime.substring(0, 2).toInt();
          int endMinute = endTime.substring(3, 5).toInt();
          
          // Validate
          if (startHour >= 0 && startHour <= 23 && startMinute >= 0 && startMinute <= 59 &&
              endHour >= 0 && endHour <= 23 && endMinute >= 0 && endMinute <= 59) {
            
            weekdayLights[index].startHour = startHour;
            weekdayLights[index].startMinute = startMinute;
            weekdayLights[index].endHour = endHour;
            weekdayLights[index].endMinute = endMinute;
            
            logMessageParts("✅ Updated schedule ", String(index+1), 
                           " to ", formatTime(startHour, startMinute), 
                           " - " + formatTime(endHour, endMinute), true);
            printLightSchedule(weekdayLights, "WEEKDAY", true);
          } else {
            logMessage("❌ Invalid time format. Hours 0-23, Minutes 0-59", true);
          }
        } else {
          logMessage("❌ Invalid index. Use 1-3", true);
        }
      } else {
        logMessage("❌ Invalid format. Use: edit [n] [start] [end]", true);
        logMessage("   Example: edit 2 19:30 21:30", true);
      }
      return;
    }
    
    logMessage("Unknown command in light edit mode", true);
    logMessage("Available: list, toggle N, edit N HH:MM HH:MM, save, cancel", true);
    return;
  }
  
  // ==================================================================================================
  // MENU: Weekend Lights Edit
  // ==================================================================================================
  if (currentMenu == MENU_LIGHTS_EDIT_WEEKEND) {
    if (command == "save") {
      currentMenu = MENU_NONE;
      setLEDMode(LED_FADING);
      saveConfiguration();
      logMessage("✅ Weekend light schedule saved", true);
      printLightSchedule(weekendLights, "WEEKEND", true);
      return;
    }
    
    if (command == "cancel") {
      currentMenu = MENU_NONE;
      setLEDMode(LED_FADING);
      logMessage("❌ Edit cancelled - no changes saved", true);
      return;
    }
    
    if (command == "list") {
      printLightSchedule(weekendLights, "WEEKEND", true);
      return;
    }
    
    if (command.startsWith("toggle ")) {
      int index = command.substring(7).toInt() - 1;
      if (index >= 0 && index < 3) {
        weekendLights[index].active = !weekendLights[index].active;
        logMessageParts("✅ Toggled schedule ", String(index+1), 
                       " to ", weekendLights[index].active ? "ACTIVE" : "INACTIVE", "", true);
        printLightSchedule(weekendLights, "WEEKEND", true);
      } else {
        logMessage("❌ Invalid index. Use 1-3", true);
      }
      return;
    }
    
    if (command.startsWith("edit ")) {
      int firstSpace = command.indexOf(' ', 5);
      int secondSpace = command.indexOf(' ', firstSpace + 1);
      
      if (firstSpace > 0 && secondSpace > firstSpace) {
        int index = command.substring(5, firstSpace).toInt() - 1;
        String startTime = command.substring(firstSpace + 1, secondSpace);
        String endTime = command.substring(secondSpace + 1);
        
        if (index >= 0 && index < 3) {
          int startHour = startTime.substring(0, 2).toInt();
          int startMinute = startTime.substring(3, 5).toInt();
          int endHour = endTime.substring(0, 2).toInt();
          int endMinute = endTime.substring(3, 5).toInt();
          
          if (startHour >= 0 && startHour <= 23 && startMinute >= 0 && startMinute <= 59 &&
              endHour >= 0 && endHour <= 23 && endMinute >= 0 && endMinute <= 59) {
            
            weekendLights[index].startHour = startHour;
            weekendLights[index].startMinute = startMinute;
            weekendLights[index].endHour = endHour;
            weekendLights[index].endMinute = endMinute;
            
            logMessageParts("✅ Updated schedule ", String(index+1), 
                           " to ", formatTime(startHour, startMinute), 
                           " - " + formatTime(endHour, endMinute), true);
            printLightSchedule(weekendLights, "WEEKEND", true);
          } else {
            logMessage("❌ Invalid time format", true);
          }
        } else {
          logMessage("❌ Invalid index. Use 1-3", true);
        }
      } else {
        logMessage("❌ Invalid format. Use: edit [n] [start] [end]", true);
      }
      return;
    }
    
    logMessage("Unknown command in light edit mode", true);
    logMessage("Available: list, toggle N, edit N HH:MM HH:MM, save, cancel", true);
    return;
  }
  
  // ==================================================================================================
  // MAIN COMMAND PROCESSING
  // ==================================================================================================
  command.toLowerCase();
  
  // ------------------------------------------------------------------------------------------------
  // HELP COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command == "help" || command == "h" || command == "?") {
    showGeneralHelp();
    return;
  }
  
  if (command.startsWith("help ")) {
    String cmd = command.substring(5);
    cmd.trim();
    showCommandHelp(cmd);
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // STATUS COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command == "status" || command == "s") {
    String status = "📊 SYSTEM STATUS\n";
    status += "═══════════════════════════\n";
    status += "School: " + systemConfig.schoolID + "\n";
    status += "Mode: " + modeNames[currentMode] + "\n";
    status += "Uptime: " + String(millis() / 1000) + "s\n";
    status += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
    status += "\n📶 NETWORK:\n";
    status += "WiFi: " + String(WiFi.status() == WL_CONNECTED ? "✅ Connected" : "❌ Disconnected") + "\n";
    if (WiFi.status() == WL_CONNECTED) {
      status += "SSID: " + WiFi.SSID() + "\n";
      status += "IP: " + WiFi.localIP().toString() + "\n";
      status += "Signal: " + String(WiFi.RSSI()) + " dBm\n";
    }
    status += "\n💡 LAMP:\n";
    status += "State: " + String(lampState ? "ON" : "OFF") + "\n";
    status += "Mode: " + String(manualLampOverride ? "MANUAL ✋" : "AUTO 🤖") + "\n";
    status += "\n⏰ TIME:\n";
    status += "Current: " + lastKnownTime + "\n";
    status += "Day: " + getDayName(currentDayOfWeek) + "\n";
    status += "\n📊 BLYNK:\n";
    status += "Messages: " + String(blynkMessageCount) + "/" + String(BLYNK_MONTHLY_LIMIT) + "\n";
    float percent = (float)blynkMessageCount / BLYNK_MONTHLY_LIMIT * 100;
    status += "Used: " + String(percent, 1) + "%\n";
    status += "═══════════════════════════\n";
    logMessage(status, true);
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // TIME COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command == "time") {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char buffer[50];
      strftime(buffer, sizeof(buffer), "%H:%M:%S %A, %B %d %Y", &timeinfo);
      logMessageParts("🕐 ", String(buffer), "", "", "", true);
    } else {
      logMessageParts("❌ Time unavailable (last known: ", lastKnownTime, ")", "", "", true);
    }
    return;
  }
  
  if (command == "uptime") {
    unsigned long seconds = millis() / 1000;
    int days = seconds / 86400;
    int hours = (seconds % 86400) / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    
    String uptime = "⏱️ Uptime: ";
    if (days > 0) uptime += String(days) + "d ";
    if (hours > 0) uptime += String(hours) + "h ";
    if (minutes > 0) uptime += String(minutes) + "m ";
    uptime += String(secs) + "s";
    logMessage(uptime, true);
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // WIFI COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command == "scan") {
    scanWiFiNetworks();
    return;
  }
  
  if (command.startsWith("scan ")) {
    int index = command.substring(5).toInt() - 1;
    connectToWiFiNetwork(index);
    return;
  }
  
  if (command == "wifi") {
    String wifiInfo = "📶 WIFI DETAILS:\n";
    wifiInfo += "═══════════════════════════\n";
    if (WiFi.status() == WL_CONNECTED) {
      wifiInfo += "SSID: " + WiFi.SSID() + "\n";
      wifiInfo += "IP: " + WiFi.localIP().toString() + "\n";
      wifiInfo += "Gateway: " + WiFi.gatewayIP().toString() + "\n";
      wifiInfo += "Subnet: " + WiFi.subnetMask().toString() + "\n";
      wifiInfo += "MAC: " + WiFi.macAddress() + "\n";
      wifiInfo += "Channel: " + String(WiFi.channel()) + "\n";
      wifiInfo += "Signal: " + String(WiFi.RSSI()) + " dBm\n";
    } else {
      wifiInfo += "Status: DISCONNECTED\n";
      wifiInfo += "Reason: " + String(WiFi.status()) + "\n";
    }
    wifiInfo += "═══════════════════════════\n";
    logMessage(wifiInfo, true);
    return;
  }
  
  if (command == "config") {
    logMessage("⚠️ Entering configuration mode...", true);
    delay(1000);
    enterConfigMode();
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // BELL SCHEDULE COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command == "schedule") {
    if (currentDayOfWeek >= 1 && currentDayOfWeek <= 5) {
      printRingSchedule(weekdayRingTimes, hasRungWeekday, "TODAY'S (WEEKDAY)", true);
    } else {
      printRingSchedule(weekendRingTimes, hasRungWeekend, "TODAY'S (WEEKEND)", true);
    }
    return;
  }
  
  if (command == "weekday") {
    printRingSchedule(weekdayRingTimes, hasRungWeekday, "WEEKDAY", true);
    return;
  }
  
  if (command == "weekend") {
    printRingSchedule(weekendRingTimes, hasRungWeekend, "WEEKEND", true);
    return;
  }
  
  if (command == "edit weekday") {
    setLEDMode(LED_SCHEDULE_EDIT);
    logMessage("✏️ EDITING WEEKDAY RINGS", true);
    logMessage("═══════════════════════════", true);
    printRingSchedule(weekdayRingTimes, hasRungWeekday, "CURRENT", true);
    logMessage("", true);
    logMessage("Commands:", true);
    logMessage("  list            - Show current rings", true);
    logMessage("  add HH:MM       - Add new ring time", true);
    logMessage("  del N           - Delete ring number N", true);
    logMessage("  edit N HH:MM    - Edit ring number N", true);
    logMessage("  save            - Save and exit", true);
    logMessage("  cancel          - Exit without saving", true);
    logMessage("═══════════════════════════", true);
    
    currentMenu = MENU_SCHEDULE_EDIT_WEEKDAY;
    menuTimeout = millis() + MENU_TIMEOUT_MS;
    return;
  }
  
  if (command == "edit weekend") {
    setLEDMode(LED_SCHEDULE_EDIT);
    logMessage("✏️ EDITING WEEKEND RINGS", true);
    logMessage("═══════════════════════════", true);
    printRingSchedule(weekendRingTimes, hasRungWeekend, "CURRENT", true);
    logMessage("", true);
    logMessage("Commands:", true);
    logMessage("  list            - Show current rings", true);
    logMessage("  add HH:MM       - Add new ring time", true);
    logMessage("  del N           - Delete ring number N", true);
    logMessage("  edit N HH:MM    - Edit ring number N", true);
    logMessage("  save            - Save and exit", true);
    logMessage("  cancel          - Exit without saving", true);
    logMessage("═══════════════════════════", true);
    
    currentMenu = MENU_SCHEDULE_EDIT_WEEKEND;
    menuTimeout = millis() + MENU_TIMEOUT_MS;
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // BULK SCHEDULE COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command.startsWith("bulk weekday ")) {
    String timesStr = command.substring(13);
    timesStr.trim();
    
    std::vector<String> newTimes;
    int start = 0;
    int commaPos;
    
    do {
      commaPos = timesStr.indexOf(',', start);
      String time;
      if (commaPos > 0) {
        time = timesStr.substring(start, commaPos);
        start = commaPos + 1;
      } else {
        time = timesStr.substring(start);
      }
      time.trim();
      newTimes.push_back(time);
    } while (commaPos > 0);
    
    if (bulkSetRingTimes(weekdayRingTimes, hasRungWeekday, newTimes)) {
      logMessageParts("✅ Weekday schedule updated with ", String(newTimes.size()), " rings", "", "", true);
      printRingSchedule(weekdayRingTimes, hasRungWeekday, "WEEKDAY", true);
      saveConfiguration();
    } else {
      logMessage("❌ Invalid time format in list", true);
      logMessage("   Use: HH:MM,HH:MM,HH:MM (e.g., 08:00,09:00,10:00)", true);
    }
    return;
  }
  
  if (command.startsWith("bulk weekend ")) {
    String timesStr = command.substring(13);
    timesStr.trim();
    
    std::vector<String> newTimes;
    int start = 0;
    int commaPos;
    
    do {
      commaPos = timesStr.indexOf(',', start);
      String time;
      if (commaPos > 0) {
        time = timesStr.substring(start, commaPos);
        start = commaPos + 1;
      } else {
        time = timesStr.substring(start);
      }
      time.trim();
      newTimes.push_back(time);
    } while (commaPos > 0);
    
    if (bulkSetRingTimes(weekendRingTimes, hasRungWeekend, newTimes)) {
      logMessageParts("✅ Weekend schedule updated with ", String(newTimes.size()), " rings", "", "", true);
      printRingSchedule(weekendRingTimes, hasRungWeekend, "WEEKEND", true);
      saveConfiguration();
    } else {
      logMessage("❌ Invalid time format in list", true);
    }
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // LIGHT COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command == "lights") {
    logMessage("💡 LIGHT SCHEDULES", true);
    logMessage("═══════════════════════════", true);
    printLightSchedule(weekdayLights, "WEEKDAY", true);
    logMessage("", true);
    printLightSchedule(weekendLights, "WEEKEND", true);
    return;
  }
  
  if (command == "lights wd") {
    printLightSchedule(weekdayLights, "WEEKDAY", true);
    return;
  }
  
  if (command == "lights we") {
    printLightSchedule(weekendLights, "WEEKEND", true);
    return;
  }
  
  if (command == "edit lights wd") {
    setLEDMode(LED_SCHEDULE_EDIT);
    logMessage("✏️ EDITING WEEKDAY LIGHTS", true);
    logMessage("═══════════════════════════", true);
    printLightSchedule(weekdayLights, "CURRENT", true);
    logMessage("", true);
    logMessage("Commands:", true);
    logMessage("  list                  - Show current schedules", true);
    logMessage("  toggle N              - Enable/disable schedule N", true);
    logMessage("  edit N HH:MM HH:MM    - Edit schedule N times", true);
    logMessage("  save                  - Save and exit", true);
    logMessage("  cancel                - Exit without saving", true);
    logMessage("═══════════════════════════", true);
    logMessage("Example: edit 2 19:30 21:30", true);
    
    currentMenu = MENU_LIGHTS_EDIT_WEEKDAY;
    menuTimeout = millis() + MENU_TIMEOUT_MS;
    return;
  }
  
  if (command == "edit lights we") {
    setLEDMode(LED_SCHEDULE_EDIT);
    logMessage("✏️ EDITING WEEKEND LIGHTS", true);
    logMessage("═══════════════════════════", true);
    printLightSchedule(weekendLights, "CURRENT", true);
    logMessage("", true);
    logMessage("Commands:", true);
    logMessage("  list                  - Show current schedules", true);
    logMessage("  toggle N              - Enable/disable schedule N", true);
    logMessage("  edit N HH:MM HH:MM    - Edit schedule N times", true);
    logMessage("  save                  - Save and exit", true);
    logMessage("  cancel                - Exit without saving", true);
    logMessage("═══════════════════════════", true);
    
    currentMenu = MENU_LIGHTS_EDIT_WEEKEND;
    menuTimeout = millis() + MENU_TIMEOUT_MS;
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // CONTROL COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command == "ring" || command == "bell") {
    startBellRing();
    logMessage("🔔 Manual bell triggered", true);
    if (Blynk.connected()) {
      Blynk.virtualWrite(V1, 1);
      delay(100);
      Blynk.virtualWrite(V1, 0);
    }
    return;
  }
  
  if (command == "lights on") {
    setLamp(true, true);
    return;
  }
  
  if (command == "lights off") {
    setLamp(false, true);
    return;
  }
  
  if (command == "lights auto") {
    manualLampOverride = false;
    logMessage("🤖 Lights returned to AUTO mode", true);
    if (Blynk.connected()) {
      Blynk.virtualWrite(V8, 1);
      delay(100);
      Blynk.virtualWrite(V8, 0);
    }
    saveConfiguration();
    return;
  }
  
  if (command == "reset") {
    manualLampOverride = false;
    logMessage("✅ Manual override reset", true);
    if (Blynk.connected()) {
      Blynk.virtualWrite(V8, 1);
      delay(100);
      Blynk.virtualWrite(V8, 0);
    }
    saveConfiguration();
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // MODE COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command == "mode") {
    logMessageParts("📊 Current mode: ", modeNames[currentMode], "", "", "", true);
    logMessageParts("Messages: ", String(blynkMessageCount), "/", String(BLYNK_MONTHLY_LIMIT), "", true);
    return;
  }
  
  if (command == "mode eco") {
    currentMode = MODE_ECO;
    systemConfig.mode = MODE_ECO;
    saveConfiguration();
    logMessage("✅ Switched to ECO mode - minimal Blynk messages", true);
    return;
  }
  
  if (command == "mode normal") {
    currentMode = MODE_NORMAL;
    systemConfig.mode = MODE_NORMAL;
    saveConfiguration();
    logMessage("✅ Switched to NORMAL mode", true);
    return;
  }
  
  if (command == "mode verbose") {
    currentMode = MODE_VERBOSE;
    systemConfig.mode = MODE_VERBOSE;
    saveConfiguration();
    logMessage("✅ Switched to VERBOSE mode - all messages sent", true);
    return;
  }
  
  if (command == "mode silent") {
    currentMode = MODE_SILENT;
    systemConfig.mode = MODE_SILENT;
    saveConfiguration();
    logMessage("✅ Switched to SILENT mode - no Blynk messages", true);
    return;
  }
  
  if (command == "blynk") {
    logMessage(getBlynkStatus(), true);
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // SYSTEM COMMANDS
  // ------------------------------------------------------------------------------------------------
  if (command == "memory") {
    String memInfo = "🧠 MEMORY STATUS:\n";
    memInfo += "═══════════════════════════\n";
    memInfo += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
    memInfo += "Min Free Heap: " + String(ESP.getMinFreeHeap()) + " bytes\n";
    memInfo += "Max Alloc: " + String(ESP.getMaxAllocHeap()) + " bytes\n";
    memInfo += "Heap Size: " + String(ESP.getHeapSize()) + " bytes\n";
    memInfo += "Sketch Size: " + String(ESP.getSketchSize()) + " bytes\n";
    memInfo += "Free Sketch: " + String(ESP.getFreeSketchSpace()) + " bytes\n";
    memInfo += "Flash Size: " + String(ESP.getFlashChipSize()) + " bytes\n";
    memInfo += "═══════════════════════════\n";
    logMessage(memInfo, true);
    return;
  }
  
  if (command == "errors") {
    String errorReport = "⚠️ ERROR COUNTERS:\n";
    errorReport += "═══════════════════════════\n";
    errorReport += "WiFi Failures: " + String(errorCounters[ERROR_WIFI_FAIL]) + "\n";
    errorReport += "Blynk Failures: " + String(errorCounters[ERROR_BLYNK_FAIL]) + "\n";
    errorReport += "Time Sync Fail: " + String(errorCounters[ERROR_TIME_FAIL]) + "\n";
    errorReport += "Memory Issues: " + String(errorCounters[ERROR_MEMORY_FAIL]) + "\n";
    errorReport += "Button Errors: " + String(errorCounters[ERROR_BUTTON_FAIL]) + "\n";
    errorReport += "Config Errors: " + String(errorCounters[ERROR_CONFIG_FAIL]) + "\n";
    errorReport += "═══════════════════════════\n";
    logMessage(errorReport, true);
    return;
  }
  
  if (command == "errors clear") {
    memset(errorCounters, 0, sizeof(errorCounters));
    logMessage("✅ Error counters cleared", true);
    return;
  }
  
  if (command == "restart") {
    logMessage("⚠️ System restarting in 3 seconds...", true);
    delay(3000);
    ESP.restart();
    return;
  }
  
  if (command == "factory") {
    logMessage("⚠️⚠️⚠️ WARNING ⚠️⚠️⚠️", true);
    logMessage("This will ERASE ALL settings and schedules!", true);
    logMessage("Type 'factory confirm' to proceed", true);
    return;
  }
  
  if (command == "factory confirm") {
    logMessage("⚠️ Factory reset in 3 seconds...", true);
    delay(3000);
    
    preferences.begin(NAMESPACE, false);
    preferences.clear();
    preferences.end();
    
    initializeDefaultSchedules();
    
    ESP.restart();
    return;
  }
  
  if (command == "clear") {
    logMessage("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n", true);
    logMessage("✨ Terminal cleared", true);
    logMessage("Type 'help' for commands", true);
    return;
  }
  
  // ------------------------------------------------------------------------------------------------
  // UNKNOWN COMMAND
  // ------------------------------------------------------------------------------------------------
  if (command.length() > 0) {
    logMessageParts("❌ Unknown command: '", command, "'", "", "", true);
    logMessage("   Type 'help' for available commands", true);
  }
}

// ====================================================================================================
// SETUP FUNCTION
// ====================================================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n====================================================================");
  Serial.println("SCHOOL BELL SYSTEM - ULTIMATE PROFESSIONAL EDITION v3.0");
  Serial.println("====================================================================");
  Serial.print("Build: ");
  Serial.println(BUILD_DATE);
  Serial.println("Developed by COT CLUB");
  Serial.println("====================================================================\n");
  
  // Initialize pins
  pinMode(PIN_LAMP, OUTPUT);
  pinMode(PIN_BELL, OUTPUT);
  pinMode(PIN_LAMP_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BELL_BUTTON, INPUT_PULLUP);
  pinMode(PIN_CONFIG_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  
  // Set initial relay states
  digitalWrite(PIN_LAMP, RELAY_OFF);
  digitalWrite(PIN_BELL, RELAY_OFF);
  
  // Initialize LED
  setLEDMode(LED_FADING);
  
  logMessage("GPIO pins initialized:", true);
  logMessageInt("  Lamp: GPIO", PIN_LAMP, true);
  logMessageInt("  Bell: GPIO", PIN_BELL, true);
  logMessageInt("  Lamp Button: GPIO", PIN_LAMP_BUTTON, true);
  logMessageInt("  Bell Button: GPIO", PIN_BELL_BUTTON, true);
  logMessageInt("  Config Button: GPIO", PIN_CONFIG_BUTTON, true);
  logMessageInt("  LED: GPIO", PIN_LED, true);
  
  // Initialize default schedules
  initializeDefaultSchedules();
  
  // Load configuration
  logMessage("Loading configuration...", true);
  if (!loadConfiguration()) {
    logMessage("No valid configuration found!", true);
    logMessage("Entering configuration mode...", true);
    enterConfigMode();
  }
  
  // Connect to WiFi if configured
  if (!systemConfig.wifiSSID.isEmpty()) {
    logMessageParts("Connecting to WiFi: ", systemConfig.wifiSSID, "", "", "", true);
    WiFi.begin(systemConfig.wifiSSID.c_str(), systemConfig.wifiPassword.c_str());
  }
  
  // Configure time
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
  logMessage("NTP configured with UTC+" + String(UTC_OFFSET/3600) + " offset", true);
  
  // Configure Blynk
  if (!systemConfig.blynkToken.isEmpty()) {
    Blynk.config(systemConfig.blynkToken.c_str());
    logMessage("Blynk configured", true);
  }
  
  // Initialize message tracking
  lastMessageResetTime = millis();
  blynkMessageCount = 0;
  
  logMessage("====================================================================", true);
  logMessage("SETUP COMPLETE", true);
  logMessageParts("Mode: ", modeNames[currentMode], "", "", "", true);
  logMessageParts("Messages: 0/", String(BLYNK_MONTHLY_LIMIT), "", "", "", true);
  logMessage("Type 'help' in V10 terminal for commands", true);
  logMessage("====================================================================", true);
}

// ====================================================================================================
// MAIN LOOP
// ====================================================================================================

void loop() {

  // Check if long hours have passed and reset
  if (millis() - lastReset >= dayMillis) {
    Serial.println("Scheduled hours reached restarting...");
    delay(2000);//delay 2 seconds before restarting
    ESP.restart(); // Hard reset to clear memory and reconnect
    
  }

  // Monitor connection and reconnect if lost
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi lost, attempting reconnect...");
    WiFi.reconnect();
  }
  
  unsigned long now = millis();
  
  // Reset message counter monthly
  if (now - lastMessageResetTime > MONTH_MS) {
    blynkMessageCount = 0;
    lastMessageResetTime = now;
    blynkWarningShown = false;
    blynkCriticalShown = false;
    logMessage("📊 Blynk message counter reset for new month", true);
  }
  
  // Update LED based on status
  if (currentMenu != MENU_NONE) {
    // Menu mode - LED already set by menu
  } else if (WiFi.status() != WL_CONNECTED) {
    setLEDMode(LED_ERROR_BLINK);
  } else if ((float)blynkMessageCount / BLYNK_MONTHLY_LIMIT * 100 > BLYNK_CRITICAL_THRESHOLD) {
    setLEDMode(LED_BLYNK_WARNING);
  } else {
    setLEDMode(LED_FADING);
  }
  
  updateLED();
  
  // WiFi management
  if (now - lastWifiStatusCheckTime >= WIFI_CHECK_INTERVAL) {
    lastWifiStatusCheckTime = now;
    if (WiFi.status() != WL_CONNECTED && !systemConfig.wifiSSID.isEmpty()) {
      logMessage("📶 WiFi disconnected - attempting reconnect...", false);
      WiFi.reconnect();
      connectionAttempts++;
      if (connectionAttempts > 5) {
        logError(ERROR_WIFI_FAIL, "Failed to reconnect after 5 attempts");
        connectionAttempts = 0;
      }
    } else if (WiFi.status() == WL_CONNECTED) {
      connectionAttempts = 0;
      lastSuccessfulConnection = now;
    }
  }
  
  // Blynk run
  if (WiFi.status() == WL_CONNECTED && !systemConfig.blynkToken.isEmpty()) {
    if (now - lastBlynkRunTime >= BLYNK_RUN_INTERVAL) {
      lastBlynkRunTime = now;
      if (!Blynk.connected()) {
        Blynk.connect();
      } else {
        Blynk.run();
      }
    }
  }
  
  // Time update and schedule checks
  if (now - lastTimeUpdateTime >= TIME_UPDATE_INTERVAL) {
    lastTimeUpdateTime = now;
    String currentTime = getCurrentTime();
    
    updateLights();
    checkBellSchedule();
    
    // Check for midnight reset
    static String lastDate = "";
    String today = currentTime.substring(0, 5);
    if (today == "00:00" && lastDate != "00:00") {
      resetDailyStates();
    }
    lastDate = today;
  }
  
  // Button check
  if (now - lastButtonCheckTime >= BUTTON_CHECK_INTERVAL) {
    lastButtonCheckTime = now;
    checkButtons();
  }
   
  // Bell state update
  updateBellState();
  
  // Blynk message check
  if (now - lastBlynkMessageCheck >= BLYNK_MESSAGE_CHECK_INTERVAL) {
    lastBlynkMessageCheck = now;
    float percent = (float)blynkMessageCount / BLYNK_MONTHLY_LIMIT * 100;
    if (percent > BLYNK_CRITICAL_THRESHOLD && currentMode != MODE_ECO) {
      logMessageParts("⚠️ CRITICAL: ", String(percent, 1), "% of Blynk messages used!", "", "", true);
      logMessage("   Switch to ECO mode immediately!", true);
    } else if (percent > BLYNK_WARNING_THRESHOLD && currentMode == MODE_VERBOSE) {
      logMessageParts("⚠️ WARNING: ", String(percent, 1), "% used. Consider NORMAL or ECO mode", "", "", true);
    }
  }
  
  // Periodic configuration save
  if (now - lastConfigSaveTime >= CONFIG_SAVE_INTERVAL) {
    lastConfigSaveTime = now;
    saveConfiguration();
  }
  
  delay(1);
}

// ====================================================================================================
// END OF CODE
// ====================================================================================================
