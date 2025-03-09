#include "WiFiManager.h"
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

// Global WiFiManager instance
WiFiManager wifiManager;

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
void onWiFiScanComplete(const std::vector<NetworkInfo>& results);
void onWiFiStatus(WiFiState state, const String& message);


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
const unsigned long SCAN_TIMEOUT = 30000; // 30 seconds timeout for WiFi scan

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
// LVGL semaphore for thread safety (from CoreS3 User Demo)
SemaphoreHandle_t xGuiSemaphore;
#define LV_TICK_PERIOD_MS 10

// Default WiFi credentials (will be overridden by saved networks)
const char DEFAULT_SSID[] = "Wack House";
const char DEFAULT_PASS[] = "justice69";

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

// LVGL tick task from CoreS3 User Demo (modified for compatibility)
static void lvgl_tick_task(void *arg) {
    (void)arg;
    // Use lv_tick_get instead of lv_tick_inc which is not available in this LVGL version
    static uint32_t last_tick = 0;
    uint32_t current_tick = millis();
    if (current_tick - last_tick > LV_TICK_PERIOD_MS) {
        last_tick = current_tick;
        lv_task_handler(); // Process LVGL tasks
    }
}

// Menu options
const char* genders[] = {"Male", "Female"};

// Global next button pointers
lv_obj_t* main_menu_screen = nullptr;
lv_obj_t* shirt_next_btn = nullptr;
lv_obj_t* pants_next_btn = nullptr;
lv_obj_t* shoes_next_btn = nullptr;
lv_obj_t* settings_screen = nullptr;

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

// SPI-safe file system initialization
// SPI-safe file system initialization
void initFileSystem() {
    if (fileSystemInitialized) return;
    DEBUG_PRINT("Initializing SD card...");
    
    WiFi.disconnect(); // Ensure WiFi is off during SPI switch
    SPI.end();
    delay(200);
    SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, -1);
    pinMode(SD_SPI_CS_PIN, OUTPUT);
    digitalWrite(SD_SPI_CS_PIN, HIGH);
    delay(200);

    bool sdInitialized = false;
    for (int i = 0; i < 3 && !sdInitialized; i++) {
        sdInitialized = SD.begin(SD_SPI_CS_PIN, SPI_SD, 2000000);
        if (!sdInitialized) delay(100);
    }

    if (!sdInitialized) {
        DEBUG_PRINT("SD card initialization failed");
        lv_obj_t* msgbox = lv_msgbox_create(NULL, "Storage Error", 
            "SD card initialization failed.", NULL, true);
        lv_obj_center(msgbox);
    } else {
        fileSystemInitialized = true;
        DEBUG_PRINT("SD card initialized successfully");
    }

    SPI_SD.end();
    SPI.begin();
    pinMode(TFT_DC, OUTPUT);
    digitalWrite(TFT_DC, HIGH);
    if (wifiManager.isEnabled()) wifiManager.connectToBestNetwork(); // Restore WiFi
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("Serial initialized");
    DEBUG_PRINT("Starting Loss Prevention Log...");

    auto cfg = M5.config();
    Serial.println("Before M5.begin");
    M5.begin(cfg);
    M5.Power.begin();

    // Enable external bus power (PORT A/B/C via BUS_EN)
    M5.Power.setExtOutput(true);
    Serial.println("External bus power enabled: " + String(M5.Power.getExtOutput() ? "Yes" : "No"));

    // Since setVoltage() and AXP2101_ALDO3 aren't available, use direct register writes for ALDO3 (3.3V)
    // ALDO3 is controlled by register 0x94 (voltage) and 0x90 (enable bit 3)
    M5.Power.Axp2101.writeRegister8(0x94, 28); // Set ALDO3 to 3.3V (28 = 3.3V based on init logic: 0.5V + 28 * 0.1V)
    uint8_t reg90 = M5.Power.Axp2101.readRegister8(0x90);
    M5.Power.Axp2101.writeRegister8(0x90, reg90 | 0x08); // Enable ALDO3 (bit 3)
    Serial.printf("ALDO3 configured to 3.3V\n");

    // Check AW9523 PORT0 (BUS_EN)
    uint8_t port0_val = M5.In_I2C.readRegister8(0x58, 0x02, 100000);
    Serial.printf("AW9523 PORT0: 0x%02X (BUS_EN: %d)\n", port0_val, (port0_val & 0x02) >> 1);

    // Check AW9523 PORT1 (BOOST_EN for 5V)
    uint8_t port1_val = M5.In_I2C.readRegister8(0x58, 0x03, 100000);
    Serial.printf("AW9523 PORT1: 0x%02X (BOOST_EN: %d)\n", port1_val, (port1_val & 0x80) >> 7);

    // Monitor battery and VBUS for context
    Serial.printf("Battery Voltage: %d mV\n", M5.Power.getBatteryVoltage());
    Serial.printf("VBUS Voltage: %d mV\n", M5.Power.getVBUSVoltage());

    // Initialize speaker
    M5.Speaker.begin();
    DEBUG_PRINT("Speaker initialized");

    // Load persistent sound settings
    Preferences prefs;
    prefs.begin("settings", false); // Open "settings" namespace in read/write mode
    uint8_t saved_volume = prefs.getUChar("volume", 128); // Default to 128 if not set
    bool sound_enabled = prefs.getBool("sound_enabled", true); // Default to true
    M5.Speaker.setVolume(sound_enabled ? saved_volume : 0); // Apply volume or mute
    DEBUG_PRINTF("Loaded sound settings - Enabled: %d, Volume: %d\n", sound_enabled, saved_volume);
    
    // Load persistent brightness settings
    M5.Display.setBrightness(displayBrightness);
    DEBUG_PRINTF("Loaded Brightness settings - Brightness: %d\n", displayBrightness);
    prefs.end();

    Serial.println("Before lv_init");
    lv_init();
    Serial.println("After lv_init");

    // Initialize LVGL buffer with double buffering for smoother scrolling
    static lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    static lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_WIDTH * 40);
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

    // Create semaphore for LVGL thread safety
    xGuiSemaphore = xSemaphoreCreateMutex();

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
    
    DEBUG_PRINTF("Battery Voltage: %f V\n", M5.Power.getBatteryVoltage() / 1000.0); // Adjusted to volts
    DEBUG_PRINTF("Is Charging: %d\n", M5.Power.isCharging());
    DEBUG_PRINTF("Battery Level: %d%%\n", M5.Power.getBatteryLevel());
    
    // Initialize WiFi Manager
    wifiManager.begin();
    wifiManager.setStatusCallback(onWiFiStatus);
    wifiManager.setScanCallback(onWiFiScanComplete);
    loadSavedNetworks();
    DEBUG_PRINT("WiFi Manager initialized");

    initStyles();
    createMainMenu();
    lastLvglTick = millis();

    DEBUG_PRINTF("Free heap after setup: %d bytes\n", ESP.getFreeHeap());

    if (wifiManager.isConnected()) syncTimeWithNTP();
    DEBUG_PRINT("Setup complete!");
}

void loop() {
    M5.update();  // Update M5CoreS3 hardware state (touch, buttons, etc.)
    uint32_t currentMillis = millis();

    // Handle LVGL timing with thread safety - using lv_task_handler for compatibility
    if (currentMillis - lastLvglTick > screenTickPeriod) {
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            lv_task_handler();  // Process LVGL events and updates
            xSemaphoreGive(xGuiSemaphore);
            lastLvglTick = currentMillis;
        }
    }
    
    // Update indicators periodically (every 5 seconds)
    static unsigned long lastIndicatorUpdate = 0;
    if (currentMillis - lastIndicatorUpdate > 5000) {
        updateWifiIndicator();
        updateBatteryIndicator();
        lastIndicatorUpdate = currentMillis;
    }

    // WiFi scan timeout check - will be handled by WiFiManager in the future
    if (wifiScanInProgress && (currentMillis - lastScanStartTime > SCAN_TIMEOUT)) {
        DEBUG_PRINT("WiFi scan timeout detected");
        wifiScanInProgress = false;
        if (wifi_status_label && lv_scr_act() == wifi_screen) {
            lv_label_set_text(wifi_status_label, "Scan timed out. Try again.");
        }
    }

    // Reset connection attempts when connected - will be handled by WiFiManager in the future
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
    if (wifiManager.isConnected() && currentMillis - lastTimeSync > 3600000) {
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
    lgfx::touch_point_t tp[1];
    static bool was_touching = false;
    static int16_t touch_start_x = 0;
    static int16_t touch_start_y = 0;
    static int16_t touch_last_x = 0;
    static int16_t touch_last_y = 0;
    static unsigned long touch_start_time = 0;

    M5.update();
    
    // Get touch points using the CoreS3 User Demo approach
    int nums = M5.Display.getTouchRaw(tp, 1);
    if (nums) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = tp[0].x;
        data->point.y = tp[0].y;
        
        if (!was_touching) {
            touch_start_x = tp[0].x;
            touch_start_y = tp[0].y;
            touch_start_time = millis();
            was_touching = true;
        }
        touch_last_x = tp[0].x;
        touch_last_y = tp[0].y;
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
    if (main_menu_screen) {
        lv_obj_del(main_menu_screen);
        main_menu_screen = nullptr;
    }
    main_menu_screen = lv_obj_create(NULL);
    lv_obj_add_style(main_menu_screen, &style_screen, 0);

    lv_obj_t* header = lv_obj_create(main_menu_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Loss Prevention Logger");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    addWifiIndicator(main_menu_screen);
    addBatteryIndicator(main_menu_screen);

    lv_obj_t* menu_cont = lv_obj_create(main_menu_screen);
    lv_obj_set_size(menu_cont, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 70);
    lv_obj_align(menu_cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(menu_cont, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_layout(menu_cont, LV_LAYOUT_GRID);
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(menu_cont, col_dsc, row_dsc);

    // Button 1: New Entry
    lv_obj_t* new_entry_btn = lv_btn_create(menu_cont);
    lv_obj_add_style(new_entry_btn, &style_btn, 0);
    lv_obj_add_style(new_entry_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(new_entry_btn, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_t* new_entry_label = lv_label_create(new_entry_btn);
    lv_label_set_text(new_entry_label, "New Entry");
    lv_obj_center(new_entry_label);
    lv_obj_add_event_cb(new_entry_btn, [](lv_event_t* e) {
        DEBUG_PRINT("New Entry clicked");
        createGenderMenu(); // Navigate to gender selection screen
    }, LV_EVENT_CLICKED, NULL);

    // Button 2: View Logs
    lv_obj_t* view_logs_btn = lv_btn_create(menu_cont);
    lv_obj_add_style(view_logs_btn, &style_btn, 0);
    lv_obj_add_style(view_logs_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(view_logs_btn, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_t* view_logs_label = lv_label_create(view_logs_btn);
    lv_label_set_text(view_logs_label, "View Logs");
    lv_obj_center(view_logs_label);
    lv_obj_add_event_cb(view_logs_btn, [](lv_event_t* e) {
        DEBUG_PRINT("View Logs clicked");
        createViewLogsScreen(); // Navigate to log viewing screen
    }, LV_EVENT_CLICKED, NULL);

    // Button 3: Settings (already functional)
    lv_obj_t* settings_btn = lv_btn_create(menu_cont);
    lv_obj_add_style(settings_btn, &style_btn, 0);
    lv_obj_add_style(settings_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(settings_btn, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_t* settings_label = lv_label_create(settings_btn);
    lv_label_set_text(settings_label, "Settings");
    lv_obj_center(settings_label);
    lv_obj_add_event_cb(settings_btn, [](lv_event_t* e) {
        createSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Button 4: Placeholder 1
    lv_obj_t* ph1_btn = lv_btn_create(menu_cont);
    lv_obj_add_style(ph1_btn, &style_btn, 0);
    lv_obj_add_style(ph1_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(ph1_btn, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_t* ph1_label = lv_label_create(ph1_btn);
    lv_label_set_text(ph1_label, "Placeholder");
    lv_obj_center(ph1_label);
    lv_obj_add_event_cb(ph1_btn, [](lv_event_t* e) {
        DEBUG_PRINT("Placeholder 1 clicked");
    }, LV_EVENT_CLICKED, NULL);

    // Button 5: Placeholder 2
    lv_obj_t* ph2_btn = lv_btn_create(menu_cont);
    lv_obj_add_style(ph2_btn, &style_btn, 0);
    lv_obj_add_style(ph2_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(ph2_btn, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_t* ph2_label = lv_label_create(ph2_btn);
    lv_label_set_text(ph2_label, "Placeholder");
    lv_obj_center(ph2_label);
    lv_obj_add_event_cb(ph2_btn, [](lv_event_t* e) {
        DEBUG_PRINT("Placeholder 2 clicked");
    }, LV_EVENT_CLICKED, NULL);

    // Button 6: Placeholder 3
    lv_obj_t* ph3_btn = lv_btn_create(menu_cont);
    lv_obj_add_style(ph3_btn, &style_btn, 0);
    lv_obj_add_style(ph3_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(ph3_btn, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_t* ph3_label = lv_label_create(ph3_btn);
    lv_label_set_text(ph3_label, "Placeholder");
    lv_obj_center(ph3_label);
    lv_obj_add_event_cb(ph3_btn, [](lv_event_t* e) {
        DEBUG_PRINT("Placeholder 3 clicked");
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(main_menu_screen);
}

void updateStatus(const char* message, uint32_t color) {
    if (status_bar) {
        lv_label_set_text(status_bar, message);
        lv_obj_set_style_text_color(status_bar, lv_color_hex(color), 0);
    }
}

void updateWifiIndicator() {
    if (!wifiIndicator) return;
    switch (wifiManager.getState()) {
        case WiFiState::WIFI_CONNECTED:
            lv_label_set_text(wifiIndicator, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0x00FF00), 0);
            break;
        case WiFiState::WIFI_CONNECTING:
            lv_label_set_text(wifiIndicator, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0xFFFF00), 0);
            break;
        case WiFiState::WIFI_DISCONNECTED:
        case WiFiState::WIFI_SCANNING:
            lv_label_set_text(wifiIndicator, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0xFF6600), 0);
            break;
        case WiFiState::WIFI_DISABLED:
            lv_label_set_text(wifiIndicator, LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_color(wifiIndicator, lv_color_hex(0xFF0000), 0);
            break;
    }
    lv_obj_move_foreground(wifiIndicator);
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
    lv_obj_set_style_text_font(entry_label, &lv_font_montserrat_14, 0); // Increased font size
    lv_obj_set_style_text_color(entry_label, lv_color_hex(0xFFFFFF), 0); // White text for better contrast
    lv_obj_set_style_bg_color(entry_label, lv_color_hex(0x222222), 0); // Dark background
    lv_obj_set_style_bg_opa(entry_label, LV_OPA_70, 0); // Semi-transparent background
    lv_obj_set_style_radius(entry_label, 5, 0); // Rounded corners
    lv_obj_set_style_pad_all(entry_label, 10, 0); // Add padding around text
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
    
    // Check if WiFi is enabled in settings
    if (!wifiEnabled) {
        DEBUG_PRINT("WiFi is disabled in settings, cannot scan");
        updateStatus("WiFi is disabled in settings", 0xFF9900);
        
        // If we're on the WiFi screen, update the status label
        if (wifi_status_label && lv_obj_is_valid(wifi_status_label)) {
            lv_label_set_text(wifi_status_label, "WiFi is disabled. Enable WiFi in Settings first.");
        }
        return;
    }
    
    // Check WiFi status using WiFiManager
    if (!wifiManager.isInitialized()) {
        DEBUG_PRINT("WiFi module not initialized");
        updateStatus("WiFi module not found", 0xFF0000);
        return;
    } else if (wifiManager.isConnected()) {
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
    
    // Start WiFi scan using WiFiManager
    DEBUG_PRINT("Starting WiFi scan");
    
    // Start the scan using WiFiManager
    bool scanStarted = wifiManager.startScan();
    
    if (!scanStarted) {
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
    
    // Reset batch processing variables
    currentBatch = 0;
    totalNetworksFound = 0;
    
    // If we have credentials, try to reconnect after scan
    if (strlen(selected_ssid) > 0 && strlen(selected_password) > 0) {
        DEBUG_PRINTF("Will reconnect to %s after scan\n", selected_ssid);
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
    DEBUG_PRINTF("Connecting to %s\n", selected_ssid);
    wifiManager.connect(selected_ssid, selected_password, true, 0);
    if (wifi_keyboard) {
        lv_obj_del(wifi_keyboard);
        wifi_keyboard = nullptr;
    }
}

void createWiFiManagerScreen() {
    if (wifi_manager_screen) {
        lv_obj_del(wifi_manager_screen);
        wifi_manager_screen = nullptr;
    }
    wifi_manager_screen = lv_obj_create(NULL);
    lv_obj_add_style(wifi_manager_screen, &style_screen, 0);

    lv_obj_t* header = lv_obj_create(wifi_manager_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    addWifiIndicator(wifi_manager_screen);
    addBatteryIndicator(wifi_manager_screen);

    lv_obj_t* status_label = lv_label_create(wifi_manager_screen);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(status_label, "Saved Networks:");

    saved_networks_list = lv_obj_create(wifi_manager_screen);
    lv_obj_set_size(saved_networks_list, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 120);
    lv_obj_align(saved_networks_list, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(saved_networks_list, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_scroll_dir(saved_networks_list, LV_DIR_VER);

    auto networks = wifiManager.getSavedNetworks();
    for (size_t i = 0; i < networks.size(); i++) {
        lv_obj_t* net_cont = lv_obj_create(saved_networks_list);
        lv_obj_set_size(net_cont, SCREEN_WIDTH - 40, 60);
        lv_obj_set_pos(net_cont, 0, i * 70);
        lv_obj_set_style_bg_color(net_cont, lv_color_hex(0x3A3A3A), 0);

        String status = networks[i].connected ? " (Connected)" : "";
        lv_obj_t* ssid_label = lv_label_create(net_cont);
        lv_label_set_text(ssid_label, (networks[i].ssid + status).c_str());
        lv_obj_align(ssid_label, LV_ALIGN_LEFT_MID, 5, -10);

        lv_obj_t* prio_label = lv_label_create(net_cont);
        lv_label_set_text(prio_label, ("Priority: " + String(networks[i].priority)).c_str());
        lv_obj_align(prio_label, LV_ALIGN_LEFT_MID, 5, 10);

        lv_obj_t* connect_btn = lv_btn_create(net_cont);
        lv_obj_set_size(connect_btn, 80, 40);
        lv_obj_align(connect_btn, LV_ALIGN_RIGHT_MID, -90, 0);
        lv_obj_add_style(connect_btn, &style_btn, 0);
        lv_obj_t* connect_label = lv_label_create(connect_btn);
        lv_label_set_text(connect_label, "Connect");
        lv_obj_center(connect_label);
        lv_obj_add_event_cb(connect_btn, [](lv_event_t* e) {
            lv_obj_t* cont = static_cast<lv_obj_t*>(lv_event_get_user_data(e)); // Cast to lv_obj_t*
            lv_obj_t* label = lv_obj_get_child(cont, 0);
            String ssid = lv_label_get_text(label);
            int idx = ssid.indexOf(" (Connected)");
            if (idx != -1) ssid = ssid.substring(0, idx);
            auto nets = wifiManager.getSavedNetworks();
            for (const auto& net : nets) {
                if (net.ssid == ssid) {
                    wifiManager.connect(net.ssid, net.password, false, net.priority);
                    break;
                }
            }
        }, LV_EVENT_CLICKED, net_cont);

        lv_obj_t* remove_btn = lv_btn_create(net_cont);
        lv_obj_set_size(remove_btn, 80, 40);
        lv_obj_align(remove_btn, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_obj_add_style(remove_btn, &style_btn, 0);
        lv_obj_t* remove_label = lv_label_create(remove_btn);
        lv_label_set_text(remove_label, "Remove");
        lv_obj_center(remove_label);
        lv_obj_add_event_cb(remove_btn, [](lv_event_t* e) {
            lv_obj_t* cont = static_cast<lv_obj_t*>(lv_event_get_user_data(e)); // Cast to lv_obj_t*
            lv_obj_t* label = lv_obj_get_child(cont, 0);
            String ssid = lv_label_get_text(label);
            int idx = ssid.indexOf(" (Connected)");
            if (idx != -1) ssid = ssid.substring(0, idx);
            wifiManager.removeNetwork(ssid);
            createWiFiManagerScreen();
        }, LV_EVENT_CLICKED, net_cont);
    }

    lv_obj_t* add_btn = lv_btn_create(wifi_manager_screen);
    lv_obj_align(add_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_size(add_btn, 90, 40);
    lv_obj_add_style(add_btn, &style_btn, 0);
    lv_obj_t* add_label = lv_label_create(add_btn);
    lv_label_set_text(add_label, "Add New");
    lv_obj_center(add_label);
    lv_obj_add_event_cb(add_btn, [](lv_event_t* e) { createWiFiScreen(); }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* back_btn = lv_btn_create(wifi_manager_screen);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_size(back_btn, 90, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) { createMainMenu(); }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(wifi_manager_screen);
}

void createWiFiScreen() {
    if (wifi_screen) {
        lv_obj_del(wifi_screen);
        wifi_screen = nullptr;
    }
    wifi_screen = lv_obj_create(NULL);
    lv_obj_add_style(wifi_screen, &style_screen, 0);

    lv_obj_t* header = lv_obj_create(wifi_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "WiFi Networks");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    addWifiIndicator(wifi_screen);
    addBatteryIndicator(wifi_screen);

    // Container with subtle shadow
    lv_obj_t* container = lv_obj_create(wifi_screen);
    lv_obj_set_size(container, 300, 180);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x2A2A40), 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_set_style_shadow_color(container, lv_color_hex(0x333333), 0);
    lv_obj_set_style_shadow_width(container, 15, 0);
    lv_obj_set_style_pad_all(container, 20, 0);

    wifi_status_label = lv_label_create(container);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_MID, 0, 5);
    lv_label_set_text(wifi_status_label, "Scanning...");
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xFFFFFF), 0);

    wifi_list = lv_list_create(container);
    lv_obj_set_size(wifi_list, 280, 140);
    lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(0x2A2A40), 0);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);
    current_scroll_obj = wifi_list;

    // Bottom button container
    lv_obj_t* btnContainer = lv_obj_create(wifi_screen);
    lv_obj_set_size(btnContainer, 300, 50);
    lv_obj_align(btnContainer, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(btnContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnContainer, 0, 0);
    lv_obj_set_flex_flow(btnContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* back_btn = lv_btn_create(btnContainer);
    lv_obj_set_size(back_btn, 120, 45);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createWiFiManagerScreen();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* refresh_btn = lv_btn_create(btnContainer);
    lv_obj_set_size(refresh_btn, 120, 45);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_style(refresh_btn, &style_btn, 0);
    lv_obj_add_style(refresh_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, "Scan");
    lv_obj_center(refresh_label);

    lv_obj_add_event_cb(refresh_btn, [](lv_event_t* e) {
        // Clear list and update status
        lv_obj_clean(wifi_list);
        lv_label_set_text(wifi_status_label, "Scanning...");
        
        // Create spinner to indicate scanning
        if (g_spinner == nullptr) {
            g_spinner = lv_spinner_create(wifi_screen, 1000, 60);
            lv_obj_set_size(g_spinner, 50, 50);
            lv_obj_align(g_spinner, LV_ALIGN_CENTER, 0, 0);
        }
        
        // Start scan
        wifiManager.startScan();
        
        // Create a timeout timer
        static uint32_t scan_start_time = 0;
        scan_start_time = millis();
        
        if (scan_timer != nullptr) {
            lv_timer_del(scan_timer);
            scan_timer = nullptr;
        }
        
        scan_timer = lv_timer_create([](lv_timer_t* timer) {
            static uint32_t* start_time_ptr = (uint32_t*)timer->user_data;
            uint32_t elapsed = millis() - *start_time_ptr;
            
            // Check if scan is taking too long (more than 10 seconds)
            if (elapsed > 10000) {
                // Update status label
                if (wifi_status_label && lv_obj_is_valid(wifi_status_label)) {
                    lv_label_set_text(wifi_status_label, "Scan timed out. Try again.");
                }
                
                // Remove spinner
                if (g_spinner && lv_obj_is_valid(g_spinner)) {
                    lv_obj_del(g_spinner);
                    g_spinner = nullptr;
                }
                
                // Delete timer
                lv_timer_del(timer);
                scan_timer = nullptr;
            }
        }, 1000, &scan_start_time);
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(wifi_screen);
    wifiManager.startScan(); // Start scan on screen load
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
    SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, -1); // -1 means no default CS
    pinMode(SD_SPI_CS_PIN, OUTPUT); // Set CS pin (4) as OUTPUT
    digitalWrite(SD_SPI_CS_PIN, HIGH); // Deselect SD card (HIGH = off)
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
    lv_obj_set_size(header, 320, 40);
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
        SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, -1); // -1 means no default CS
        pinMode(SD_SPI_CS_PIN, OUTPUT); // Set CS pin (4) as OUTPUT
        digitalWrite(SD_SPI_CS_PIN, HIGH); // Deselect SD card (HIGH = off)
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
            lv_obj_align(no_logs, LV_ALIGN_CENTER, 0, 0);
        } else {
            int y_pos = 0;
            int entry_num = 1;
            for (const auto* entry : entries_by_day[i]) {
                lv_obj_t* entry_btn = lv_btn_create(container);
                lv_obj_set_size(entry_btn, SCREEN_WIDTH - 30, 25);
                lv_obj_add_style(entry_btn, &style_btn, 0);
                lv_obj_add_style(entry_btn, &style_btn_pressed, LV_STATE_PRESSED);
                lv_obj_set_style_bg_color(entry_btn, lv_color_hex(0x3A3A3A), 0);

                struct tm* entry_time = localtime(&entry->timestamp);
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M", entry_time);
                String entry_label = "Entry " + String(entry_num++) + ": " + String(time_str);

                lv_obj_t* label = lv_label_create(entry_btn);
                lv_label_set_text(label, entry_label.c_str());
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
                SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, -1); // -1 means no default CS
                pinMode(SD_SPI_CS_PIN, OUTPUT); // Set CS pin (4) as OUTPUT
                digitalWrite(SD_SPI_CS_PIN, HIGH); // Deselect SD card (HIGH = off)
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
    // Screen style with gradient background
    lv_style_init(&style_screen);
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
    lv_style_set_bg_color(&style_screen, lv_color_hex(0x1E1E2F)); // Dark blue-gray base
    lv_style_set_bg_grad_color(&style_screen, lv_color_hex(0x3A3A5A)); // Lighter gradient
    lv_style_set_bg_grad_dir(&style_screen, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_screen, 0);

    // Title style
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_24);
    lv_style_set_text_color(&style_title, lv_color_hex(0xFFFFFF));
    lv_style_set_shadow_color(&style_title, lv_color_hex(0xAAAAAA));
    lv_style_set_shadow_width(&style_title, 10);
    lv_style_set_shadow_spread(&style_title, 2);

    // Button style
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x4A90E2)); // Bright blue
    lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn, 0);
    lv_style_set_radius(&style_btn, 8);
    lv_style_set_shadow_color(&style_btn, lv_color_hex(0x333333));
    lv_style_set_shadow_width(&style_btn, 10);
    lv_style_set_text_color(&style_btn, lv_color_white());

    // Button pressed style
    lv_style_init(&style_btn_pressed);
    lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(0x357ABD)); // Darker blue
    lv_style_set_shadow_width(&style_btn_pressed, 5);

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
        DEBUG_PRINTF("Will reconnect to %s after scan\n", selected_ssid);
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

void connectToSavedNetworks() {
    DEBUG_PRINT("Attempting to connect to saved networks");
    
    if (!wifiEnabled) {
        DEBUG_PRINT("WiFi is disabled in settings, not connecting");
        return;
    }
    
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
    int colonPos = entry.indexOf(",");
    int lastIndex = 0;
    jsonPayload += "\"gender\":\"" + entry.substring(lastIndex, colonPos) + "\",";
    lastIndex = colonPos + 1;
    colonPos = entry.indexOf(",", lastIndex);
    jsonPayload += "\"shirt\":\"" + entry.substring(lastIndex, colonPos) + "\",";
    lastIndex = colonPos + 1;
    colonPos = entry.indexOf(",", lastIndex);
    jsonPayload += "\"pants\":\"" + entry.substring(lastIndex, colonPos) + "\",";
    lastIndex = colonPos + 1;
    colonPos = entry.indexOf(",", lastIndex);
    jsonPayload += "\"shoes\":\"" + entry.substring(lastIndex, colonPos) + "\",";
    lastIndex = colonPos + 1;
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

// Callback for status updates
void onWiFiStatus(WiFiState state, const String& message) {
    DEBUG_PRINTF("WiFi Status: %s - %s\n", wifiManager.getStateString().c_str(), message.c_str());
    if (status_bar) {
        lv_label_set_text(status_bar, message.c_str());
        lv_obj_set_style_text_color(status_bar, 
            state == WiFiState::WIFI_CONNECTED ? lv_color_hex(0x00FF00) : 
            state == WiFiState::WIFI_CONNECTING ? lv_color_hex(0xFFFF00) : 
            lv_color_hex(0xFF0000), 0);
    }
    updateWifiIndicator();
}
// Callback for scan results
void onWiFiScanComplete(const std::vector<NetworkInfo>& results) {
    if (wifi_screen && lv_obj_is_valid(wifi_screen)) {
        // Remove spinner if it exists
        if (g_spinner != nullptr) {
            lv_obj_del(g_spinner);
            g_spinner = nullptr;
        }
        
        lv_obj_clean(wifi_list);
        
        if (results.empty()) {
            lv_label_set_text(wifi_status_label, "No networks found");
            return;
        }
        
        lv_label_set_text(wifi_status_label, "Select a network");
        
        for (const auto& net : results) {
            if (net.ssid.isEmpty()) continue; // Skip empty SSIDs
            
            String displayText = net.ssid;
            if (net.encryptionType != WIFI_AUTH_OPEN) {
                displayText += " *";
            }
            
            lv_obj_t* btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, displayText.c_str());
            lv_obj_add_style(btn, &style_btn, 0);
            
            // Add click event
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                const char* text = lv_list_get_btn_text(wifi_list, lv_event_get_target(e));
                String ssid = String(text);
                int idx = ssid.indexOf(" *");
                if (idx != -1) ssid = ssid.substring(0, idx);
                strncpy(selected_ssid, ssid.c_str(), sizeof(selected_ssid) - 1);
                selected_ssid[sizeof(selected_ssid) - 1] = '\0';
                showWiFiKeyboard();
            }, LV_EVENT_CLICKED, NULL);
        }
    lv_label_set_text(wifi_status_label, "Scan complete");
    }
}

void createSettingsScreen() {
    if (settingsScreen) {
        lv_obj_del(settingsScreen);
        settingsScreen = nullptr;
    }
    settingsScreen = lv_obj_create(NULL);
    lv_obj_add_style(settingsScreen, &style_screen, 0);

    // Header with gradient
    lv_obj_t* header = lv_obj_create(settingsScreen);
    lv_obj_set_size(header, SCREEN_WIDTH, 60);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x4A90E2), 0);
    lv_obj_set_style_bg_grad_color(header, lv_color_hex(0x357ABD), 0);
    lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_VER, 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Settings");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    addWifiIndicator(settingsScreen);
    addBatteryIndicator(settingsScreen);

    // Settings container with subtle shadow
    lv_obj_t* container = lv_obj_create(settingsScreen);
    lv_obj_set_size(container, 300, 150);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 95);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x2A2A40), 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_set_style_shadow_color(container, lv_color_hex(0x7294CB), 0); // Burlington blue shadow
    lv_obj_set_style_shadow_width(container, 15, 0);
    lv_obj_set_style_pad_all(container, 20, 0);

    // Sound option
    lv_obj_t* sound_btn = lv_btn_create(container);
    lv_obj_set_size(sound_btn, 260, 40);
    lv_obj_align(sound_btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(sound_btn, &style_btn, 0);
    lv_obj_add_style(sound_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* sound_label = lv_label_create(sound_btn);
    lv_label_set_text(sound_label, LV_SYMBOL_AUDIO " Sound");
    lv_obj_center(sound_label);
    lv_obj_add_event_cb(sound_btn, [](lv_event_t* e) {
        DEBUG_PRINT("Sound settings clicked");
        createSoundSettingsScreen(); // Navigate to sound settings
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* brightness_btn = lv_btn_create(container);
    lv_obj_set_size(brightness_btn, 260, 40);
    lv_obj_align(brightness_btn, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_add_style(brightness_btn, &style_btn, 0);
    lv_obj_add_style(brightness_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* brightness_label = lv_label_create(brightness_btn);
    lv_label_set_text(brightness_label, LV_SYMBOL_SETTINGS " Brightness");
    lv_obj_center(brightness_label);
    lv_obj_add_event_cb(brightness_btn, [](lv_event_t* e) {
        createBrightnessSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    // WiFi option
    lv_obj_t* wifi_btn = lv_btn_create(container);
    lv_obj_set_size(wifi_btn, 260, 40);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_add_style(wifi_btn, &style_btn, 0);
    lv_obj_add_style(wifi_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI " WiFi");
    lv_obj_center(wifi_label);
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t* e) {
        createWiFiManagerScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Back button
    lv_obj_t* back_btn = lv_btn_create(settingsScreen);
    lv_obj_set_size(back_btn, 120, 45);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createMainMenu();
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(settingsScreen);
}

void createSoundSettingsScreen() {
    static lv_obj_t* sound_settings_screen = nullptr;
    if (sound_settings_screen) {
        lv_obj_del(sound_settings_screen);
        sound_settings_screen = nullptr;
    }
    sound_settings_screen = lv_obj_create(NULL);
    if (!sound_settings_screen) {
        DEBUG_PRINT("Failed to create sound_settings_screen");
        return;
    }
    DEBUG_PRINTF("Created sound_settings_screen: %p\n", sound_settings_screen);
    
    lv_obj_add_style(sound_settings_screen, &style_screen, 0);
    lv_scr_load(sound_settings_screen);
    DEBUG_PRINT("Screen loaded");

    // Header with gradient
    lv_obj_t* header = lv_obj_create(sound_settings_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 60);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x4A90E2), 0);
    lv_obj_set_style_bg_grad_color(header, lv_color_hex(0x357ABD), 0);
    lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_VER, 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Sound Settings");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    addWifiIndicator(sound_settings_screen);
    addBatteryIndicator(sound_settings_screen);

    // Sound Toggle Switch
    lv_obj_t* sound_toggle_label = lv_label_create(sound_settings_screen);
    lv_label_set_text(sound_toggle_label, "Sound Enable");
    lv_obj_align(sound_toggle_label, LV_ALIGN_TOP_LEFT, 20, 70);
    lv_obj_add_style(sound_toggle_label, &style_text, 0);

    lv_obj_t* sound_switch = lv_switch_create(sound_settings_screen);
    lv_obj_align(sound_switch, LV_ALIGN_TOP_RIGHT, -20, 65);
    lv_obj_set_size(sound_switch, 50, 25);
    if (M5.Speaker.isEnabled()) {
        lv_obj_add_state(sound_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sound_switch, [](lv_event_t* e) {
        Preferences prefs;
        prefs.begin("settings", false);
        bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        uint8_t current_volume = prefs.getUChar("volume", 128); // Get saved volume
        M5.Speaker.setVolume(enabled ? current_volume : 0); // Restore volume or mute
        if (enabled) M5.Speaker.tone(440, 100); // Play 440Hz test tone
        prefs.putBool("sound_enabled", enabled); // Save state
        DEBUG_PRINTF("Sound %s\n", enabled ? "Enabled" : "Disabled");
        prefs.end();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Volume Slider
    lv_obj_t* volume_label = lv_label_create(sound_settings_screen);
    lv_label_set_text(volume_label, "Volume");
    lv_obj_align(volume_label, LV_ALIGN_TOP_LEFT, 20, 120);
    lv_obj_add_style(volume_label, &style_text, 0);

    lv_obj_t* volume_slider = lv_slider_create(sound_settings_screen);
    lv_obj_set_size(volume_slider, SCREEN_WIDTH - 60, 10);
    lv_obj_align(volume_slider, LV_ALIGN_TOP_MID, 0, 150);
    lv_slider_set_range(volume_slider, 0, 255);
    lv_slider_set_value(volume_slider, M5.Speaker.getVolume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(volume_slider, [](lv_event_t* e) {
        Preferences prefs;
        prefs.begin("settings", false);
        lv_obj_t* slider = lv_event_get_target(e);
        uint8_t volume = lv_slider_get_value(slider);
        M5.Speaker.setVolume(volume);
        if (M5.Speaker.isEnabled()) {
            M5.Speaker.tone(440, 100); // Play 440Hz test tone
        }
        prefs.putUChar("volume", volume); // Save volume
        DEBUG_PRINTF("Volume set to %d\n", volume);
        prefs.end();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Volume Value Display
    static lv_obj_t* volume_value_label = nullptr;
    if (volume_value_label) lv_obj_del(volume_value_label);
    volume_value_label = lv_label_create(sound_settings_screen);
    char volume_text[10];
    snprintf(volume_text, sizeof(volume_text), "%d", M5.Speaker.getVolume());
    lv_label_set_text(volume_value_label, volume_text);
    lv_obj_align_to(volume_value_label, volume_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_add_style(volume_value_label, &style_text, 0);

    lv_obj_add_event_cb(volume_slider, [](lv_event_t* e) {
        uint8_t volume = lv_slider_get_value(lv_event_get_target(e));
        char volume_text[10];
        snprintf(volume_text, sizeof(volume_text), "%d", volume);
        lv_label_set_text(volume_value_label, volume_text);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Back Button
    lv_obj_t* back_btn = lv_btn_create(sound_settings_screen);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_size(back_btn, 90, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(sound_settings_screen);
    DEBUG_PRINT("Sound settings screen created");
}

void createBrightnessSettingsScreen() {
    DEBUG_PRINT("Entering createBrightnessSettingsScreen");
    
    // Create a static variable for the brightness screen
    static lv_obj_t* brightness_settings_screen = nullptr;
    
    // Clean up previous instance if it exists
    if (brightness_settings_screen) {
        DEBUG_PRINTF("Cleaning and deleting existing brightness_settings_screen: %p\n", brightness_settings_screen);
        lv_obj_clean(brightness_settings_screen);
        lv_obj_del(brightness_settings_screen);
        brightness_settings_screen = nullptr;
        lv_task_handler();
        delay(10);
    }
    
    // Create a new screen
    brightness_settings_screen = lv_obj_create(NULL);
    if (!brightness_settings_screen) {
        DEBUG_PRINT("Failed to create brightness_settings_screen");
        return;
    }
    DEBUG_PRINTF("Created brightness_settings_screen: %p\n", brightness_settings_screen);
    
    lv_obj_add_style(brightness_settings_screen, &style_screen, 0);
    lv_scr_load(brightness_settings_screen);
    DEBUG_PRINT("Screen loaded");

    // Header with gradient
    lv_obj_t* header = lv_obj_create(brightness_settings_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 60);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x4A90E2), 0);
    lv_obj_set_style_bg_grad_color(header, lv_color_hex(0x357ABD), 0);
    lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_VER, 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Display Brightness");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Status indicators
    addWifiIndicator(brightness_settings_screen);
    addBatteryIndicator(brightness_settings_screen);
    
    // Container with subtle shadow
    lv_obj_t* container = lv_obj_create(brightness_settings_screen);
    lv_obj_set_size(container, 300, 200);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x2A2A40), 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_set_style_shadow_color(container, lv_color_hex(0x333333), 0);
    lv_obj_set_style_shadow_width(container, 15, 0);
    lv_obj_set_style_pad_all(container, 20, 0);
    
    // Brightness value label
    lv_obj_t* brightnessValueLabel = lv_label_create(container);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (displayBrightness * 100) / 255);
    lv_label_set_text(brightnessValueLabel, buf);
    lv_obj_align(brightnessValueLabel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(brightnessValueLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(brightnessValueLabel, lv_color_hex(0xFFFFFF), 0);

    // Brightness slider with custom colors
    lv_obj_t* brightnessSlider = lv_slider_create(container);
    lv_obj_set_width(brightnessSlider, 260);
    lv_obj_align(brightnessSlider, LV_ALIGN_TOP_MID, 0, 40);
    lv_slider_set_range(brightnessSlider, 10, 255);
    lv_slider_set_value(brightnessSlider, displayBrightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightnessSlider, lv_color_hex(0x4A90E2), LV_PART_KNOB);
    lv_obj_set_style_bg_color(brightnessSlider, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightnessSlider, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    
    // Preset buttons
    lv_obj_t* presetContainer = lv_obj_create(container);
    lv_obj_set_size(presetContainer, 260, 50);
    lv_obj_align(presetContainer, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_opa(presetContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(presetContainer, 0, 0);
    lv_obj_set_flex_flow(presetContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(presetContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    const char* presets[] = {"Low", "Medium", "High"};
    const uint8_t presetValues[] = {50, 150, 250};
    
    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn = lv_btn_create(presetContainer);
        lv_obj_set_size(btn, 80, 40);
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
        
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, presets[i]);
        lv_obj_center(label);
        
        lv_obj_set_user_data(btn, (void*)(uintptr_t)presetValues[i]);
        
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* btn = lv_event_get_target(e);
            uint8_t value = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
            lv_obj_t* presetContainer = lv_obj_get_parent(btn);
            lv_obj_t* container = lv_obj_get_parent(presetContainer);
            lv_obj_t* slider = lv_obj_get_child(container, 1);
            lv_obj_t* valueLabel = lv_obj_get_child(container, 0);
            
            displayBrightness = value;
            lv_slider_set_value(slider, value, LV_ANIM_ON);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", (value * 100) / 255);
            lv_label_set_text(valueLabel, buf);
            M5.Display.setBrightness(value);
            
            Preferences prefs;
            prefs.begin("settings", false);
            prefs.putUChar("brightness", value);
            prefs.end();
            
            DEBUG_PRINTF("Brightness preset set to %d\n", value);
        }, LV_EVENT_CLICKED, NULL);
    }
    
    // Slider event handler
    lv_obj_add_event_cb(brightnessSlider, [](lv_event_t* e) {
        lv_obj_t* slider = lv_event_get_target(e);
        displayBrightness = lv_slider_get_value(slider);
        lv_obj_t* container = lv_obj_get_parent(slider);
        lv_obj_t* valueLabel = lv_obj_get_child(container, 0);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (displayBrightness * 100) / 255);
        lv_label_set_text(valueLabel, buf);
        M5.Display.setBrightness(displayBrightness);
        
        Preferences prefs;
        prefs.begin("settings", false);
        prefs.putUChar("brightness", displayBrightness);
        prefs.end();
        
        DEBUG_PRINTF("Brightness set to %d\n", displayBrightness);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Back button
    lv_obj_t* back_btn = lv_btn_create(brightness_settings_screen);
    lv_obj_set_size(back_btn, 140, 50);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);
    
    // Auto-brightness option
    lv_obj_t* auto_container = lv_obj_create(container);
    lv_obj_set_size(auto_container, 260, 50);
    lv_obj_align(auto_container, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_bg_opa(auto_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(auto_container, 0, 0);
    
    lv_obj_t* auto_label = lv_label_create(auto_container);
    lv_label_set_text(auto_label, "Auto Brightness");
    lv_obj_align(auto_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(auto_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(auto_label, lv_color_hex(0xFFFFFF), 0);
    
    lv_obj_t* auto_switch = lv_switch_create(auto_container);
    lv_obj_align(auto_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(auto_switch, lv_color_hex(0x4A90E2), LV_PART_KNOB);
    
    Preferences prefs;
    prefs.begin("settings", false);
    bool auto_brightness = prefs.getBool("auto_bright", false);
    prefs.end();
    
    if (auto_brightness) {
        lv_obj_add_state(auto_switch, LV_STATE_CHECKED);
    }
    
    lv_obj_add_event_cb(auto_switch, [](lv_event_t* e) {
        lv_obj_t* sw = lv_event_get_target(e);
        bool auto_brightness = lv_obj_has_state(sw, LV_STATE_CHECKED);
        lv_obj_t* container = lv_obj_get_parent(lv_obj_get_parent(sw));
        lv_obj_t* slider = lv_obj_get_child(container, 1);
        lv_obj_t* presetContainer = lv_obj_get_child(container, 2);
        
        if (auto_brightness) {
            lv_obj_add_state(slider, LV_STATE_DISABLED);
            for (int i = 0; i < lv_obj_get_child_cnt(presetContainer); i++) {
                lv_obj_add_state(lv_obj_get_child(presetContainer, i), LV_STATE_DISABLED);
            }
        } else {
            lv_obj_clear_state(slider, LV_STATE_DISABLED);
            for (int i = 0; i < lv_obj_get_child_cnt(presetContainer); i++) {
                lv_obj_clear_state(lv_obj_get_child(presetContainer, i), LV_STATE_DISABLED);
            }
        }
        
        Preferences prefs;
        prefs.begin("settings", false);
        prefs.putBool("auto_bright", auto_brightness);
        prefs.end();
        
        DEBUG_PRINTF("Auto brightness set to %d\n", auto_brightness);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    DEBUG_PRINT("Finished createBrightnessSettingsScreen");
}