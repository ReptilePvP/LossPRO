#include <ESP32Time.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include "lv_conf.h"
#include <lvgl.h>
#include <WiFi.h>
#include <Preferences.h>
#include <SPI.h>
SPIClass SPI_SD; // Custom SPI instance for SD card
#include <SD.h>
#include <time.h>

// Debug flag - set to true to enable debug output
#define DEBUG_ENABLED true

// Debug macros
#define DEBUG_PRINT(x) if(DEBUG_ENABLED) { Serial.print(millis()); Serial.print(": "); Serial.println(x); }
#define DEBUG_PRINTF(x, ...) if(DEBUG_ENABLED) { Serial.print(millis()); Serial.print(": "); Serial.printf(x, __VA_ARGS__); }

// SD Card pins for M5Stack CoreS3
#define SD_SPI_SCK_PIN  36
#define SD_SPI_MISO_PIN 35
#define SD_SPI_MOSI_PIN 37
#define SD_SPI_CS_PIN   4
#define TFT_DC 35


// Forward declarations for timer callbacks
void scan_timer_callback(lv_timer_t* timer);
void network_batch_timer_callback(lv_timer_t* timer);
void wifi_btn_event_callback(lv_event_t* e);
void cleanupWiFiResources();

// Function declarations
void initStyles();
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
void createMainMenu();
void updateWifiIndicator();
void addWifiIndicator(lv_obj_t *screen);
void addBatteryIndicator(lv_obj_t *screen);
void updateBatteryIndicator();
void createGenderMenu();
void createColorMenuShirt();
void createColorMenuPants();
void createColorMenuShoes();
void createItemMenu();
void createConfirmScreen();
void scanNetworks();
void showWiFiKeyboard();
void connectToWiFi();
void createWiFiScreen();
void createWiFiManagerScreen();
void loadSavedNetworks();
void saveNetworks();
void connectToSavedNetworks();
String getFormattedEntry(const String& entry);
String getTimestamp();
bool appendToLog(const String& entry);
void createViewLogsScreen();
void initFileSystem();
void syncTimeWithNTP();
void listSavedEntries();
void saveEntry(const String& entry);
void updateStatus(const char* message, uint32_t color);
void sendWebhook(const String& entry);

// Global variables for scrolling
lv_obj_t *current_scroll_obj = nullptr;
const int SCROLL_AMOUNT = 40;  // Pixels to scroll per button press

// Global variables for color selection
String selectedShirtColors = "";
String selectedPantsColors = "";
String selectedShoesColors = "";

ESP32Time rtc; // ESP32Time object to manage RTC

// WiFi connection management
unsigned long lastWiFiConnectionAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000; // 10 seconds between connection attempts
const int MAX_WIFI_CONNECTION_ATTEMPTS = 5; // Maximum number of consecutive connection attempts
int wifiConnectionAttempts = 0;
bool wifiReconnectEnabled = true;

// WiFi scan management
bool wifiScanInProgress = false;
unsigned long lastScanStartTime = 0;
const unsigned long SCAN_TIMEOUT = 10000; // 10 seconds timeout for WiFi scan

// Global variables for network processing
static int currentBatch = 0;
static int totalNetworksFound = 0;
static lv_obj_t* g_spinner = nullptr; // Global spinner object
static lv_timer_t* scan_timer = nullptr; // Global scan timer

// Current entry for loss prevention logging
static String currentEntry = "";

// Declare the Montserrat font
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);

// LVGL Refresh time
static const uint32_t screenTickPeriod = 10;  // Increased to 10ms for better stability
static uint32_t lastLvglTick = 0;

// For log pagination
static std::vector<String> logEntries;
static int currentLogPage = 0;
static int totalLogPages = 0;
static const int LOGS_PER_PAGE = 8;

// Status bar
static lv_obj_t* status_bar = nullptr;

// Screen dimensions for CoreS3
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Display buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_WIDTH * 20];  // Increased buffer size

// Display and input drivers
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// Default WiFi credentials (will be overridden by saved networks)
const char DEFAULT_SSID[] = "Wack House";
const char DEFAULT_PASS[] = "justice69";

//Discord webhook
const char* WEBHOOK_URL = "https://discord.com/api/webhooks/1346971779553300542/f1jn7qgqOQ86b99mljWFr0-L98gy-iwfJxYJOKvkeq0iUbdoLyc2Z22S5RP7S5nVTlwh";

// WiFi network management
#define MAX_NETWORKS 5

struct WifiNetwork {
    char ssid[33];
    char password[65];
};

// Structure to hold parsed log entry with timestamp
struct LogEntry {
    String text;
    time_t timestamp;
};

time_t parseTimestamp(const String& entry) {
    struct tm timeinfo = {0};
    int day, year, hour, minute, second;
    char monthStr[4]; // For 3-letter month abbreviation + null terminator

    // Expecting format: "DD-MMM-YYYY HH:MM:SS" (e.g., "05-Mar-2025 08:12:24")
    if (sscanf(entry.c_str(), "%d-%3s-%d %d:%d:%d", &day, monthStr, &year, &hour, &minute, &second) == 6) {
        timeinfo.tm_mday = day;
        timeinfo.tm_year = year - 1900; // Years since 1900
        timeinfo.tm_hour = hour;
        timeinfo.tm_min = minute;
        timeinfo.tm_sec = second;
        timeinfo.tm_isdst = -1; // Let mktime figure out DST

        // Convert 3-letter month abbreviation to month number (0-11)
        static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        for (int i = 0; i < 12; i++) {
            if (strncmp(monthStr, months[i], 3) == 0) {
                timeinfo.tm_mon = i;
                break;
            }
        }

        return mktime(&timeinfo);
    }
    return 0; // Invalid timestamp
}

void setSystemTimeFromRTC() {
    m5::rtc_date_t DateStruct;
    m5::rtc_time_t TimeStruct;
    M5.Rtc.getDate(&DateStruct);
    M5.Rtc.getTime(&TimeStruct);

    struct tm timeinfo = {0};
    timeinfo.tm_year = DateStruct.year - 1900; // Years since 1900
    timeinfo.tm_mon = DateStruct.month - 1;     // Months 0-11
    timeinfo.tm_mday = DateStruct.date;
    timeinfo.tm_hour = TimeStruct.hours;
    timeinfo.tm_min = TimeStruct.minutes;
    timeinfo.tm_sec = TimeStruct.seconds;
    timeinfo.tm_isdst = -1; // Let mktime figure out DST

    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    DEBUG_PRINT("System time set from RTC");
}
// Global vector for parsed log entries
static std::vector<LogEntry> parsedLogEntries;

// Add these near other global LVGL object declarations
lv_obj_t* settingsScreen = nullptr;
bool wifiEnabled = true; // Already suggested in battery optimization; reusing here
uint8_t displayBrightness = 128; // Current brightness (0-255)

static WifiNetwork savedNetworks[MAX_NETWORKS];
static int numSavedNetworks = 0;
static bool wifiConfigured = false;
bool manualDisconnect = false; // Flag to track if disconnection was user-initiated

// Touch tracking inspired by touch.ino
static bool touch_active = false;
static int16_t touch_start_x = 0;
static int16_t touch_start_y = 0;
static int16_t touch_last_x = 0;
static int16_t touch_last_y = 0;
static unsigned long touch_start_time = 0;
static bool was_touching = false;  // Add this line
const int TOUCH_SWIPE_THRESHOLD = 30;  // Pixels for a swipe
const int TOUCH_MAX_SWIPE_TIME = 700;  // Max time (ms) for a swipe

// Menu options
const char* genders[] = {"Male", "Female"};

// Global next button pointers
lv_obj_t* shirt_next_btn = nullptr;
lv_obj_t* pants_next_btn = nullptr;
lv_obj_t* shoes_next_btn = nullptr;

const char* shirtColors[] = {
    "Red", "Orange", "Yellow", "Green", "Blue", "Purple", 
    "Black", "White"
};

const char* pantsColors[] = {
    "Black", "Blue", "Grey", "Khaki", "Brown", "Navy",
    "White", "Beige"
};

const char* shoeColors[] = {
    "Black", "Brown", "White", "Grey", "Navy", "Red",
    "Blue", "Green"
};

const char* items[] = {"Jewelry", "Women's Shoes", "Men's Shoes", "Cosmetics", "Fragrances", "Home", "Kids"};

// LVGL objects
lv_obj_t *mainScreen = nullptr, *genderMenu = nullptr, *colorMenu = nullptr, *itemMenu = nullptr, *confirmScreen = nullptr;
lv_obj_t *wifiIndicator = nullptr;
lv_obj_t *batteryIndicator = nullptr;
lv_obj_t *wifi_screen = nullptr;
lv_obj_t *wifi_manager_screen = nullptr;
lv_obj_t *wifi_list = nullptr;
lv_obj_t *wifi_status_label = nullptr;
lv_obj_t *saved_networks_list = nullptr;

// WiFi UI components
static lv_obj_t* wifi_keyboard = nullptr;

// Battery monitoring
static int lastBatteryLevel = -1;
static unsigned long lastBatteryReadTime = 0;
const unsigned long BATTERY_READ_INTERVAL = 30000; // Read battery level every 30 seconds

static char selected_ssid[33] = ""; // Max SSID length is 32 characters + null terminator
static char selected_password[65] = ""; // Max WPA2 password length is 64 characters + null terminator
static Preferences preferences;

// Styles
static lv_style_t style_screen, style_btn, style_btn_pressed, style_title, style_text;
// Add this new style for keyboard buttons
static lv_style_t style_keyboard_btn;

// Add these global variables and keyboard definitions
static int keyboard_page_index = 0;

// Control map for button matrix
const lv_btnmatrix_ctrl_t keyboard_ctrl_map[] = {
    4, 4, 4, 
    4, 4, 4, 
    4, 4, 4, 
    3, 7, 7, 7  // Changed last row controls for special keys
};

// Keyboard maps for different pages
const char *btnm_mapplus[11][23] = {
    { "a", "b", "c", "\n",
      "d", "e", "f", "\n",
      "g", "h", "i", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "j", "k", "l", "\n",
      "m", "n", "o", "\n",
      "p", "q", "r", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "s", "t", "u", "\n",
      "v", "w", "x", "\n",
      "y", "z", " ", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "A", "B", "C", "\n",
      "D", "E", "F", "\n",
      "G", "H", "I", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "J", "K", "L", "\n",
      "N", "M", "O", "\n",
      "P", "Q", "R", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "S", "T", "U", "\n",
      "V", "W", "X", "\n",
      "Y", "Z", " ", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "1", "2", "3", "\n",
      "4", "5", "6", "\n",
      "7", "8", "9", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "0", "+", "-", "\n",
      "/", "*", "=", "\n",
      "!", "?", " ", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "<", ">", "@", "\n",
      "%", "$", "(", "\n",
      ")", "{", "}", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "[", "]", ";", "\n",
      "\"", "'", ".", "\n",
      ",", ":", " ", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" },
    { "\\", "_", "~", "\n",
      "|", "&", "^", "\n",
      "`", "#", " ", "\n",
      LV_SYMBOL_OK, LV_SYMBOL_BACKSPACE, LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" }
  };


// Number of keyboard pages
const int NUM_KEYBOARD_PAGES = sizeof(btnm_mapplus) / sizeof(btnm_mapplus[0]);

// File system settings
#define LOG_FILENAME "/loss_prevention_log.txt"
bool fileSystemInitialized = false;

// Time settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;  // Eastern Time -5 hours
const int daylightOffset_sec = 3600; // 1 hour DST

//Function to sync time with NTP
void syncTimeWithNTP() {
    configTime(gmtOffset_sec / 3600, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        DEBUG_PRINT("NTP time synchronized");
        // Update RTC with NTP time
        m5::rtc_time_t TimeStruct;
        TimeStruct.hours = timeinfo.tm_hour;
        TimeStruct.minutes = timeinfo.tm_min;
        TimeStruct.seconds = timeinfo.tm_sec;
        M5.Rtc.setTime(&TimeStruct);

        m5::rtc_date_t DateStruct;
        DateStruct.year = timeinfo.tm_year + 1900;
        DateStruct.month = timeinfo.tm_mon + 1;
        DateStruct.date = timeinfo.tm_mday;
        DateStruct.weekDay = timeinfo.tm_wday;
        M5.Rtc.setDate(&DateStruct);

        // Update system time after RTC sync
        setSystemTimeFromRTC();
        DEBUG_PRINT("RTC and system time updated with NTP");
    } else {
        DEBUG_PRINT("Failed to sync with NTP");
    }
}
// Function to release SPI bus
void releaseSPIBus() {
    SPI.end();
    delay(100);
}

// Function to initialize file system
void initFileSystem() {
    if (fileSystemInitialized) {
        DEBUG_PRINT("File system already initialized");
        return;
    }
    
    DEBUG_PRINT("Initializing SD card...");
    
    // Stop the default SPI (used by LCD)
    SPI.end();
    delay(200); // Give it a moment to settle
    
    // Start custom SPI for SD card
    SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, -1); // -1 means no default CS
    pinMode(SD_SPI_CS_PIN, OUTPUT); // Set CS pin (4) as OUTPUT
    digitalWrite(SD_SPI_CS_PIN, HIGH); // Deselect SD card (HIGH = off)
    delay(200); // Wait for SD card to stabilize
    
    bool sdInitialized = false;
    
    // Try initializing the SD card up to 3 times
    for (int i = 0; i < 3 && !sdInitialized; i++) {
        DEBUG_PRINTF("Attempt %d: Initializing SD at 2 MHz...\n", i + 1);
        sdInitialized = SD.begin(SD_SPI_CS_PIN, SPI_SD, 2000000); // CS=4, custom SPI, 2 MHz
        if (!sdInitialized) {
            DEBUG_PRINTF("Attempt %d failed\n", i + 1);
            delay(100); // Short delay before retrying
        }
    }
    
    if (!sdInitialized) {
        DEBUG_PRINT("All SD card initialization attempts failed");
        // Show error message on screen
        lv_obj_t* msgbox = lv_msgbox_create(NULL, "Storage Error", 
            "SD card initialization failed. Please check the card.", NULL, true);
        lv_obj_set_size(msgbox, 280, 150);
        lv_obj_center(msgbox);
        
        // Clean up and switch back to LCD mode
        SPI_SD.end();
        SPI.begin();
        pinMode(TFT_DC, OUTPUT);
        digitalWrite(TFT_DC, HIGH);
        return;
    }
    
    DEBUG_PRINT("SD card initialized successfully");
    fileSystemInitialized = true;
    
    // Check card type and print info
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        DEBUG_PRINT("No SD card attached");
    } else {
        DEBUG_PRINT("SD Card Type: ");
        if (cardType == CARD_MMC) {
            DEBUG_PRINT("MMC");
        } else if (cardType == CARD_SD) {
            DEBUG_PRINT("SDSC");
        } else if (cardType == CARD_SDHC) {
            DEBUG_PRINT("SDHC");
        } else {
            DEBUG_PRINT("UNKNOWN");
        }
        
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        DEBUG_PRINTF("SD Card Size: %lluMB\n", cardSize);
    }
    
    // Switch back to LCD mode after initialization
    SPI_SD.end();
    SPI.begin();
    pinMode(TFT_DC, OUTPUT);
    digitalWrite(TFT_DC, HIGH);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("Serial initialized"); // Confirm serial works
    DEBUG_PRINT("Starting Loss Prevention Log...");

    auto cfg = M5.config();
    Serial.println("Before M5.begin"); // Check before hardware init
    M5.begin(cfg);
    Serial.println("After M5.begin"); // Confirm hardware init

    M5.Display.setBrightness(displayBrightness);
    Serial.println("Display brightness set");
    M5.Display.clear();
    DEBUG_PRINT("Display configured");

    Serial.println("Before lv_init");
    lv_init();
    Serial.println("After lv_init");

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * 5);
    Serial.println("Display buffer initialized");

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = M5.Display.width();
    disp_drv.ver_res = M5.Display.height();
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    Serial.println("Display driver registered");

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
    Serial.println("Input driver registered");

    m5::rtc_date_t DateStruct;
    m5::rtc_time_t TimeStruct;
    Serial.println("Before RTC getDate");
    M5.Rtc.getDate(&DateStruct);
    Serial.println("After RTC getDate");
    if (DateStruct.year < 2020) {
        int day, month, year, hour, minute, second;
        char monthStr[4];
        sscanf(__DATE__, "%3s %2d %4d", monthStr, &day, &year);
        sscanf(__TIME__, "%2d:%2d:%2d", &hour, &minute, &second);

        static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        for (int i = 0; i < 12; i++) {
            if (strncmp(monthStr, months[i], 3) == 0) {
                month = i + 1;
                break;
            }
        }

        rtc.setTime(second, minute, hour, day, month, year);
        int y = year, m = month, d = day;
        if (m < 3) { m += 12; y--; }
        int k = d, M = m, D = y % 100, C = y / 100;
        int f = k + ((13 * (M + 1)) / 5) + D + (D / 4) + (C / 4) - (2 * C);
        int weekDay = (f % 7 + 7) % 7;

        TimeStruct.hours = hour;
        TimeStruct.minutes = minute;
        TimeStruct.seconds = second;
        M5.Rtc.setTime(&TimeStruct);
        DateStruct.year = year;
        DateStruct.month = month;
        DateStruct.date = day;
        DateStruct.weekDay = weekDay;
        M5.Rtc.setDate(&DateStruct);

        DEBUG_PRINTF("RTC set to compile time: %04d-%02d-%02d %02d:%02d:%02d, Weekday: %d\n",
                     year, month, day, hour, minute, second, weekDay);
    }
    setSystemTimeFromRTC();
    Serial.println("System time set");

    initStyles();
    createMainMenu();
    lastLvglTick = millis();

    DEBUG_PRINTF("Battery Voltage: %f V\n", M5.Power.getBatteryVoltage());
    DEBUG_PRINTF("Is Charging: %d\n", M5.Power.isCharging());
    DEBUG_PRINTF("Battery Level: %d%%\n", M5.Power.getBatteryLevel());
    DEBUG_PRINT("Setup complete!");
}

void loop() {
    M5.update();  // Update M5CoreS3 hardware state (touch, buttons, etc.)
    uint32_t currentMillis = millis();

    // Handle LVGL timing (10ms tick period from your code)
    if (currentMillis - lastLvglTick > screenTickPeriod) {
        lv_timer_handler();  // Process LVGL events and updates
        lastLvglTick = currentMillis;
    }
    
    // Update indicators periodically (every 5 seconds)
    static unsigned long lastIndicatorUpdate = 0;
    if (currentMillis - lastIndicatorUpdate > 5000) {
        updateWifiIndicator();
        updateBatteryIndicator();
        lastIndicatorUpdate = currentMillis;
    }

    // WiFi scan timeout check
    if (wifiScanInProgress && (currentMillis - lastScanStartTime > SCAN_TIMEOUT)) {
        DEBUG_PRINT("WiFi scan timeout detected");
        wifiScanInProgress = false;
        if (wifi_status_label && lv_scr_act() == wifi_screen) {
            lv_label_set_text(wifi_status_label, "Scan timed out. Try again.");
        }
    }

    // WiFi reconnection logic
    if (wifiReconnectEnabled && WiFi.status() != WL_CONNECTED && !manualDisconnect &&
        currentMillis - lastWiFiConnectionAttempt > WIFI_RECONNECT_INTERVAL) {
        if (wifiConnectionAttempts < MAX_WIFI_CONNECTION_ATTEMPTS) {
            DEBUG_PRINTF("WiFi.status(): %d\n", WiFi.status());
            DEBUG_PRINTF("Retrying connection in %d milliseconds\n", WIFI_RECONNECT_INTERVAL);
            if (numSavedNetworks > 0) {
                WiFi.disconnect();
                delay(100);
                WiFi.begin(savedNetworks[0].ssid, savedNetworks[0].password);  // Try first saved network
                lastWiFiConnectionAttempt = currentMillis;
                wifiConnectionAttempts++;
            }
        } else {
            DEBUG_PRINT("Max WiFi connection attempts reached. Pausing reconnection.");
            if (currentMillis - lastWiFiConnectionAttempt > WIFI_RECONNECT_INTERVAL * 6) {
                wifiConnectionAttempts = 0;  // Reset after 60s pause
            }
        }
    }

    // Reset connection attempts when connected
    if (WiFi.status() == WL_CONNECTED && wifiConnectionAttempts > 0) {
        wifiConnectionAttempts = 0;
        manualDisconnect = false;
        DEBUG_PRINT("WiFi connected successfully");
        
        static bool timeSynced = false;
        if (!timeSynced) {
            syncTimeWithNTP();
            timeSynced = true;
        }
    }

    // Periodic NTP sync (every hour)
    static unsigned long lastTimeSync = 0;
    if (WiFi.status() == WL_CONNECTED && currentMillis - lastTimeSync > 3600000) {
        syncTimeWithNTP();
        lastTimeSync = currentMillis;
    }

    delay(5);  // Small delay for stability, matching your original
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    M5.Display.startWrite();
    M5.Display.setAddrWindow(area->x1, area->y1, w, h);
    M5.Display.pushPixels((uint16_t *)color_p, w * h); // Replace pushColors with pushPixels
    M5.Display.endWrite();
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    m5::touch_detail_t t = M5.Touch.getDetail();
    if (t.state == m5::touch_state_t::touch) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = t.x;
        data->point.y = t.y;

        if (!was_touching) {
            touch_start_x = t.x;
            touch_start_y = t.y;
            touch_start_time = millis();
            was_touching = true;
        }
        touch_last_x = t.x;
        touch_last_y = t.y;
    } else if (was_touching) {
        data->state = LV_INDEV_STATE_REL;
        was_touching = false;

        int dx = touch_last_x - touch_start_x;
        int dy = touch_last_y - touch_start_y;
        int abs_dx = abs(dx);
        int abs_dy = abs(dy);
        unsigned long duration = millis() - touch_start_time;

        if (duration < TOUCH_MAX_SWIPE_TIME) {
            if (abs_dx > TOUCH_SWIPE_THRESHOLD && abs_dx > abs_dy) {
                if (dx < 0) handleSwipeLeft();
            } else if (abs_dy > TOUCH_SWIPE_THRESHOLD && abs_dy > abs_dx) {
                handleSwipeVertical(dy < 0 ? -SCROLL_AMOUNT : SCROLL_AMOUNT);
            }
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void handleSwipeLeft() {
    lv_obj_t* current_screen = lv_scr_act();
    
    if (current_screen == wifi_screen) {
        DEBUG_PRINT("Swiping back to WiFi Manager from WiFi Screen");
        cleanupWiFiResources();
        lv_obj_t* screen_to_delete = wifi_screen;
        wifi_screen = nullptr;
        if (screen_to_delete != nullptr) {
            lv_obj_del(screen_to_delete);
        }
        createWiFiManagerScreen();
    } else if (current_screen == wifi_manager_screen) {
        DEBUG_PRINT("Swiping back to main menu from WiFi Manager");
        createMainMenu();
    } else if (current_screen == genderMenu) {
        DEBUG_PRINT("Swiping back to main menu from gender menu");
        createMainMenu();
    } else if (current_screen == colorMenu) {
        if (shoes_next_btn != nullptr) {
            DEBUG_PRINT("Swiping back to pants menu from shoes menu");
            createColorMenuPants();
        } else if (pants_next_btn != nullptr) {
            DEBUG_PRINT("Swiping back to shirt menu from pants menu");
            createColorMenuShirt();
        } else {
            DEBUG_PRINT("Swiping back to gender menu from shirt menu");
            createGenderMenu();
        }
    } else if (current_screen == itemMenu) {
        DEBUG_PRINT("Swiping back to shoes menu from item menu");
        createColorMenuShoes();
    } else if (current_screen == confirmScreen) {
        DEBUG_PRINT("Swiping back to item menu from confirm screen");
        createItemMenu();
    }

    if (current_screen && current_screen != lv_scr_act()) {
        DEBUG_PRINTF("Cleaning old screen: %p\n", current_screen);
        lv_obj_del_async(current_screen);
    }
}

void handleSwipeVertical(int amount) {
    if (current_scroll_obj && lv_obj_is_valid(current_scroll_obj)) {
        lv_obj_scroll_by(current_scroll_obj, 0, amount, LV_ANIM_ON);
        DEBUG_PRINTF("Scrolled %p by %d pixels\n", current_scroll_obj, amount);
        lv_obj_invalidate(current_scroll_obj); // Force UI refresh
    } else {
        DEBUG_PRINT("No valid scrollable object");
    }
}

void addStatusBar(lv_obj_t* screen) {
    if (status_bar) lv_obj_del(status_bar);
    status_bar = lv_label_create(screen);
    lv_obj_set_style_text_font(status_bar, &lv_font_montserrat_14, 0);
    lv_obj_align(status_bar, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_color(status_bar, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(status_bar, "Ready");
}

void createMainMenu() {
    // Clean up all existing screens to prevent memory leaks
    if (mainScreen && lv_obj_is_valid(mainScreen)) {
        DEBUG_PRINT("Cleaning existing main screen");
        lv_obj_del_async(mainScreen);
        mainScreen = nullptr;
    }
    
    if (wifi_screen && lv_obj_is_valid(wifi_screen)) {
        DEBUG_PRINT("Cleaning existing wifi screen");
        cleanupWiFiResources();
        lv_obj_del_async(wifi_screen);
        wifi_screen = nullptr;
    }
    
    if (wifi_manager_screen && lv_obj_is_valid(wifi_manager_screen)) {
        DEBUG_PRINT("Cleaning existing wifi manager screen");
        lv_obj_del_async(wifi_manager_screen);
        wifi_manager_screen = nullptr;
    }
    
    if (wifi_keyboard && lv_obj_is_valid(wifi_keyboard)) {
        DEBUG_PRINT("Cleaning existing wifi keyboard");
        lv_obj_del_async(wifi_keyboard);
        wifi_keyboard = nullptr;
    }
    
    if (genderMenu && lv_obj_is_valid(genderMenu)) {
        DEBUG_PRINT("Cleaning existing gender menu");
        lv_obj_del_async(genderMenu);
        genderMenu = nullptr;
    }
    
    if (colorMenu && lv_obj_is_valid(colorMenu)) {
        DEBUG_PRINT("Cleaning existing color menu");
        lv_obj_del_async(colorMenu);
        colorMenu = nullptr;
    }
    
    if (itemMenu && lv_obj_is_valid(itemMenu)) {
        DEBUG_PRINT("Cleaning existing item menu");
        lv_obj_del_async(itemMenu);
        itemMenu = nullptr;
    }
    
    if (confirmScreen && lv_obj_is_valid(confirmScreen)) {
        DEBUG_PRINT("Cleaning existing confirm screen");
        lv_obj_del_async(confirmScreen);
        confirmScreen = nullptr;
    }
    // Reset Nxt buttons
    shirt_next_btn = nullptr;
    pants_next_btn = nullptr;
    shoes_next_btn = nullptr;

    // Create new main screen
    mainScreen = lv_obj_create(NULL);
    if (!mainScreen) {
        DEBUG_PRINT("Failed to create main screen!");
        return;
    }
    
    mainScreen = lv_obj_create(NULL);
    if (!mainScreen) {
        DEBUG_PRINT("Failed to create main screen!");
        return;
    }
    
    lv_obj_add_style(mainScreen, &style_screen, 0);
    lv_scr_load(mainScreen);
    addStatusBar(mainScreen);
    addWifiIndicator(mainScreen);
    addBatteryIndicator(mainScreen);
    
    lv_obj_t* header = lv_obj_create(mainScreen);
    lv_obj_set_size(header, 320, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Loss Prevention");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* btn_new = lv_btn_create(mainScreen);
    lv_obj_set_size(btn_new, 280, 60);
    lv_obj_align(btn_new, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_style(btn_new, &style_btn, 0);
    lv_obj_add_style(btn_new, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* label_new = lv_label_create(btn_new);
    lv_label_set_text(label_new, "New Entry");
    lv_obj_center(label_new);
    lv_obj_add_event_cb(btn_new, [](lv_event_t* e) { createGenderMenu(); }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_wifi = lv_btn_create(mainScreen);
    lv_obj_set_size(btn_wifi, 280, 60);
    lv_obj_align(btn_wifi, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_add_style(btn_wifi, &style_btn, 0);
    lv_obj_add_style(btn_wifi, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* label_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(label_wifi, "Wi-Fi Settings");
    lv_obj_center(label_wifi);
    lv_obj_add_event_cb(btn_wifi, [](lv_event_t* e) { createWiFiManagerScreen(); }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_logs = lv_btn_create(mainScreen);
    lv_obj_set_size(btn_logs, 280, 60);
    lv_obj_align(btn_logs, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_add_style(btn_logs, &style_btn, 0);
    lv_obj_add_style(btn_logs, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* label_logs = lv_label_create(btn_logs);
    lv_label_set_text(label_logs, "View Logs");
    lv_obj_center(label_logs);
    lv_obj_add_event_cb(btn_logs, [](lv_event_t* e) { createViewLogsScreen(); }, LV_EVENT_CLICKED, NULL);

    // Settings Button
    lv_obj_t* btn_settings = lv_btn_create(mainScreen);
    lv_obj_set_size(btn_settings, 280, 60);
    lv_obj_align(btn_settings, LV_ALIGN_TOP_MID, 0, 270); // Adjusted to fit 4 buttons
    lv_obj_add_style(btn_settings, &style_btn, 0);
    lv_obj_add_style(btn_settings, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* label_settings = lv_label_create(btn_settings);
    lv_label_set_text(label_settings, "Settings");
    lv_obj_center(label_settings);
    lv_obj_add_event_cb(btn_settings, [](lv_event_t* e) { createSettingsMenu(); }, LV_EVENT_CLICKED, NULL);

    lv_timer_handler();
}

void updateStatus(const char* message, uint32_t color = 0xFFFFFF) {
    if (status_bar) {
        lv_label_set_text(status_bar, message);
        lv_obj_set_style_text_color(status_bar, lv_color_hex(color), 0);
    }
}

void updateWifiIndicator() {
    if (!wifiIndicator) return;
    
    int wifiStatus = WiFi.status();
    
    if (wifiStatus == WL_CONNECTED) {
        // Show signal strength with different colors based on RSSI
        int rssi = WiFi.RSSI();
        if (rssi > -60) {
            // Strong signal (green)
            lv_label_set_text(wifiIndicator, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0x00FF00), 0);
        } else if (rssi > -70) {
            // Medium signal (yellow)
            lv_label_set_text(wifiIndicator, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0xFFFF00), 0);
        } else {
            // Weak signal (orange)
            lv_label_set_text(wifiIndicator, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0xFFA500), 0);
        }
    } else if (wifiStatus == WL_IDLE_STATUS) {
        // WiFi is in idle status (blue)
        lv_label_set_text(wifiIndicator, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0x0000FF), 0);
    } else if (wifiStatus == WL_DISCONNECTED || wifiStatus == WL_CONNECTION_LOST) {
        // WiFi is disconnected but trying to reconnect (orange)
        lv_label_set_text(wifiIndicator, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0xFF6600), 0);
    } else {
        // Not connected (red X)
        lv_label_set_text(wifiIndicator, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0xFF0000), 0);
    }
    
    lv_obj_move_foreground(wifiIndicator);
    lv_obj_invalidate(wifiIndicator);
}

void addWifiIndicator(lv_obj_t *screen) {
    if (wifiIndicator != nullptr) {
        lv_obj_del(wifiIndicator);
        wifiIndicator = nullptr;
    }
    wifiIndicator = lv_label_create(screen);
    lv_obj_set_style_text_font(wifiIndicator, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(wifiIndicator, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(wifiIndicator, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_set_style_bg_opa(wifiIndicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_pad_all(wifiIndicator, 0, 0);
    lv_obj_set_size(wifiIndicator, 32, 32);
    updateWifiIndicator();
}

void addBatteryIndicator(lv_obj_t *screen) {
    if (batteryIndicator != nullptr) {
        lv_obj_del(batteryIndicator);
        batteryIndicator = nullptr;
    }
    batteryIndicator = lv_label_create(screen);
    lv_obj_set_style_text_font(batteryIndicator, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(batteryIndicator, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(batteryIndicator, LV_ALIGN_TOP_LEFT, 10, 5);
    lv_obj_set_style_bg_opa(batteryIndicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(batteryIndicator, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(batteryIndicator, 0, 0);
    lv_obj_set_size(batteryIndicator, 100, 32);
    
    // Always update immediately when creating the indicator
    updateBatteryIndicator();
}

void updateBatteryIndicator() {
    if (!batteryIndicator) return;
    
    unsigned long currentTime = millis();
    int batteryLevel;
    
    // Only read the battery level at specified intervals
    if (lastBatteryLevel == -1 || (currentTime - lastBatteryReadTime) >= BATTERY_READ_INTERVAL) {
        batteryLevel = M5.Power.getBatteryLevel();
        
        // Apply smoothing - don't allow drops of more than 1% at a time unless significant time has passed
        if (lastBatteryLevel != -1) {
            if (batteryLevel < lastBatteryLevel) {
                // If battery level is dropping, limit the rate of change
                long timeSinceLastRead = currentTime - lastBatteryReadTime;
                int maxAllowedDrop = 1 + (timeSinceLastRead / 60000); // Allow 1% drop per minute
                
                if (batteryLevel < lastBatteryLevel - maxAllowedDrop) {
                    batteryLevel = lastBatteryLevel - maxAllowedDrop;
                }
            } else if (batteryLevel > lastBatteryLevel + 5 && !M5.Power.isCharging()) {
                // If battery level is increasing by more than 5% and not charging, limit the increase
                batteryLevel = lastBatteryLevel + 1;
            }
        }
        
        lastBatteryLevel = batteryLevel;
        lastBatteryReadTime = currentTime;
    } else {
        // Use the cached value
        batteryLevel = lastBatteryLevel;
    }
    
    bool isCharging = M5.Power.isCharging();
    
    // Choose color based on battery level
    uint32_t batteryColor;
    if (batteryLevel > 60) {
        batteryColor = 0x00FF00; // Green for good battery
    } else if (batteryLevel > 20) {
        batteryColor = 0xFFFF00; // Yellow for medium battery
    } else {
        batteryColor = 0xFF0000; // Red for low battery
    }
    
    // If charging, use a different color
    if (isCharging) {
        batteryColor = 0x00FFFF; // Cyan for charging
    }
    
    // Set the battery text with appropriate icon
    String batteryText;
    if (isCharging) {
        batteryText = LV_SYMBOL_CHARGE " " + String(batteryLevel) + "%";
    } else if (batteryLevel <= 10) {
        batteryText = LV_SYMBOL_BATTERY_EMPTY " " + String(batteryLevel) + "%";
    } else if (batteryLevel <= 30) {
        batteryText = LV_SYMBOL_BATTERY_1 " " + String(batteryLevel) + "%";
    } else if (batteryLevel <= 60) {
        batteryText = LV_SYMBOL_BATTERY_2 " " + String(batteryLevel) + "%";
    } else if (batteryLevel <= 80) {
        batteryText = LV_SYMBOL_BATTERY_3 " " + String(batteryLevel) + "%";
    } else {
        batteryText = LV_SYMBOL_BATTERY_FULL " " + String(batteryLevel) + "%";
    }
    
    lv_label_set_text(batteryIndicator, batteryText.c_str());
    lv_obj_set_style_text_color(batteryIndicator, lv_color_hex(batteryColor), 0);
    lv_obj_move_foreground(batteryIndicator);
    lv_obj_invalidate(batteryIndicator);
}

void createGenderMenu() {
    if (genderMenu) {
        DEBUG_PRINT("Cleaning existing gender menu");
        lv_obj_del(genderMenu);
        genderMenu = nullptr;
    }
    genderMenu = lv_obj_create(NULL);
    lv_obj_add_style(genderMenu, &style_screen, 0);
    lv_scr_load(genderMenu);
    DEBUG_PRINTF("Gender menu created: %p\n", genderMenu);
    
    // Force immediate update of indicators
    addWifiIndicator(genderMenu);
    addBatteryIndicator(genderMenu);
    lv_timer_handler(); // Process any pending UI updates
    
    lv_obj_t *header = lv_obj_create(genderMenu);
    lv_obj_set_size(header, 320, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Select Gender");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *btn = lv_btn_create(genderMenu);
        lv_obj_set_size(btn, 280, 80);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 60 + i * 90);
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, genders[i]);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            currentEntry = String(lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0))) + ",";
            delay(100); // Small delay for stability
            createColorMenuShirt();
        }, LV_EVENT_CLICKED, NULL);
    }

    // Add Back button
    lv_obj_t *back_btn = lv_btn_create(genderMenu);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        delay(100);
        DEBUG_PRINT("Returning to main menu");
        createMainMenu();
        if (genderMenu && genderMenu != lv_scr_act()) {
            DEBUG_PRINTF("Cleaning old gender menu: %p\n", genderMenu);
            lv_obj_del_async(genderMenu);
            genderMenu = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void createColorMenuShirt() {
    DEBUG_PRINT("Creating Shirt Color Menu");
    
    colorMenu = lv_obj_create(NULL);
    lv_obj_t *newMenu = colorMenu;
    lv_obj_add_style(newMenu, &style_screen, 0);
    DEBUG_PRINTF("New color menu created: %p\n", newMenu);
    addWifiIndicator(newMenu);
    addBatteryIndicator(newMenu);
    lv_timer_handler(); // Process any pending UI updates

    lv_obj_t *header = lv_obj_create(newMenu);
    lv_obj_set_size(header, 320, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Select Shirt Color");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *container = lv_obj_create(newMenu);
    lv_obj_set_size(container, 300, 160);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(container, 5, 0);
    lv_obj_set_style_pad_row(container, 5, 0);
    lv_obj_set_style_pad_column(container, 5, 0);

    // Set up grid layout on container
    static lv_coord_t col_dsc[] = {90, 90, 90, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {40, 40, 40, LV_GRID_TEMPLATE_LAST};
    
    lv_obj_set_grid_dsc_array(container, col_dsc, row_dsc);
    lv_obj_set_layout(container, LV_LAYOUT_GRID);

    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLLABLE);
    current_scroll_obj = container;

    static lv_point_t last_point = {0, 0};

    lv_obj_add_event_cb(container, [](lv_event_t *e) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_indev_get_point(indev, &last_point);
    }, LV_EVENT_PRESSED, NULL);

    lv_obj_add_event_cb(container, [](lv_event_t *e) {
        lv_obj_t *container = (lv_obj_t*)lv_event_get_user_data(e);
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t curr_point;
        lv_indev_get_point(indev, &curr_point);
        lv_coord_t delta_y = last_point.y - curr_point.y;
        lv_obj_scroll_by(container, 0, delta_y, LV_ANIM_OFF);
        last_point = curr_point;
    }, LV_EVENT_PRESSING, container);

    int numShirtColors = sizeof(shirtColors) / sizeof(shirtColors[0]);
    DEBUG_PRINTF("Creating %d shirt color buttons\n", numShirtColors);

    for (int i = 0; i < numShirtColors; i++) {
        lv_obj_t *btn = lv_btn_create(container);
        lv_obj_set_size(btn, 90, 40);
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
        lv_obj_set_user_data(btn, (void*)i);
        
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, shirtColors[i]);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_t *btn = lv_event_get_target(e);
            int idx = (int)lv_obj_get_user_data(btn);
            bool is_selected = lv_obj_has_state(btn, LV_STATE_USER_1);

            if (!is_selected) {
                lv_obj_add_state(btn, LV_STATE_USER_1);
                lv_obj_set_style_border_width(btn, 3, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFF00), LV_PART_MAIN);
                if (!selectedShirtColors.isEmpty()) selectedShirtColors += "+";
                selectedShirtColors += shirtColors[idx];
            } else {
                lv_obj_clear_state(btn, LV_STATE_USER_1);
                lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
                int pos = selectedShirtColors.indexOf(shirtColors[idx]);
                if (pos >= 0) {
                    int nextPlus = selectedShirtColors.indexOf("+", pos);
                    if (nextPlus >= 0) {
                        selectedShirtColors.remove(pos, nextPlus - pos + 1);
                    } else {
                        if (pos > 0) selectedShirtColors.remove(pos - 1, String(shirtColors[idx]).length() + 1);
                        else selectedShirtColors.remove(pos, String(shirtColors[idx]).length());
                    }
                }
            }
            
            DEBUG_PRINTF("Selected shirt colors: %s\n", selectedShirtColors.c_str());
            
            if (!selectedShirtColors.isEmpty()) {
                if (shirt_next_btn == nullptr) {
                    shirt_next_btn = lv_btn_create(lv_obj_get_parent(lv_obj_get_parent(btn)));
                    lv_obj_align(shirt_next_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
                    lv_obj_set_size(shirt_next_btn, 100, 40);
                    lv_obj_add_style(shirt_next_btn, &style_btn, 0);
                    lv_obj_add_style(shirt_next_btn, &style_btn_pressed, LV_STATE_PRESSED);
                    
                    lv_obj_t* next_label = lv_label_create(shirt_next_btn);
                    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
                    lv_obj_center(next_label);
                    
                    lv_obj_add_event_cb(shirt_next_btn, [](lv_event_t* e) {
                        currentEntry += selectedShirtColors + ",";
                        DEBUG_PRINTF("Current entry: %s\n", currentEntry.c_str());
                        delay(100);
                        DEBUG_PRINT("Transitioning to pants menu");
                        createColorMenuPants();
                        if (colorMenu && colorMenu != lv_scr_act()) {
                            DEBUG_PRINTF("Cleaning old color menu: %p\n", colorMenu);
                            lv_obj_del_async(colorMenu);
                            colorMenu = nullptr;
                        }
                        DEBUG_PRINT("Shirt menu transition complete");
                    }, LV_EVENT_CLICKED, NULL);
                }
            }
        }, LV_EVENT_CLICKED, NULL);
    }

    for (int i = 0; i < numShirtColors; i++) {
        lv_obj_t *btn = lv_obj_get_child(container, i);
        if (btn && lv_obj_check_type(btn, &lv_btn_class)) {
            uint32_t color_hex = 0x4A4A4A; // Default gray
            if (strcmp(shirtColors[i], "White") == 0) color_hex = 0xFFFFFF;
            else if (strcmp(shirtColors[i], "Black") == 0) color_hex = 0x000000;
            else if (strcmp(shirtColors[i], "Red") == 0) color_hex = 0xFF0000;
            else if (strcmp(shirtColors[i], "Orange") == 0) color_hex = 0xFFA500;
            else if (strcmp(shirtColors[i], "Yellow") == 0) color_hex = 0xFFFF00;
            else if (strcmp(shirtColors[i], "Green") == 0) color_hex = 0x00FF00;
            else if (strcmp(shirtColors[i], "Blue") == 0) color_hex = 0x0000FF;
            else if (strcmp(shirtColors[i], "Purple") == 0) color_hex = 0x800080;
            
            lv_obj_set_style_bg_color(btn, lv_color_hex(color_hex), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color_hex), LV_STATE_PRESSED);
            
            uint32_t text_color = 0xFFFFFF; // Default white text
            if (strcmp(shirtColors[i], "White") == 0 || 
                strcmp(shirtColors[i], "Yellow") == 0) {
                text_color = 0x000000; // Black text for light backgrounds
            }
            
            lv_obj_t *label = lv_obj_get_child(btn, 0);
            if (label && lv_obj_check_type(label, &lv_label_class)) {
                lv_obj_set_style_text_color(label, lv_color_hex(text_color), 0);
            }
        }
    }

    lv_obj_t *back_btn = lv_btn_create(newMenu);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        delay(100);
        DEBUG_PRINT("Returning to gender menu");
        createGenderMenu();
        if (colorMenu && colorMenu != lv_scr_act()) {
            DEBUG_PRINTF("Cleaning old color menu: %p\n", colorMenu);
            lv_obj_del_async(colorMenu);
            colorMenu = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
    
    lv_scr_load(newMenu);
    
    if (colorMenu && colorMenu != newMenu) {
        DEBUG_PRINTF("Cleaning existing color menu: %p\n", colorMenu);
        lv_obj_del_async(colorMenu);
    }
    
    colorMenu = newMenu;
}

void createColorMenuPants() {
    DEBUG_PRINT("Creating Pants Color Menu");
    
    lv_obj_t* newMenu = lv_obj_create(NULL);
    lv_obj_add_style(newMenu, &style_screen, 0);
    DEBUG_PRINTF("New color menu created: %p\n", newMenu);
    addWifiIndicator(newMenu);
    addBatteryIndicator(newMenu);
    lv_timer_handler(); // Process any pending UI updates

    lv_obj_t *header = lv_obj_create(newMenu);
    lv_obj_set_size(header, 320, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Select Pants Color");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *container = lv_obj_create(newMenu);
    lv_obj_set_size(container, 300, 150);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(container, 5, 0);
    lv_obj_set_style_pad_row(container, 8, 0);
    lv_obj_set_style_pad_column(container, 8, 0);
    
    // Set up grid layout on container with fixed sizes
    static lv_coord_t col_dsc[] = {90, 90, 90, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {40, 40, 40, LV_GRID_TEMPLATE_LAST};
    
    lv_obj_set_grid_dsc_array(container, col_dsc, row_dsc);
    lv_obj_set_layout(container, LV_LAYOUT_GRID);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLLABLE);
    current_scroll_obj = container;

    static lv_point_t last_point = {0, 0};

    lv_obj_add_event_cb(container, [](lv_event_t *e) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_indev_get_point(indev, &last_point);
    }, LV_EVENT_PRESSED, NULL);

    lv_obj_add_event_cb(container, [](lv_event_t *e) {
        lv_obj_t *container = (lv_obj_t*)lv_event_get_user_data(e);
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t curr_point;
        lv_indev_get_point(indev, &curr_point);
        lv_coord_t delta_y = last_point.y - curr_point.y;
        lv_obj_scroll_by(container, 0, delta_y, LV_ANIM_OFF);
        last_point = curr_point;
    }, LV_EVENT_PRESSING, container);

    int numPantsColors = sizeof(pantsColors) / sizeof(pantsColors[0]);
    DEBUG_PRINTF("Creating %d pants color buttons\n", numPantsColors);

    for (int i = 0; i < numPantsColors; i++) {
        lv_obj_t *btn = lv_btn_create(container);
        lv_obj_set_size(btn, 90, 40);
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
        lv_obj_set_user_data(btn, (void*)i);
        
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, pantsColors[i]);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_t *btn = lv_event_get_target(e);
            int idx = (int)lv_obj_get_user_data(btn);
            bool is_selected = lv_obj_has_state(btn, LV_STATE_USER_1);

            if (!is_selected) {
                lv_obj_add_state(btn, LV_STATE_USER_1);
                lv_obj_set_style_border_width(btn, 3, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFF00), LV_PART_MAIN);
                if (!selectedPantsColors.isEmpty()) selectedPantsColors += "+";
                selectedPantsColors += pantsColors[idx];
            } else {
                lv_obj_clear_state(btn, LV_STATE_USER_1);
                lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
                int pos = selectedPantsColors.indexOf(pantsColors[idx]);
                if (pos >= 0) {
                    int nextPlus = selectedPantsColors.indexOf("+", pos);
                    if (nextPlus >= 0) {
                        selectedPantsColors.remove(pos, nextPlus - pos + 1);
                    } else {
                        if (pos > 0) selectedPantsColors.remove(pos - 1, String(pantsColors[idx]).length() + 1);
                        else selectedPantsColors.remove(pos, String(pantsColors[idx]).length());
                    }
                }
            }
            
            DEBUG_PRINTF("Selected pants colors: %s\n", selectedPantsColors.c_str());
            
            if (!selectedPantsColors.isEmpty()) {
                if (pants_next_btn == nullptr) {
                    pants_next_btn = lv_btn_create(lv_obj_get_parent(lv_obj_get_parent(btn)));
                    lv_obj_align(pants_next_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
                    lv_obj_set_size(pants_next_btn, 100, 40);
                    lv_obj_add_style(pants_next_btn, &style_btn, 0);
                    lv_obj_add_style(pants_next_btn, &style_btn_pressed, LV_STATE_PRESSED);
                    
                    lv_obj_t* next_label = lv_label_create(pants_next_btn);
                    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
                    lv_obj_center(next_label);
                    
                    lv_obj_add_event_cb(pants_next_btn, [](lv_event_t* e) {
                        currentEntry += selectedPantsColors + ",";
                        DEBUG_PRINTF("Current entry: %s\n", currentEntry.c_str());
                        delay(100);
                        DEBUG_PRINT("Transitioning to shoes menu");
                        createColorMenuShoes();
                        if (colorMenu && colorMenu != lv_scr_act()) {
                            DEBUG_PRINTF("Cleaning old color menu: %p\n", colorMenu);
                            lv_obj_del_async(colorMenu);
                            colorMenu = nullptr;
                        }
                        DEBUG_PRINT("Pants menu transition complete");
                    }, LV_EVENT_CLICKED, NULL);
                }
            }
        }, LV_EVENT_CLICKED, NULL);
    }

    for (int i = 0; i < numPantsColors; i++) {
        lv_obj_t *btn = lv_obj_get_child(container, i);
        if (btn && lv_obj_check_type(btn, &lv_btn_class)) {
            uint32_t color_hex = 0x4A4A4A; // Default gray
            if (strcmp(pantsColors[i], "White") == 0) color_hex = 0xFFFFFF;
            else if (strcmp(pantsColors[i], "Black") == 0) color_hex = 0x000000;
            else if (strcmp(pantsColors[i], "Red") == 0) color_hex = 0xFF0000;
            else if (strcmp(pantsColors[i], "Orange") == 0) color_hex = 0xFFA500;
            else if (strcmp(pantsColors[i], "Yellow") == 0) color_hex = 0xFFFF00;
            else if (strcmp(pantsColors[i], "Green") == 0) color_hex = 0x00FF00;
            else if (strcmp(pantsColors[i], "Blue") == 0) color_hex = 0x0000FF;
            else if (strcmp(pantsColors[i], "Purple") == 0) color_hex = 0x800080;
            else if (strcmp(pantsColors[i], "Brown") == 0) color_hex = 0x8B4513;
            else if (strcmp(pantsColors[i], "Grey") == 0) color_hex = 0x808080;
            else if (strcmp(pantsColors[i], "Tan") == 0) color_hex = 0xD2B48C;
            
            lv_obj_set_style_bg_color(btn, lv_color_hex(color_hex), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color_hex), LV_STATE_PRESSED);
            
            uint32_t text_color = 0xFFFFFF; // Default white text
            if (strcmp(pantsColors[i], "White") == 0 || 
                strcmp(pantsColors[i], "Yellow") == 0 ||
                strcmp(pantsColors[i], "Tan") == 0) {
                text_color = 0x000000; // Black text for light backgrounds
            }
            
            lv_obj_t *label = lv_obj_get_child(btn, 0);
            if (label && lv_obj_check_type(label, &lv_label_class)) {
                lv_obj_set_style_text_color(label, lv_color_hex(text_color), 0);
            }
        }
    }

    lv_obj_t *back_btn = lv_btn_create(newMenu);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        delay(100);
        DEBUG_PRINT("Returning to shirt menu");
        
        pants_next_btn = nullptr;
        shoes_next_btn = nullptr;
        
        createColorMenuShirt();
        if (colorMenu && colorMenu != lv_scr_act()) {
            DEBUG_PRINTF("Cleaning old color menu: %p\n", colorMenu);
            lv_obj_del_async(colorMenu);
            colorMenu = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
    
    lv_scr_load(newMenu);
    
    if (colorMenu && colorMenu != newMenu) {
        DEBUG_PRINTF("Cleaning existing color menu: %p\n", colorMenu);
        lv_obj_del_async(colorMenu);
    }
    
    colorMenu = newMenu;
}

void createColorMenuShoes() {
    DEBUG_PRINT("Creating Shoes Color Menu");
    
    lv_obj_t* newMenu = lv_obj_create(NULL);
    lv_obj_add_style(newMenu, &style_screen, 0);
    DEBUG_PRINTF("New color menu created: %p\n", newMenu);
    addWifiIndicator(newMenu);
    addBatteryIndicator(newMenu);
    lv_timer_handler(); // Process any pending UI updates

    lv_obj_t *header = lv_obj_create(newMenu);
    lv_obj_set_size(header, 320, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Select Shoes Color");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *container = lv_obj_create(newMenu);
    lv_obj_set_size(container, 300, 150);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(container, 5, 0);
    lv_obj_set_style_pad_row(container, 8, 0);
    lv_obj_set_style_pad_column(container, 8, 0);
    
    // Set up grid layout on container with fixed sizes
    static lv_coord_t col_dsc[] = {90, 90, 90, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {40, 40, 40, LV_GRID_TEMPLATE_LAST};
    
    lv_obj_set_grid_dsc_array(container, col_dsc, row_dsc);
    lv_obj_set_layout(container, LV_LAYOUT_GRID);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLLABLE);
    current_scroll_obj = container;

    static lv_point_t last_point = {0, 0};

    lv_obj_add_event_cb(container, [](lv_event_t *e) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_indev_get_point(indev, &last_point);
    }, LV_EVENT_PRESSED, NULL);

    lv_obj_add_event_cb(container, [](lv_event_t *e) {
        lv_obj_t *container = (lv_obj_t*)lv_event_get_user_data(e);
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t curr_point;
        lv_indev_get_point(indev, &curr_point);
        lv_coord_t delta_y = last_point.y - curr_point.y;
        lv_obj_scroll_by(container, 0, delta_y, LV_ANIM_OFF);
        last_point = curr_point;
    }, LV_EVENT_PRESSING, container);

    int numShoeColors = sizeof(shoeColors) / sizeof(shoeColors[0]);
    DEBUG_PRINTF("Creating %d shoe color buttons\n", numShoeColors);

    for (int i = 0; i < numShoeColors; i++) {
        lv_obj_t *btn = lv_btn_create(container);
        lv_obj_set_size(btn, 90, 40);
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
        lv_obj_set_user_data(btn, (void*)i);
        
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, shoeColors[i]);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_t *btn = lv_event_get_target(e);
            int idx = (int)lv_obj_get_user_data(btn);
            bool is_selected = lv_obj_has_state(btn, LV_STATE_USER_1);

            if (!is_selected) {
                lv_obj_add_state(btn, LV_STATE_USER_1);
                lv_obj_set_style_border_width(btn, 3, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFF00), LV_PART_MAIN);
                if (!selectedShoesColors.isEmpty()) selectedShoesColors += "+";
                selectedShoesColors += shoeColors[idx];
            } else {
                lv_obj_clear_state(btn, LV_STATE_USER_1);
                lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
                int pos = selectedShoesColors.indexOf(shoeColors[idx]);
                if (pos >= 0) {
                    int nextPlus = selectedShoesColors.indexOf("+", pos);
                    if (nextPlus >= 0) {
                        selectedShoesColors.remove(pos, nextPlus - pos + 1);
                    } else {
                        if (pos > 0) selectedShoesColors.remove(pos - 1, String(shoeColors[idx]).length() + 1);
                        else selectedShoesColors.remove(pos, String(shoeColors[idx]).length());
                    }
                }
            }
            
            DEBUG_PRINTF("Selected shoe colors: %s\n", selectedShoesColors.c_str());
            
            if (!selectedShoesColors.isEmpty()) {
                if (shoes_next_btn == nullptr) {
                    shoes_next_btn = lv_btn_create(lv_obj_get_parent(lv_obj_get_parent(btn)));
                    lv_obj_align(shoes_next_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
                    lv_obj_set_size(shoes_next_btn, 100, 40);
                    lv_obj_add_style(shoes_next_btn, &style_btn, 0);
                    lv_obj_add_style(shoes_next_btn, &style_btn_pressed, LV_STATE_PRESSED);
                    
                    lv_obj_t* next_label = lv_label_create(shoes_next_btn);
                    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
                    lv_obj_center(next_label);
                    
                    lv_obj_add_event_cb(shoes_next_btn, [](lv_event_t* e) {
                        currentEntry += selectedShoesColors + ",";
                        DEBUG_PRINTF("Current entry: %s\n", currentEntry.c_str());
                        delay(100);
                        DEBUG_PRINT("Transitioning to item menu");
                        createItemMenu();
                        if (colorMenu && colorMenu != lv_scr_act()) {
                            DEBUG_PRINTF("Cleaning old color menu: %p\n", colorMenu);
                            lv_obj_del_async(colorMenu);
                            colorMenu = nullptr;
                        }
                        DEBUG_PRINT("Shoes menu transition complete");
                    }, LV_EVENT_CLICKED, NULL);
                }
            }
        }, LV_EVENT_CLICKED, NULL);
    }

    for (int i = 0; i < numShoeColors; i++) {
        lv_obj_t *btn = lv_obj_get_child(container, i);
        if (btn && lv_obj_check_type(btn, &lv_btn_class)) {
            uint32_t color_hex = 0x4A4A4A; // Default gray
            if (strcmp(shoeColors[i], "White") == 0) color_hex = 0xFFFFFF;
            else if (strcmp(shoeColors[i], "Black") == 0) color_hex = 0x000000;
            else if (strcmp(shoeColors[i], "Red") == 0) color_hex = 0xFF0000;
            else if (strcmp(shoeColors[i], "Orange") == 0) color_hex = 0xFFA500;
            else if (strcmp(shoeColors[i], "Yellow") == 0) color_hex = 0xFFFF00;
            else if (strcmp(shoeColors[i], "Green") == 0) color_hex = 0x00FF00;
            else if (strcmp(shoeColors[i], "Blue") == 0) color_hex = 0x0000FF;
            else if (strcmp(shoeColors[i], "Purple") == 0) color_hex = 0x800080;
            else if (strcmp(shoeColors[i], "Brown") == 0) color_hex = 0x8B4513;
            else if (strcmp(shoeColors[i], "Grey") == 0) color_hex = 0x808080;
            else if (strcmp(shoeColors[i], "Tan") == 0) color_hex = 0xD2B48C;
            
            lv_obj_set_style_bg_color(btn, lv_color_hex(color_hex), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color_hex), LV_STATE_PRESSED);
            
            uint32_t text_color = 0xFFFFFF; // Default white text
            if (strcmp(shoeColors[i], "White") == 0 || 
                strcmp(shoeColors[i], "Yellow") == 0 ||
                strcmp(shoeColors[i], "Tan") == 0) {
                text_color = 0x000000; // Black text for light backgrounds
            }
            
            lv_obj_t *label = lv_obj_get_child(btn, 0);
            if (label && lv_obj_check_type(label, &lv_label_class)) {
                lv_obj_set_style_text_color(label, lv_color_hex(text_color), 0);
            }
        }
    }

    lv_obj_t *back_btn = lv_btn_create(newMenu);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        delay(100);
        DEBUG_PRINT("Returning to pants menu");
        
        shoes_next_btn = nullptr;
        
        createColorMenuPants();
        if (colorMenu && colorMenu != lv_scr_act()) {
            DEBUG_PRINTF("Cleaning old color menu: %p\n", colorMenu);
            lv_obj_del_async(colorMenu);
            colorMenu = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
    
    lv_scr_load(newMenu);
    
    if (colorMenu && colorMenu != newMenu) {
        DEBUG_PRINTF("Cleaning existing color menu: %p\n", colorMenu);
        lv_obj_del_async(colorMenu);
    }
    
    colorMenu = newMenu;
}

void createItemMenu() {
    if (itemMenu) {
        DEBUG_PRINT("Cleaning existing item menu");
        lv_obj_del(itemMenu);
        itemMenu = nullptr;
    }
    itemMenu = lv_obj_create(NULL);
    lv_obj_add_style(itemMenu, &style_screen, 0);
    lv_scr_load(itemMenu);
    DEBUG_PRINTF("Item menu created: %p\n", itemMenu);
    
    // Force immediate update of indicators
    addWifiIndicator(itemMenu);
    addBatteryIndicator(itemMenu);
    lv_timer_handler(); // Process any pending UI updates

    lv_obj_t *header = lv_obj_create(itemMenu);
    lv_obj_set_size(header, 320, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Select Item");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *list_cont = lv_obj_create(itemMenu);
    lv_obj_set_size(list_cont, 280, 180);
    lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(list_cont, lv_color_hex(0x4A4A4A), 0);
    
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_cont, 5, 0);
    lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(list_cont, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_clear_flag(list_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_flag(list_cont, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 7; i++) {
        lv_obj_t *btn = lv_btn_create(list_cont);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 40);
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, items[i]);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        lv_obj_t* btn = lv_event_get_target(e);
        currentEntry += String(lv_label_get_text(lv_obj_get_child(btn, 0))); // Append item
        delay(100);
        createConfirmScreen();
        }, LV_EVENT_CLICKED, NULL);
    }
}

void createConfirmScreen() {
    if (confirmScreen) {
        DEBUG_PRINT("Cleaning existing confirm screen");
        lv_obj_del(confirmScreen);
        confirmScreen = nullptr;
    }
    confirmScreen = lv_obj_create(NULL);
    lv_obj_add_style(confirmScreen, &style_screen, 0);
    lv_scr_load(confirmScreen);

    addWifiIndicator(confirmScreen);
    addBatteryIndicator(confirmScreen);

    lv_obj_t* header = lv_obj_create(confirmScreen);
    lv_obj_set_size(header, 320, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Confirm Entry");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Add entry display
    String formattedEntry = getFormattedEntry(currentEntry);
    lv_obj_t* entry_label = lv_label_create(confirmScreen);
    lv_obj_set_style_text_font(entry_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(entry_label, 280); // Wrap text within screen width
    lv_label_set_long_mode(entry_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(entry_label, formattedEntry.c_str());
    lv_obj_align(entry_label, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t* confirm_btn = lv_btn_create(confirmScreen);
    lv_obj_set_size(confirm_btn, 100, 40);
    lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_style(confirm_btn, &style_btn, 0);
    lv_obj_add_style(confirm_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);

    lv_obj_add_event_cb(confirm_btn, [](lv_event_t* e) {
        Serial.println("Confirm clicked");
        saveEntry(currentEntry);
        delay(100);
        createMainMenu();
        if (confirmScreen && confirmScreen != lv_scr_act()) {
            DEBUG_PRINTF("Cleaning existing confirm screen: %p\n", confirmScreen);
            lv_obj_del_async(confirmScreen);
            confirmScreen = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* back_btn = lv_btn_create(confirmScreen);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        delay(100);
        createItemMenu();
        if (confirmScreen && confirmScreen != lv_scr_act()) {
            lv_obj_del_async(confirmScreen);
            confirmScreen = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void scanNetworks() {
    // Don't start a new scan if one is already in progress
    if (wifiScanInProgress) {
        DEBUG_PRINT("WiFi scan already in progress, ignoring request");
        return;
    }
    
    // Check WiFi status
    if (WiFi.status() == WL_NO_SHIELD) {
        DEBUG_PRINT("No WiFi shield or module found");
        updateStatus("WiFi module not found", 0xFF0000);
        return;
    } else if (WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINTF("WiFi connected to %s\n", WiFi.SSID().c_str());
    } else {
        DEBUG_PRINTF("WiFi status: %d\n", WiFi.status());
    }
    
    // Clear previous list
    if (wifi_list != nullptr) {
        lv_obj_clean(wifi_list);
    }
    
    // Create spinner if it doesn't exist
    if (g_spinner == nullptr) {
        g_spinner = lv_spinner_create(wifi_screen, 1000, 60);
        lv_obj_set_size(g_spinner, 50, 50);
        lv_obj_align(g_spinner, LV_ALIGN_CENTER, 0, 0);
    }
    
    // Update status
    updateStatus("Scanning for WiFi networks...", 0xFFFFFF);
    
    // Start WiFi scan
    DEBUG_PRINT("Starting WiFi scan");
    WiFi.scanDelete(); // Delete results of previous scan if any
    
    int scanResult = WiFi.scanNetworks(true); // Start scan in async mode
    
    if (scanResult == WIFI_SCAN_FAILED) {
        DEBUG_PRINT("WiFi scan failed to start");
        updateStatus("WiFi scan failed", 0xFF0000);
        
        // Clean up
        if (g_spinner != nullptr) {
            lv_obj_del(g_spinner);
            g_spinner = nullptr;
        }
        return;
    }
    
    // Set scan in progress flag and record start time
    wifiScanInProgress = true;
    lastScanStartTime = millis();
    
    // Create timer to check scan status
    if (scan_timer != nullptr) {
        lv_timer_del(scan_timer);
    }
    scan_timer = lv_timer_create(scan_timer_callback, 500, NULL);
    
    // Reset batch processing variables
    currentBatch = 0;
    totalNetworksFound = 0;
    
    // If we have credentials, try to reconnect after scan
    if (strlen(selected_ssid) > 0 && strlen(selected_password) > 0) {
        DEBUG_PRINTF("Will reconnect to %s after scan\n", selected_ssid);
    }
}

// Callback for scan timer
void scan_timer_callback(lv_timer_t* timer) {
    if (!wifiScanInProgress) {
        DEBUG_PRINT("Scan not in progress, deleting timer");
        lv_timer_del(timer);
        scan_timer = nullptr;
        return;
    }
    
    // Check for timeout
    if (millis() - lastScanStartTime > SCAN_TIMEOUT) {
        DEBUG_PRINT("Scan timeout reached");
        wifiScanInProgress = false;
        
        // Clean up
        if (g_spinner != nullptr) {
            lv_obj_del(g_spinner);
            g_spinner = nullptr;
        }
        
        // Delete the timer
        lv_timer_del(timer);
        scan_timer = nullptr;
        
        // Update status
        updateStatus("Scan timed out", 0xFF0000);
        
        // If we were connected before, try to reconnect
        if (strlen(selected_ssid) > 0 && strlen(selected_password) > 0) {
            DEBUG_PRINT("Attempting to reconnect to previous network");
            WiFi.begin(selected_ssid, selected_password);
        }
        
        return;
    }
    
    // Check scan status
    int scanStatus = WiFi.scanComplete();
    
    if (scanStatus == WIFI_SCAN_RUNNING) {
        // Scan still in progress
        return;
    } else if (scanStatus == WIFI_SCAN_FAILED) {
        DEBUG_PRINT("WiFi scan failed");
        wifiScanInProgress = false;
        
        // Clean up
        if (g_spinner != nullptr) {
            lv_obj_del(g_spinner);
            g_spinner = nullptr;
        }
        
        // Delete the timer
        lv_timer_del(timer);
        scan_timer = nullptr;
        
        // Update status
        updateStatus("Scan failed", 0xFF0000);
        
        // If we were connected before, try to reconnect
        if (strlen(selected_ssid) > 0 && strlen(selected_password) > 0) {
            DEBUG_PRINT("Attempting to reconnect to previous network");
            WiFi.begin(selected_ssid, selected_password);
        }
        
        return;
    } else if (scanStatus == 0) {
        updateStatus("No networks found", 0xFF0000);
        
        // If we were connected before, try to reconnect
        if (strlen(selected_ssid) > 0 && strlen(selected_password) > 0) {
            DEBUG_PRINT("Attempting to reconnect to previous network");
            WiFi.begin(selected_ssid, selected_password);
        }
        
        return;
    } else if (scanStatus > 0) {
        // Scan completed with results
        DEBUG_PRINTF("WiFi scan completed with %d networks found\n", scanStatus);
        wifiScanInProgress = false;
        
        // Clean up spinner
        if (g_spinner != nullptr) {
            lv_obj_del(g_spinner);
            g_spinner = nullptr;
        }
        
        // Delete the timer
        lv_timer_del(timer);
        scan_timer = nullptr;
        
        // Update status
        String statusMsg = "Networks found: " + String(scanStatus);
        updateStatus(statusMsg.c_str(), 0x00FF00);
        
        // Process scan results in batches
        totalNetworksFound = scanStatus;
        currentBatch = 0;
        
        // Create a timer to process networks in batches
        lv_timer_t* batch_timer = lv_timer_create(network_batch_timer_callback, 10, NULL);
    }
}

// Callback for network batch timer
void network_batch_timer_callback(lv_timer_t* timer) {
    if (timer == nullptr || wifi_list == nullptr) {
        return;
    }
    
    // Check if wifi_list is still valid
    if (!lv_obj_is_valid(wifi_list)) {
        DEBUG_PRINT("WiFi list is no longer valid");
        lv_timer_del(timer);
        return;
    }
    
    int batchSize = 5;
    int startIdx = currentBatch * batchSize;
    int endIdx = min(startIdx + batchSize, totalNetworksFound);
    
    for (int i = startIdx; i < endIdx; ++i) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        
        // Skip empty SSIDs
        if (ssid.length() == 0) {
            continue;
        }
        
        String signal_indicator;
        if (rssi > -60) {
            signal_indicator = "●●●";
        } else if (rssi > -70) {
            signal_indicator = "●●○";
        } else {
            signal_indicator = "●○○";
        }
        
        String security = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "" : "*";
        
        char list_text[50];
        snprintf(list_text, sizeof(list_text), "%s %s %s", 
            ssid.c_str(), signal_indicator.c_str(), security.c_str());
        
        lv_obj_t* btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, list_text);
        if (btn == nullptr) {
            DEBUG_PRINT("Failed to create WiFi list button");
            continue;
        }
        
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
        
        if (strcmp(selected_ssid, ssid.c_str()) == 0) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x3E6E6E), 0); // Teal highlight
        }
        
        // Allocate memory for the SSID string
        size_t ssid_len = ssid.length() + 1;  // +1 for null terminator
        char* btn_ssid = (char*)lv_mem_alloc(ssid_len);
        if (btn_ssid == nullptr) {
            DEBUG_PRINT("Failed to allocate memory for SSID");
            continue;
        }
        
        // Copy the SSID string safely
        strncpy(btn_ssid, ssid.c_str(), ssid_len - 1);
        btn_ssid[ssid_len - 1] = '\0';
        
        // Store the SSID string in the button's user data
        lv_obj_set_user_data(btn, btn_ssid);
        
        // Add click event callback
        lv_obj_add_event_cb(btn, wifi_btn_event_callback, LV_EVENT_CLICKED, NULL);
    }
    
    currentBatch++;
    if (currentBatch >= (totalNetworksFound + batchSize - 1) / batchSize) {
        // All batches processed, delete the timer
        lv_timer_del(timer);
    }
}

// Callback for WiFi button event
void wifi_btn_event_callback(lv_event_t* e) {
    if (e == nullptr) {
        return;
    }
    
    lv_obj_t* btn = lv_event_get_target(e);
    if (btn == nullptr) {
        return;
    }
    
    // Make sure the button is still valid
    if (!lv_obj_is_valid(btn)) {
        DEBUG_PRINT("Button is no longer valid");
        return;
    }
    
    char* ssid = (char*)lv_obj_get_user_data(btn);
    if (ssid && (intptr_t)ssid > 100) { // Check if it's a valid pointer and not just an integer cast
        strncpy(selected_ssid, ssid, 32);
        selected_ssid[32] = '\0';
        DEBUG_PRINTF("Selected SSID: %s\n", selected_ssid);
        
        // Don't rescan networks when selecting one
        // scanNetworks();
        
        showWiFiKeyboard();
    } else {
        DEBUG_PRINT("Invalid SSID data in button");
    }
}

void showWiFiKeyboard() {
    if (wifi_keyboard == nullptr) {
        keyboard_page_index = 0;
        memset(selected_password, 0, sizeof(selected_password));
        
        wifi_keyboard = lv_obj_create(wifi_screen);
        lv_obj_set_size(wifi_keyboard, 320, 240);
        lv_obj_align(wifi_keyboard, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(wifi_keyboard, lv_color_hex(0x1E1E1E), 0);
        
        lv_obj_t* ta = lv_textarea_create(wifi_keyboard);
        lv_obj_set_size(ta, 260, 40);
        lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 15);
        lv_textarea_set_password_mode(ta, true);
        lv_textarea_set_max_length(ta, 64);
        lv_textarea_set_placeholder_text(ta, "Password");
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            lv_obj_t* ta = lv_event_get_target(e);
            const char* password = lv_textarea_get_text(ta);
            strncpy(selected_password, password, 64);
            selected_password[64] = '\0';
        }, LV_EVENT_VALUE_CHANGED, NULL);
        
        lv_obj_t* close_btn = lv_btn_create(wifi_keyboard);
        lv_obj_set_size(close_btn, 40, 40);
        lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_radius(close_btn, 20, 0);
        
        lv_obj_t* close_label = lv_label_create(close_btn);
        lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
        lv_obj_center(close_label);
        
        lv_obj_t* kb = lv_btnmatrix_create(wifi_keyboard);
        lv_obj_set_size(kb, 300, 150);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -5);
        
        lv_btnmatrix_set_map(kb, btnm_mapplus[keyboard_page_index]);
        lv_btnmatrix_set_ctrl_map(kb, keyboard_ctrl_map);
        
        lv_obj_add_event_cb(kb, [](lv_event_t* e) {
            lv_event_code_t code = lv_event_get_code(e);
            lv_obj_t* btnm = lv_event_get_target(e);
            
            if (code == LV_EVENT_VALUE_CHANGED) {
                uint32_t btn_id = lv_btnmatrix_get_selected_btn(btnm);
                const char* txt = lv_btnmatrix_get_btn_text(btnm, btn_id);
                
                if (txt) {
                    DEBUG_PRINTF("Button matrix pressed: %s\n", txt);
                    
                    if (strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
                        keyboard_page_index = (keyboard_page_index + 1) % NUM_KEYBOARD_PAGES;
                        DEBUG_PRINTF("Changing to keyboard page: %d\n", keyboard_page_index);
                        lv_btnmatrix_set_map(btnm, btnm_mapplus[keyboard_page_index]);
                    } else if (strcmp(txt, LV_SYMBOL_LEFT) == 0) {
                        keyboard_page_index = (keyboard_page_index - 1 + NUM_KEYBOARD_PAGES) % NUM_KEYBOARD_PAGES;
                        DEBUG_PRINTF("Changing to keyboard page: %d\n", keyboard_page_index);
                        lv_btnmatrix_set_map(btnm, btnm_mapplus[keyboard_page_index]);
                    } else if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
                        lv_obj_t* ta = lv_obj_get_child(wifi_keyboard, 0);
                        if (ta && lv_obj_check_type(ta, &lv_textarea_class)) {
                            lv_textarea_del_char(ta);
                        }
                    } else if (strcmp(txt, LV_SYMBOL_OK) == 0) {
                        lv_obj_t* ta = lv_obj_get_child(wifi_keyboard, 0);
                        if (ta && lv_obj_check_type(ta, &lv_textarea_class)) {
                            const char* password = lv_textarea_get_text(ta);
                            strncpy(selected_password, password, 64);
                            selected_password[64] = '\0';
                        }
                        
                        lv_obj_clean(wifi_keyboard);
                        
                        lv_obj_t* pwd_label = lv_label_create(wifi_keyboard);
                        lv_obj_align(pwd_label, LV_ALIGN_TOP_MID, 0, 30);
                        
                        char asterisks[65] = {0};
                        size_t len = strlen(selected_password);
                        if (len > 64) len = 64;
                        for (size_t i = 0; i < len; i++) {
                            asterisks[i] = '*';
                        }
                        asterisks[len] = '\0';
                        
                        char buffer[100];
                        snprintf(buffer, sizeof(buffer), "Password: %s", asterisks);
                        lv_label_set_text(pwd_label, buffer);
                        
                        lv_obj_t* connect_btn = lv_btn_create(wifi_keyboard);
                        lv_obj_set_size(connect_btn, 140, 50);
                        lv_obj_align(connect_btn, LV_ALIGN_CENTER, -75, 50);
                        lv_obj_add_style(connect_btn, &style_btn, 0);
                        lv_obj_add_style(connect_btn, &style_btn_pressed, LV_STATE_PRESSED);
                        
                        lv_obj_t* connect_label = lv_label_create(connect_btn);
                        lv_label_set_text(connect_label, "Connect");
                        lv_obj_center(connect_label);
                        
                        lv_obj_t* cancel_btn = lv_btn_create(wifi_keyboard);
                        lv_obj_set_size(cancel_btn, 140, 50);
                        lv_obj_align(cancel_btn, LV_ALIGN_CENTER, 75, 50);
                        lv_obj_add_style(cancel_btn, &style_btn, 0);
                        lv_obj_add_style(cancel_btn, &style_btn_pressed, LV_STATE_PRESSED);
                        
                        lv_obj_t* cancel_label = lv_label_create(cancel_btn);
                        lv_label_set_text(cancel_label, "Cancel");
                        lv_obj_center(cancel_label);
                        
                        lv_obj_add_event_cb(connect_btn, [](lv_event_t* e) {
                            connectToWiFi();
                        }, LV_EVENT_CLICKED, NULL);
                        
                        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
                            lv_obj_del(wifi_keyboard);
                            wifi_keyboard = nullptr;
                        }, LV_EVENT_CLICKED, NULL);
                    } else {
                        lv_obj_t* ta = lv_obj_get_child(wifi_keyboard, 0);
                        if (ta && lv_obj_check_type(ta, &lv_textarea_class)) {
                            lv_textarea_add_text(ta, txt);
                        }
                    }
                }
            }
        }, LV_EVENT_VALUE_CHANGED, NULL);
        
        lv_obj_set_style_pad_row(kb, 15, 0);     
        lv_obj_set_style_pad_column(kb, 15, 0);  
        
        lv_obj_add_style(kb, &style_keyboard_btn, LV_PART_ITEMS);
        
        lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
            lv_obj_del(wifi_keyboard);
            wifi_keyboard = nullptr;
        }, LV_EVENT_CLICKED, NULL);
    }
}

void connectToWiFi() {
    DEBUG_PRINTF("Connecting to %s with password %s\n", selected_ssid, selected_password);
    
    // Show a connecting dialog with spinner
    lv_obj_t* connecting_box = lv_msgbox_create(wifi_screen, "Connecting", 
        "Connecting to WiFi network...", NULL, false);
    lv_obj_set_size(connecting_box, 250, 150);
    lv_obj_center(connecting_box);
    
    lv_obj_t* spinner = lv_spinner_create(connecting_box, 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    lv_timer_handler(); // Force UI update
    
    // Disconnect from any existing network first
    WiFi.disconnect();
    delay(100);
    
    // Set WiFi mode explicitly
    WiFi.mode(WIFI_STA);
    delay(100);
    
    // Start connection attempt
    DEBUG_PRINT("Starting WiFi connection");
    WiFi.begin(selected_ssid, selected_password);
    
    int attempt = 0;
    bool connected = false;
    
    // Try to connect with timeout (10 seconds)
    while (attempt < 20) {
        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            break;
        }
        delay(500);
        Serial.print(".");
        lv_timer_handler();
        attempt++;
    }
    
    // Clean up the connecting dialog
    lv_obj_del(connecting_box);
    
    if (connected) {
        Serial.println("\nConnected to WiFi!");
        
        // Reset connection attempts counter
        wifiConnectionAttempts = 0;
        manualDisconnect = false; // Reset flag on successful connection
        
        // Save this network to preferences (for backward compatibility with old code)
        preferences.begin("wifi", false);
        preferences.putString("ssid", selected_ssid);
        preferences.putString("password", selected_password);
        preferences.end();
        
        // Add or update the network in savedNetworks
        bool networkExists = false;
        int emptySlot = -1;
        
        for (int i = 0; i < numSavedNetworks; i++) {
            if (strcmp(savedNetworks[i].ssid, selected_ssid) == 0) {
                // Update password if network already exists
                strncpy(savedNetworks[i].password, selected_password, 64);
                savedNetworks[i].password[64] = '\0';
                networkExists = true;
                break;
            }
            // Find an empty slot if we need to add a new network
            if (emptySlot == -1 && strlen(savedNetworks[i].ssid) == 0) {
                emptySlot = i;
            }
        }
        
        // Add new network if it doesn't exist
        if (!networkExists) {
            int idx = emptySlot;
            if (idx == -1) {
                if (numSavedNetworks < MAX_NETWORKS) {
                    idx = numSavedNetworks++;
                } else {
                    idx = MAX_NETWORKS - 1; // Overwrite last slot if full
                }
            }
            strncpy(savedNetworks[idx].ssid, selected_ssid, 32);
            savedNetworks[idx].ssid[32] = '\0';
            strncpy(savedNetworks[idx].password, selected_password, 64);
            savedNetworks[idx].password[64] = '\0';
        }
        
        // Save the updated network list
        saveNetworks();
        
        // Update the WiFi indicator
        updateWifiIndicator();
        
        // Refresh the WiFi Manager screen instead of showing a message box
        createWiFiManagerScreen();
    } else {
        Serial.println("\nFailed to connect to WiFi.");
        
        // Initialize the reconnection mechanism
        lastWiFiConnectionAttempt = millis();
        wifiConnectionAttempts = 1;
        
        // Show detailed error message based on WiFi status
        const char* errorMsg;
        switch (WiFi.status()) {
            case WL_NO_SSID_AVAIL:
                errorMsg = "Network not found.\nCheck if the network is in range.";
                break;
            case WL_CONNECT_FAILED:
                errorMsg = "Connection failed.\nPlease check your password and try again.";
                break;
            case WL_CONNECTION_LOST:
                errorMsg = "Connection lost.\nPlease try again.";
                break;
            default:
                errorMsg = "Failed to connect to the WiFi network.\nPlease check your password and try again.";
                break;
        }
        
        lv_obj_t* error_box = lv_msgbox_create(wifi_screen, "Connection Failed", 
            errorMsg, NULL, true);
        lv_obj_set_size(error_box, 280, 150);
        lv_obj_center(error_box);

        // Store current screen and reload safely
        lv_obj_t* current_screen = wifi_screen;
        wifi_screen = nullptr; // Prevent double deletion
        if (current_screen && lv_obj_is_valid(current_screen)) {
            lv_obj_del_async(current_screen); // Defer deletion
        }
        delay(50); // Small delay for stability
        createWiFiScreen();
    }
}

void createWiFiManagerScreen() {
    // Clean up existing manager screen if it exists
    if (wifi_manager_screen && lv_obj_is_valid(wifi_manager_screen)) {
        lv_obj_del(wifi_manager_screen);
        wifi_manager_screen = nullptr;
    }
    
    wifi_manager_screen = lv_obj_create(NULL);
    if (!wifi_manager_screen) {
        DEBUG_PRINT("Failed to create WiFi manager screen");
        return;
    }
    lv_obj_add_style(wifi_manager_screen, &style_screen, 0);
    
    // Header
    lv_obj_t* header = lv_obj_create(wifi_manager_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Add indicators
    addWifiIndicator(wifi_manager_screen);
    addBatteryIndicator(wifi_manager_screen);

    // Status label
    lv_obj_t* status_label = lv_label_create(wifi_manager_screen);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(status_label, "Saved Networks:");

    // Network list container
    saved_networks_list = lv_obj_create(wifi_manager_screen);
    lv_obj_set_size(saved_networks_list, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 120);
    lv_obj_align(saved_networks_list, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(saved_networks_list, lv_color_hex(0x2D2D2D), 0);
    
    // Populate saved networks
    if (numSavedNetworks == 0) {
        lv_obj_t* no_networks_label = lv_label_create(saved_networks_list);
        lv_label_set_text(no_networks_label, "No saved networks");
        lv_obj_align(no_networks_label, LV_ALIGN_CENTER, 0, 0);
    } else {
        for (int i = 0; i < numSavedNetworks; i++) {
            // Network container
            lv_obj_t* network_container = lv_obj_create(saved_networks_list);
            lv_obj_set_width(network_container, SCREEN_WIDTH - 40);
            lv_obj_set_height(network_container, 60);
            lv_obj_set_style_bg_color(network_container, lv_color_hex(0x3A3A3A), 0);
            lv_obj_set_style_pad_all(network_container, 5, 0);
            lv_obj_set_pos(network_container, 0, i * 70);

            // SSID Label
            lv_obj_t* ssid_label = lv_label_create(network_container);
            String status = (WiFi.status() == WL_CONNECTED && strcmp(WiFi.SSID().c_str(), savedNetworks[i].ssid) == 0) 
                ? " (Connected)" : "";
            String ssid_text = String(savedNetworks[i].ssid) + status;
            lv_label_set_text(ssid_label, ssid_text.c_str());
            lv_obj_align(ssid_label, LV_ALIGN_LEFT_MID, 5, 0);
            lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);

            // Connect Button
            lv_obj_t* connect_btn = lv_btn_create(network_container);
            lv_obj_set_size(connect_btn, 80, 40);
            lv_obj_align(connect_btn, LV_ALIGN_RIGHT_MID, -90, 0);
            lv_obj_add_style(connect_btn, &style_btn, 0);
            lv_obj_add_style(connect_btn, &style_btn_pressed, LV_STATE_PRESSED);
            lv_obj_t* connect_label = lv_label_create(connect_btn);
            lv_label_set_text(connect_label, "Connect");
            lv_obj_center(connect_label);
            lv_obj_set_user_data(connect_btn, (void*)(intptr_t)i);

            // Disconnect Button
            lv_obj_t* disconnect_btn = lv_btn_create(network_container);
            lv_obj_set_size(disconnect_btn, 80, 40);
            lv_obj_align(disconnect_btn, LV_ALIGN_RIGHT_MID, -5, 0);
            lv_obj_add_style(disconnect_btn, &style_btn, 0);
            lv_obj_add_style(disconnect_btn, &style_btn_pressed, LV_STATE_PRESSED);
            lv_obj_t* disconnect_label = lv_label_create(disconnect_btn);
            lv_label_set_text(disconnect_label, "Disconnect");
            lv_obj_center(disconnect_label);
            lv_obj_set_user_data(disconnect_btn, (void*)(intptr_t)i);

            // Connect Button Event
            lv_obj_add_event_cb(connect_btn, [](lv_event_t* e) {
                lv_obj_t* btn = lv_event_get_target(e);
                if (!btn || !lv_obj_is_valid(btn)) {
                    DEBUG_PRINT("Invalid connect button");
                    return;
                }
                int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
                if (idx < 0 || idx >= numSavedNetworks) {
                    DEBUG_PRINT("Invalid network index");
                    return;
                }

                DEBUG_PRINTF("Connecting to: %s\n", savedNetworks[idx].ssid);

                // Show spinner
                lv_obj_t* spinner = lv_spinner_create(lv_scr_act(), 1000, 60);
                lv_obj_set_size(spinner, 40, 40);
                lv_obj_center(spinner);
                lv_timer_handler(); // Force UI update

                // Attempt connection
                WiFi.disconnect();
                delay(100);
                WiFi.mode(WIFI_STA); // Ensure STA mode
                WiFi.begin(savedNetworks[idx].ssid, savedNetworks[idx].password);

                int timeout = 0;
                while (WiFi.status() != WL_CONNECTED && timeout < 20) {
                    delay(500);
                    DEBUG_PRINT(".");
                    timeout++;
                }

                // Reset manualDisconnect on successful connection
                if (WiFi.status() == WL_CONNECTED) {
                    manualDisconnect = false;
                    DEBUG_PRINT("Manual connection successful, auto-reconnect enabled");
                }

                // Clean up spinner
                if (spinner && lv_obj_is_valid(spinner)) {
                    lv_obj_del(spinner);
                }

                updateWifiIndicator();

                // Show result
                const char* msg = (WiFi.status() == WL_CONNECTED) 
                    ? "Connected successfully!" 
                    : "Failed to connect";
                lv_obj_t* msgbox = lv_msgbox_create(NULL, "Connection Status", msg, NULL, true);
                lv_obj_set_size(msgbox, 250, 150);
                lv_obj_center(msgbox);

                // Store current screen and reload safely
                lv_obj_t* current_screen = wifi_manager_screen;
                wifi_manager_screen = nullptr; // Prevent double deletion
                if (current_screen && lv_obj_is_valid(current_screen)) {
                    lv_obj_del_async(current_screen); // Defer deletion
                }
                delay(50); // Small delay for stability
                createWiFiManagerScreen();
            }, LV_EVENT_CLICKED, NULL);

            // Disconnect Button Event
            lv_obj_add_event_cb(disconnect_btn, [](lv_event_t* e) {
                lv_obj_t* btn = lv_event_get_target(e);
                if (!btn || !lv_obj_is_valid(btn)) {
                    DEBUG_PRINT("Invalid disconnect button");
                    return;
                }
                int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
                if (idx < 0 || idx >= numSavedNetworks) {
                    DEBUG_PRINT("Invalid network index");
                    return;
                }

                if (WiFi.status() == WL_CONNECTED && strcmp(WiFi.SSID().c_str(), savedNetworks[idx].ssid) == 0) {
                    DEBUG_PRINTF("Disconnecting from: %s\n", savedNetworks[idx].ssid);
                    WiFi.disconnect();
                    delay(100);
                    manualDisconnect = true; // Prevent auto-reconnect
                    DEBUG_PRINT("Manual disconnect triggered, auto-reconnect disabled");
                    updateWifiIndicator();

                    lv_obj_t* msgbox = lv_msgbox_create(NULL, "Disconnected", 
                        "Disconnected from network", NULL, true);
                    lv_obj_set_size(msgbox, 250, 150);
                    lv_obj_center(msgbox);

                    // Store current screen and reload safely
                    lv_obj_t* current_screen = wifi_manager_screen;
                    wifi_manager_screen = nullptr; // Prevent double deletion
                    if (current_screen && lv_obj_is_valid(current_screen)) {
                        lv_obj_del_async(current_screen); // Defer deletion
                    }
                    delay(50); // Small delay for stability
                    createWiFiManagerScreen();
                } else {
                    DEBUG_PRINT("Not connected to this network or already disconnected");
                }
            }, LV_EVENT_CLICKED, NULL);
        }
    }

    // Add New Network Button
    lv_obj_t* add_btn = lv_btn_create(wifi_manager_screen);
    lv_obj_align(add_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_size(add_btn, 90, 40);
    lv_obj_add_style(add_btn, &style_btn, 0);
    lv_obj_add_style(add_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* add_label = lv_label_create(add_btn);
    lv_label_set_text(add_label, "Add New");
    lv_obj_center(add_label);
    lv_obj_add_event_cb(add_btn, [](lv_event_t* e) {
        DEBUG_PRINT("Add New button pressed");
        createWiFiScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Back Button
    lv_obj_t* back_btn = lv_btn_create(wifi_manager_screen);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_size(back_btn, 90, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        DEBUG_PRINT("Returning to main menu from WiFi Manager");
        createMainMenu();
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(wifi_manager_screen);
    DEBUG_PRINT("WiFi Manager screen loaded");
}

void createWiFiScreen() {
    if (wifi_screen) {
        cleanupWiFiResources();
        lv_obj_del(wifi_screen);
        wifi_screen = nullptr;
    }
    
    wifi_screen = lv_obj_create(NULL);
    lv_obj_add_style(wifi_screen, &style_screen, 0);
    
    // Header
    lv_obj_t* header = lv_obj_create(wifi_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "WiFi Networks");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Add indicators
    addWifiIndicator(wifi_screen);
    addBatteryIndicator(wifi_screen);
    
    // Status label
    wifi_status_label = lv_label_create(wifi_screen);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(wifi_status_label, "Scanning for networks...");
    
    // WiFi list - Make scrollable
    wifi_list = lv_list_create(wifi_screen);
    lv_obj_set_size(wifi_list, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 120); // 120px height
    lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_SCROLLABLE);
    current_scroll_obj = wifi_list; // Set as scrollable object
    DEBUG_PRINTF("WiFi list created at %p, set as current_scroll_obj\n", wifi_list);

    // Refresh button
    lv_obj_t* refresh_btn = lv_btn_create(wifi_screen);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_size(refresh_btn, 90, 40);
    lv_obj_add_style(refresh_btn, &style_btn, 0);
    lv_obj_add_style(refresh_btn, &style_btn_pressed, LV_STATE_PRESSED);
    
    lv_obj_t* refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH " Scan");
    lv_obj_center(refresh_label);
    
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t* e) {
        if (!wifiScanInProgress) {
            scanNetworks();
        } else {
            DEBUG_PRINT("Scan already in progress, ignoring button press");
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Back button
    lv_obj_t* back_btn = lv_btn_create(wifi_screen);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_size(back_btn, 90, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        DEBUG_PRINT("Returning to WiFi Manager from WiFi Screen");
        cleanupWiFiResources();
        lv_obj_t* screen_to_delete = wifi_screen;
        wifi_screen = nullptr;
        if (screen_to_delete != nullptr) {
            lv_obj_del(screen_to_delete);
        }
        createWiFiManagerScreen();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_scr_load(wifi_screen);
    scanNetworks(); // Start scanning immediately
}

String getFormattedEntry(const String& entry) {
    String entryData = entry;
    String timestamp = getTimestamp(); // Default to current timestamp for new entries

    // Check if the entry contains a timestamp (from log file)
    int colonPos = entry.indexOf(": ");
    if (colonPos > 0 && colonPos <= 19 && entry.substring(0, 2).toInt() > 0) {
        // This is a log entry with a timestamp
        timestamp = entry.substring(0, colonPos + 1); // e.g., "05-Mar-2025 14:14:19: "
        entryData = entry.substring(colonPos + 2);    // e.g., "Male,Red+Orange+Blue,..."
        DEBUG_PRINTF("Extracted timestamp: %s\n", timestamp.c_str());
        DEBUG_PRINTF("Extracted entry data: %s\n", entryData.c_str());
    } else {
        // No timestamp; assume this is a new entry (e.g., from confirmation screen)
        entryData = entry;
        DEBUG_PRINTF("No timestamp found, using raw entry: %s\n", entryData.c_str());
    }

    // Parse the entry data into parts
    String parts[5];
    int partCount = 0, startIdx = 0;
    for (int i = 0; i < entryData.length() && partCount < 5; i++) {
        if (entryData.charAt(i) == ',') {
            parts[partCount++] = entryData.substring(startIdx, i);
            startIdx = i + 1;
        }
    }
    if (startIdx < entryData.length()) {
        parts[partCount++] = entryData.substring(startIdx);
    }

    // Format the output
    String formatted = "Time: " + timestamp + "\n";
    formatted += "Gender: " + (partCount > 0 ? parts[0] : "N/A") + "\n";
    formatted += "Shirt: " + (partCount > 1 ? parts[1] : "N/A") + "\n";
    formatted += "Pants: " + (partCount > 2 ? parts[2] : "N/A") + "\n";
    formatted += "Shoes: " + (partCount > 3 ? parts[3] : "N/A") + "\n";
    formatted += "Item: " + (partCount > 4 ? parts[4] : "N/A");

    DEBUG_PRINTF("Formatted: %s\n", formatted.c_str());
    return formatted;
}

String getTimestamp() {
    m5::rtc_date_t DateStruct;
    m5::rtc_time_t TimeStruct;
    M5.Rtc.getDate(&DateStruct);
    M5.Rtc.getTime(&TimeStruct);

    struct tm timeinfo = {0};
    timeinfo.tm_year = DateStruct.year - 1900; // Years since 1900
    timeinfo.tm_mon = DateStruct.month - 1;     // Months 0-11
    timeinfo.tm_mday = DateStruct.date;
    timeinfo.tm_hour = TimeStruct.hours;
    timeinfo.tm_min = TimeStruct.minutes;
    timeinfo.tm_sec = TimeStruct.seconds;
    timeinfo.tm_isdst = -1;

    if (timeinfo.tm_year > 70) { // Valid time (year > 1970)
        char buffer[25];
        strftime(buffer, sizeof(buffer), "%d-%b-%Y %H:%M:%S", &timeinfo); // e.g., "05-Mar-2025 08:12:24"
        return String(buffer);
    }
    return "NoTime"; // Fallback if RTC isn’t set
}

bool appendToLog(const String& entry) {
    if (!fileSystemInitialized) {
        DEBUG_PRINT("File system not initialized, attempting to initialize...");
        initFileSystem();
        if (!fileSystemInitialized) {
            DEBUG_PRINT("File system not initialized, cannot save entry");
            return false;
        }
    }
    
    SPI.end();
    delay(100);
    SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, -1);
    pinMode(SD_SPI_CS_PIN, OUTPUT);
    digitalWrite(SD_SPI_CS_PIN, HIGH);
    delay(100);
    
    File file = SD.open(LOG_FILENAME, FILE_APPEND);
    if (!file) {
        DEBUG_PRINT("Failed to open log file for writing");
        SPI_SD.end();
        SPI.begin();
        pinMode(TFT_DC, OUTPUT);
        digitalWrite(TFT_DC, HIGH);
        return false;
    }
    
    String timestamp = getTimestamp();
    String formattedEntry = timestamp + ": " + entry; // e.g., "05-Mar-2025 14:31:53: Male,Blue+Green+Red+Orange,..."
    DEBUG_PRINTF("Raw entry text: %s\n", formattedEntry.c_str());
    size_t bytesWritten = file.println(formattedEntry);
    file.close();
    
    SPI_SD.end();
    SPI.begin();
    pinMode(TFT_DC, OUTPUT);
    digitalWrite(TFT_DC, HIGH);
    
    if (bytesWritten == 0) {
        DEBUG_PRINT("Failed to write to log file");
        return false;
    }
    
    DEBUG_PRINTF("Entry saved to file (%d bytes): %s\n", bytesWritten, formattedEntry.c_str());
    return true;
}

void createViewLogsScreen() {
    DEBUG_PRINT("Creating new view logs screen");

    parsedLogEntries.clear();

    lv_obj_t* logs_screen = lv_obj_create(NULL);
    lv_obj_add_style(logs_screen, &style_screen, 0);

    addWifiIndicator(logs_screen);
    addBatteryIndicator(logs_screen);

    lv_obj_t* header = lv_obj_create(logs_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 40);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Log Entries - Last 3 Days");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_center(title);

    lv_obj_t* tabview = lv_tabview_create(logs_screen, LV_DIR_TOP, 30);
    lv_obj_set_size(tabview, SCREEN_WIDTH, SCREEN_HEIGHT - 40);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x2D2D2D), 0);

    time_t now;
    time(&now);
    if (WiFi.status() == WL_CONNECTED) syncTimeWithNTP();
    struct tm* timeinfo = localtime(&now);
    char current_time[25]; // Increased size for new format
    strftime(current_time, sizeof(current_time), "%d-%b-%Y %H:%M:%S", timeinfo);
    DEBUG_PRINTF("Current system time: %s\n", current_time);

    if (!fileSystemInitialized) {
        DEBUG_PRINT("Filesystem not initialized, attempting to initialize");
        initFileSystem();
    }

    if (fileSystemInitialized) {
        SPI.end();
        delay(100);
        SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
        digitalWrite(SD_SPI_CS_PIN, HIGH);
        delay(100);

        if (!SD.exists(LOG_FILENAME)) {
            File file = SD.open(LOG_FILENAME, FILE_WRITE);
            if (file) {
                file.println("# Loss Prevention Log - Created " + getTimestamp());
                file.close();
                DEBUG_PRINT("Created new log file");
            }
        }

        File log_file = SD.open(LOG_FILENAME, FILE_READ);
        if (log_file) {
            DEBUG_PRINT("Reading log file anew...");
            while (log_file.available()) {
                String line = log_file.readStringUntil('\n');
                line.trim();
                if (line.length() > 0 && !line.startsWith("#")) {
                    time_t timestamp = parseTimestamp(line);
                    if (timestamp != 0) {
                        parsedLogEntries.push_back({line, timestamp});
                        DEBUG_PRINTF("Parsed entry: %s\n", line.c_str());
                    } else {
                        DEBUG_PRINTF("Failed to parse timestamp: %s\n", line.c_str());
                    }
                }
            }
            log_file.close();
            DEBUG_PRINTF("Read %d valid log entries from file\n", parsedLogEntries.size());
        } else {
            DEBUG_PRINT("Failed to open log file for reading");
        }

        SPI_SD.end();
        SPI.begin();
        pinMode(TFT_DC, OUTPUT);
        digitalWrite(TFT_DC, HIGH);
    }

    char tab_names[3][16];
    lv_obj_t* tabs[3];
    std::vector<LogEntry*> entries_by_day[3];

    for (int i = 0; i < 3; i++) {
        struct tm day_time = *timeinfo;
        day_time.tm_mday -= i;
        day_time.tm_hour = 0;
        day_time.tm_min = 0;
        day_time.tm_sec = 0;
        mktime(&day_time);

        strftime(tab_names[i], sizeof(tab_names[i]), "%m/%d/%y", &day_time);
        tabs[i] = lv_tabview_add_tab(tabview, tab_names[i]);

        time_t day_start = mktime(&day_time);
        day_time.tm_hour = 23;
        day_time.tm_min = 59;
        day_time.tm_sec = 59;
        mktime(&day_time);
        time_t day_end = mktime(&day_time);

        char day_start_str[25], day_end_str[25];
        strftime(day_start_str, sizeof(day_start_str), "%d-%b-%Y %H:%M:%S", localtime(&day_start));
        strftime(day_end_str, sizeof(day_end_str), "%d-%b-%Y %H:%M:%S", localtime(&day_end));
        DEBUG_PRINTF("Filtering %s: %s to %s\n", tab_names[i], day_start_str, day_end_str);

        for (auto& entry : parsedLogEntries) {
            if (entry.timestamp >= day_start && entry.timestamp <= day_end) {
                entries_by_day[i].push_back(&entry);
                DEBUG_PRINTF("Matched entry for %s: %s\n", tab_names[i], entry.text.c_str());
            }
        }

        std::sort(entries_by_day[i].begin(), entries_by_day[i].end(), 
            [](const LogEntry* a, const LogEntry* b) {
                return a->timestamp < b->timestamp;
            });

        DEBUG_PRINTF("Day %s: %d entries\n", tab_names[i], entries_by_day[i].size());
    }

    for (int i = 0; i < 3; i++) {
        lv_obj_t* container = lv_obj_create(tabs[i]);
        lv_obj_set_size(container, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 70);
        lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_scroll_dir(container, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);

        if (entries_by_day[i].empty()) {
            lv_obj_t* no_logs = lv_label_create(container);
            lv_label_set_text(no_logs, "No entries for this day");
            lv_obj_add_style(no_logs, &style_text, 0);
            lv_obj_center(no_logs);
        } else {
            int y_pos = 0;
            int entry_num = 1;
            for (const auto* entry : entries_by_day[i]) {
                lv_obj_t* entry_btn = lv_btn_create(container);
                lv_obj_set_size(entry_btn, SCREEN_WIDTH - 30, 25);
                lv_obj_set_pos(entry_btn, 5, y_pos);
                lv_obj_add_style(entry_btn, &style_btn, 0);
                lv_obj_add_style(entry_btn, &style_btn_pressed, LV_STATE_PRESSED);
                lv_obj_set_style_bg_color(entry_btn, lv_color_hex(0x3A3A3A), 0);

                struct tm* entry_time = localtime(&entry->timestamp);
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M", entry_time);
                String entry_label = "Entry " + String(entry_num++) + ": " + String(time_str);

                lv_obj_t* label = lv_label_create(entry_btn);
                lv_label_set_text(label, entry_label.c_str());
                lv_obj_add_style(label, &style_text, 0);
                lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0);

                lv_obj_set_user_data(entry_btn, (void*)entry);

                lv_obj_add_event_cb(entry_btn, [](lv_event_t* e) {
                    lv_obj_t* btn = lv_event_get_target(e);
                    LogEntry* entry = (LogEntry*)lv_obj_get_user_data(btn);
                    if (entry) {
                        DEBUG_PRINTF("Raw entry text: %s\n", entry->text.c_str());
                        int colon_pos = entry->text.indexOf(": ");
                        if (colon_pos != -1) {
                            String entry_data = entry->text.substring(colon_pos + 2); // Extract raw data after timestamp
                            DEBUG_PRINTF("Extracted entry data: %s\n", entry_data.c_str());
                            String formatted_entry = getFormattedEntry(entry_data);
                            DEBUG_PRINTF("Formatted: %s\n", formatted_entry.c_str());
                            lv_obj_t* msgbox = lv_msgbox_create(NULL, "Log Entry Details", 
                                formatted_entry.c_str(), NULL, true);
                            lv_obj_set_size(msgbox, 280, 180);
                            lv_obj_center(msgbox);
                            lv_obj_set_style_text_font(lv_msgbox_get_text(msgbox), &lv_font_montserrat_14, 0);
                        } else {
                            DEBUG_PRINT("No colon found in entry text");
                            lv_obj_t* msgbox = lv_msgbox_create(NULL, "Error", 
                                "Invalid log entry format", NULL, true);
                            lv_obj_set_size(msgbox, 280, 180);
                            lv_obj_center(msgbox);
                        }
                    }
                }, LV_EVENT_CLICKED, NULL);

                y_pos += 30;
            }
        }
    }

    lv_obj_t* back_btn = lv_btn_create(logs_screen);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 0);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    lv_obj_move_foreground(back_btn);

    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        DEBUG_PRINT("Returning to main menu from logs");
        createMainMenu();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* reset_btn = lv_btn_create(logs_screen);
    lv_obj_set_size(reset_btn, 80, 40);
    lv_obj_align(reset_btn, LV_ALIGN_TOP_RIGHT, -10, 0);
    lv_obj_add_style(reset_btn, &style_btn, 0);
    lv_obj_add_style(reset_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset");
    lv_obj_center(reset_label);
    lv_obj_move_foreground(reset_btn);

    lv_obj_add_event_cb(reset_btn, [](lv_event_t* e) {
        static const char* btns[] = {"Yes", "No", ""};
        lv_obj_t* msgbox = lv_msgbox_create(NULL, "Confirm Reset", 
            "Are you sure you want to delete all log entries?", btns, false);
        lv_obj_set_size(msgbox, 250, 150);
        lv_obj_center(msgbox);

        lv_obj_add_event_cb(msgbox, [](lv_event_t* e) {
            lv_obj_t* obj = lv_event_get_current_target(e);
            const char* btn_text = lv_msgbox_get_active_btn_text(obj);
            if (btn_text && strcmp(btn_text, "Yes") == 0) {
                SPI.end();
                delay(100);
                SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
                digitalWrite(SD_SPI_CS_PIN, HIGH);
                delay(100);

                if (SD.remove(LOG_FILENAME)) {
                    File file = SD.open(LOG_FILENAME, FILE_WRITE);
                    if (file) {
                        file.println("# Loss Prevention Log - Reset " + getTimestamp());
                        file.close();
                        DEBUG_PRINT("Log file reset successfully");
                    }
                } else {
                    DEBUG_PRINT("Failed to reset log file");
                }

                SPI_SD.end();
                SPI.begin();
                pinMode(TFT_DC, OUTPUT);
                digitalWrite(TFT_DC, HIGH);

                parsedLogEntries.clear();
                lv_obj_t* screen_to_delete = lv_scr_act();
                createViewLogsScreen();
                if (screen_to_delete && screen_to_delete != lv_scr_act()) {
                    lv_obj_del(screen_to_delete);
                }
            }
            lv_msgbox_close(obj);
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(logs_screen);
    lv_timer_handler();
    DEBUG_PRINT("View logs screen created successfully");
}

void initStyles() {
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(0x1E1E1E));
    lv_style_set_text_color(&style_screen, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&style_screen, &lv_font_montserrat_14);

    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 10);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x00C4B4));
    lv_style_set_bg_grad_color(&style_btn, lv_color_hex(0xFF00FF));
    lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_HOR);
    lv_style_set_border_width(&style_btn, 0);
    lv_style_set_text_color(&style_btn, lv_color_hex(0xFFFFFF));  
    lv_style_set_text_font(&style_btn, &lv_font_montserrat_16);
    lv_style_set_pad_all(&style_btn, 10);

    lv_style_init(&style_btn_pressed);
    lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(0xFF00FF));
    lv_style_set_bg_grad_color(&style_btn_pressed, lv_color_hex(0x00C4B4));
    lv_style_set_bg_grad_dir(&style_btn_pressed, LV_GRAD_DIR_HOR);

    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&style_title, &lv_font_montserrat_20);
    
    lv_style_init(&style_text);
    lv_style_set_text_color(&style_text, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&style_text, &lv_font_montserrat_14);
    
    // Initialize keyboard button style
    lv_style_init(&style_keyboard_btn);
    lv_style_set_radius(&style_keyboard_btn, 8);
    lv_style_set_border_width(&style_keyboard_btn, 2);
    lv_style_set_border_color(&style_keyboard_btn, lv_color_hex(0x888888));
    lv_style_set_pad_all(&style_keyboard_btn, 5);
    lv_style_set_bg_color(&style_keyboard_btn, lv_color_hex(0x333333));
    lv_style_set_text_color(&style_keyboard_btn, lv_color_hex(0xFFFFFF));
}

void cleanupWiFiResources() {
    DEBUG_PRINT("Cleaning up WiFi resources");
    
    // Delete the spinner if it exists
    if (g_spinner != nullptr) {
        lv_obj_del(g_spinner);
        g_spinner = nullptr;
    }
    
    // Delete the scan timer if it exists
    if (scan_timer != nullptr) {
        lv_timer_del(scan_timer);
        scan_timer = nullptr;
    }
    
    // Clean up keyboard if it exists
    if (wifi_keyboard != nullptr) {
        lv_obj_del(wifi_keyboard);
        wifi_keyboard = nullptr;
    }
    
    // Clean up the WiFi list and free memory for network buttons
    if (wifi_list != nullptr) {
        // Check if wifi_list is still a valid object before accessing its children
        if (lv_obj_is_valid(wifi_list)) {
            uint32_t child_count = lv_obj_get_child_cnt(wifi_list);
            for (uint32_t i = 0; i < child_count; i++) {
                lv_obj_t* btn = lv_obj_get_child(wifi_list, i);
                if (btn != nullptr && lv_obj_is_valid(btn)) {
                    // Get user data safely - only free if not NULL
                    void* user_data = lv_obj_get_user_data(btn);
                    if (user_data != nullptr) {
                        // Only free memory that was dynamically allocated
                        // Check if the pointer is not just a cast of an integer
                        if ((intptr_t)user_data > 100) {  // Assuming small integers are used as indices
                            lv_mem_free(user_data);
                        }
                        lv_obj_set_user_data(btn, NULL); // Clear the pointer after freeing
                    }
                }
            }
        }
        // We don't delete wifi_list here as it's a child of wifi_screen
        // and will be deleted when wifi_screen is deleted
    }
    
    // Reset scan state
    wifiScanInProgress = false;
    
    // Cancel any ongoing WiFi scan
    WiFi.scanDelete();
    
    // Reset batch processing variables
    currentBatch = 0;
    totalNetworksFound = 0;
    
    // Ensure we're connected to the previous network if we have credentials
    if (WiFi.status() != WL_CONNECTED && strlen(selected_ssid) > 0 && strlen(selected_password) > 0) {
        DEBUG_PRINT("Reconnecting to previous network during cleanup");
        WiFi.begin(selected_ssid, selected_password);
    }
}

void loadSavedNetworks() {
    Preferences prefs;
    prefs.begin("wifi_config", false);
    
    numSavedNetworks = prefs.getInt("numNetworks", 0);
    numSavedNetworks = min(numSavedNetworks, MAX_NETWORKS);
    
    for (int i = 0; i < numSavedNetworks; i++) {
        char ssidKey[16];
        char passKey[16];
        sprintf(ssidKey, "ssid%d", i);
        sprintf(passKey, "pass%d", i);
        
        String ssid = prefs.getString(ssidKey, "");
        String pass = prefs.getString(passKey, "");
        
        strncpy(savedNetworks[i].ssid, ssid.c_str(), 32);
        savedNetworks[i].ssid[32] = '\0';
        strncpy(savedNetworks[i].password, pass.c_str(), 64);
        savedNetworks[i].password[64] = '\0';
    }
    
    if (numSavedNetworks == 0) {
        strncpy(savedNetworks[0].ssid, DEFAULT_SSID, 32);
        strncpy(savedNetworks[0].password, DEFAULT_PASS, 64);
        numSavedNetworks = 1;
        saveNetworks();
    }
    
    prefs.end();
}

void saveNetworks() {
    Preferences prefs;
    prefs.begin("wifi_config", false);
    
    prefs.putInt("numNetworks", numSavedNetworks);
    
    for (int i = 0; i < numSavedNetworks; i++) {
        char ssidKey[16];
        char passKey[16];
        
        sprintf(ssidKey, "ssid%d", i);
        sprintf(passKey, "pass%d", i);
        
        prefs.putString(ssidKey, savedNetworks[i].ssid);
        prefs.putString(passKey, savedNetworks[i].password);
        
        DEBUG_PRINTF("Saved network %d: %s\n", i, savedNetworks[i].ssid);
    }
    
    prefs.end();
}

void listSavedEntries() {
    if (!fileSystemInitialized) {
        initFileSystem();
    }
    
    // Switch to SD card mode
    SPI.end();
    delay(100);
    SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, -1); // -1 means no default CS
    pinMode(SD_SPI_CS_PIN, OUTPUT); // Set CS pin (4) as OUTPUT
    digitalWrite(SD_SPI_CS_PIN, HIGH); // Deselect SD card (HIGH = off)
    delay(100);
    
    if (!SD.exists(LOG_FILENAME)) {
        println_log("Log file does not exist");
        
        // Switch back to LCD mode
        SPI_SD.end();
        SPI.begin();
        pinMode(TFT_DC, OUTPUT);
        digitalWrite(TFT_DC, HIGH);
        return;
    }
    
    File file = SD.open(LOG_FILENAME, FILE_READ);
    if (!file) {
        println_log("Failed to open log file for reading");
        
        // Switch back to LCD mode
        SPI_SD.end();
        SPI.begin();
        pinMode(TFT_DC, OUTPUT);
        digitalWrite(TFT_DC, HIGH);
        return;
    }
    
    println_log("Saved entries:");
    while (file.available()) {
        String line = file.readStringUntil('\n');
        println_log(line.c_str());
    }
    file.close();
    
    // Switch back to LCD mode
    SPI_SD.end();
    SPI.begin();
    pinMode(TFT_DC, OUTPUT);
    digitalWrite(TFT_DC, HIGH);
}

void connectToSavedNetworks() {
    DEBUG_PRINT("Attempting to connect to saved networks");
    
    if (numSavedNetworks == 0 || WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINT("No saved networks or already connected");
        return;
    }
    
    WiFi.disconnect();
    delay(100);
    
    bool connected = false;
    
    for (int i = 0; i < numSavedNetworks; i++) {
        DEBUG_PRINTF("Trying to connect to %s\n", savedNetworks[i].ssid);
        
        WiFi.begin(savedNetworks[i].ssid, savedNetworks[i].password);
        
        int timeout = 0;
        while (WiFi.status() != WL_CONNECTED && timeout < 20) {
            delay(500);
            DEBUG_PRINT(".");
            timeout++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            DEBUG_PRINTF("Connected to %s\n", savedNetworks[i].ssid);
            connected = true;
            wifiConnectionAttempts = 0;
            break;
        } else {
            DEBUG_PRINTF("Failed to connect to %s\n", savedNetworks[i].ssid);
            lastWiFiConnectionAttempt = millis();
            wifiConnectionAttempts = 1;
        }
    }
    
    if (!connected) {
        DEBUG_PRINT("Could not connect to any saved networks");
    }
}

void saveEntry(const String& entry) {
    Serial.println("New Entry:");
    Serial.println(entry);
    
    bool saved = appendToLog(entry); // Save to SD card
    
    if (WiFi.status() == WL_CONNECTED) {
        sendWebhook(entry); // Send to Discord
        if (saved) {
            updateStatus("Entry Saved & Sent", 0x00FF00);
        } else {
            updateStatus("Error Saving Entry", 0xFF0000);
        }
    } else {
        if (saved) {
            updateStatus("Offline: Entry Saved Locally", 0xFFFF00);
        } else {
            updateStatus("Error: Failed to Save Entry", 0xFF0000);
        }
    }
}

// Helper functions for logging to both Serial and Display
void println_log(const char *str) {
    Serial.println(str);
    // If we have a status label, update it
    if (status_bar != nullptr) {
        lv_label_set_text(status_bar, str);
    }
}

void printf_log(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, 256, format, args);
    va_end(args);
    Serial.print(buf);
    // If we have a status label, update it
    if (status_bar != nullptr) {
        lv_label_set_text(status_bar, buf);
    }
}
void sendWebhook(const String& entry) {
    if (WiFi.status() != WL_CONNECTED) {
        DEBUG_PRINT("WiFi not connected, skipping webhook");
        return;
    }

    HTTPClient http;
    const char* zapierUrl = "https://hooks.zapier.com/hooks/catch/21957602/2qk3799/"; // Replace with your Zapier URL
    Serial.println("Starting HTTP client...");
    DEBUG_PRINTF("Webhook URL: %s\n", zapierUrl);
    http.setReuse(false);
    if (!http.begin(zapierUrl)) {
        DEBUG_PRINT("Failed to begin HTTP client");
        return;
    }
    http.addHeader("Content-Type", "application/json");

    // Structure payload as key-value pairs for Zapier
    String timestamp = getTimestamp();
    String jsonPayload = "{";
    jsonPayload += "\"timestamp\":\"" + timestamp + "\",";
    int commaIndex = entry.indexOf(",");
    int lastIndex = 0;
    jsonPayload += "\"gender\":\"" + entry.substring(lastIndex, commaIndex) + "\",";
    lastIndex = commaIndex + 1;
    commaIndex = entry.indexOf(",", lastIndex);
    jsonPayload += "\"shirt\":\"" + entry.substring(lastIndex, commaIndex) + "\",";
    lastIndex = commaIndex + 1;
    commaIndex = entry.indexOf(",", lastIndex);
    jsonPayload += "\"pants\":\"" + entry.substring(lastIndex, commaIndex) + "\",";
    lastIndex = commaIndex + 1;
    commaIndex = entry.indexOf(",", lastIndex);
    jsonPayload += "\"shoes\":\"" + entry.substring(lastIndex, commaIndex) + "\",";
    lastIndex = commaIndex + 1;
    jsonPayload += "\"item\":\"" + entry.substring(lastIndex) + "\"";
    jsonPayload += "}";

    DEBUG_PRINTF("Sending webhook payload: %s\n", jsonPayload.c_str());
    int httpCode = http.POST(jsonPayload);
    DEBUG_PRINTF("HTTP response code: %d\n", httpCode);
    if (httpCode > 0) {
        if (httpCode == 200 || httpCode == 201) { // Zapier returns 200/201
            DEBUG_PRINT("Webhook sent successfully");
            updateStatus("Webhook Sent", 0x00FF00);
        } else {
            String response = http.getString();
            DEBUG_PRINTF("Webhook failed, response: %s\n", response.c_str());
            updateStatus("Webhook Failed", 0xFF0000);
        }
    } else {
        DEBUG_PRINTF("HTTP POST failed, error: %s\n", http.errorToString(httpCode).c_str());
        updateStatus("Webhook Failed", 0xFF0000);
    }
    http.end();
}
void createSettingsMenu() {
    // Clean up existing screens
    if (settingsScreen && lv_obj_is_valid(settingsScreen)) {
        DEBUG_PRINT("Cleaning existing settings screen");
        lv_obj_del_async(settingsScreen);
        settingsScreen = nullptr;
    }

    // Create new settings screen
    settingsScreen = lv_obj_create(NULL);
    if (!settingsScreen) {
        DEBUG_PRINT("Failed to create settings screen!");
        return;
    }
    
    lv_obj_add_style(settingsScreen, &style_screen, 0);
    lv_scr_load(settingsScreen);
    addStatusBar(settingsScreen);
    addWifiIndicator(settingsScreen);
    addBatteryIndicator(settingsScreen);
    DEBUG_PRINTF("Settings screen created: %p\n", settingsScreen);

    // Header
    lv_obj_t* header = lv_obj_create(settingsScreen);
    lv_obj_set_size(header, 320, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Settings");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // WiFi Toggle Button
    lv_obj_t* wifiToggleBtn = lv_btn_create(settingsScreen);
    lv_obj_set_size(wifiToggleBtn, 280, 40);
    lv_obj_align(wifiToggleBtn, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_style(wifiToggleBtn, &style_btn, 0);
    lv_obj_add_style(wifiToggleBtn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* wifiLabel = lv_label_create(wifiToggleBtn);
    lv_label_set_text(wifiLabel, wifiEnabled ? "WiFi: On" : "WiFi: Off");
    lv_obj_center(wifiLabel);
    lv_obj_add_event_cb(wifiToggleBtn, [](lv_event_t* e) {
        wifiEnabled = !wifiEnabled;
        lv_obj_t* label = lv_obj_get_child(lv_event_get_target(e), 0);
        lv_label_set_text(label, wifiEnabled ? "WiFi: On" : "WiFi: Off");
        if (!wifiEnabled) {
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
            DEBUG_PRINT("WiFi disabled");
        } else {
            WiFi.mode(WIFI_STA);
            connectToSavedNetworks(); // Assuming this function exists from earlier suggestions
            DEBUG_PRINT("WiFi enabled");
        }
        updateWifiIndicator();
    }, LV_EVENT_CLICKED, NULL);

    // Brightness Slider
    lv_obj_t* brightnessLabel = lv_label_create(settingsScreen);
    lv_label_set_text(brightnessLabel, "Display Brightness");
    lv_obj_align(brightnessLabel, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_text_font(brightnessLabel, &lv_font_montserrat_14, 0);

    lv_obj_t* brightnessSlider = lv_slider_create(settingsScreen);
    lv_obj_set_width(brightnessSlider, 260);
    lv_obj_align(brightnessSlider, LV_ALIGN_TOP_MID, 0, 135);
    lv_slider_set_range(brightnessSlider, 10, 255); // Min 10 to avoid complete blackout
    lv_slider_set_value(brightnessSlider, displayBrightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightnessSlider, [](lv_event_t* e) {
        displayBrightness = lv_slider_get_value(lv_event_get_target(e));
        M5.Display.setBrightness(displayBrightness);
        DEBUG_PRINTF("Brightness set to %d\n", displayBrightness);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Sync Time Button
    lv_obj_t* syncTimeBtn = lv_btn_create(settingsScreen);
    lv_obj_set_size(syncTimeBtn, 280, 40);
    lv_obj_align(syncTimeBtn, LV_ALIGN_TOP_MID, 0, 185);
    lv_obj_add_style(syncTimeBtn, &style_btn, 0);
    lv_obj_add_style(syncTimeBtn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* syncLabel = lv_label_create(syncTimeBtn);
    lv_label_set_text(syncLabel, "Sync Time with NTP");
    lv_obj_center(syncLabel);
    lv_obj_add_event_cb(syncTimeBtn, [](lv_event_t* e) {
        if (wifiEnabled && WiFi.status() == WL_CONNECTED) {
            syncTimeWithNTP();
            updateStatus("Time Synced", 0x00FF00);
            DEBUG_PRINT("Manual NTP sync triggered");
        } else {
            updateStatus("WiFi Not Connected", 0xFF0000);
            DEBUG_PRINT("Cannot sync time: WiFi off or not connected");
        }
    }, LV_EVENT_CLICKED, NULL);

    // Back Button
    lv_obj_t* backBtn = lv_btn_create(settingsScreen);
    lv_obj_set_size(backBtn, 280, 40);
    lv_obj_align(backBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_style(backBtn, &style_btn, 0);
    lv_obj_add_style(backBtn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, "Back");
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backBtn, [](lv_event_t* e) { createMainMenu(); }, LV_EVENT_CLICKED, NULL);

    lv_timer_handler(); // Process UI updates
}