#include "WiFiManager.h"
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


// Add these constants near the top of your file with other defines
#define AXP2101_ADDR 0x34
#define AW9523_ADDR 0x58

// UI elements
lv_obj_t* battery_icon = nullptr;
lv_obj_t* battery_label = nullptr;
lv_obj_t* wifi_label = nullptr;
lv_obj_t* time_label = nullptr; // New time display label

// Styles for hybrid UI
static lv_style_t style_card_action;     // Interactive cards (New Entry, Logs)
static lv_style_t style_card_info;       // Info cards (Battery, WiFi)
static lv_style_t style_card_pressed;    // Pressed state for interactive cards

// Global WiFiManager instance
WiFiManager wifiManager;

// Global variables to store selected date/time temporarily
static lv_calendar_date_t selected_date = {2025, 3, 13}; // Default date
static int selected_hour = 0, selected_minute = 0, selected_is_pm = 0;

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
bool appendToLog(const String& entry); // Already correct in your code
void createViewLogsScreen();
void initFileSystem();
void listSavedEntries();
void saveEntry(const String& entry);
void updateStatus(const char* message, uint32_t color);
void sendWebhook(const String& entry);
void onWiFiScanComplete(const std::vector<NetworkInfo>& results);
void updateWiFiLoadingScreen(bool success, const String& message);
void showWiFiLoadingScreen(const String& ssid);
void addTimeDisplay(lv_obj_t *screen); // New function to add a time display
void updateTimeDisplay(); // Function to update the time display periodically

// Global variables for scrolling
lv_obj_t *current_scroll_obj = nullptr;
const int SCROLL_AMOUNT = 40;  // Pixels to scroll per button press

// Global variables for color selection
String selectedShirtColors = "";
String selectedPantsColors = "";
String selectedShoesColors = "";
String selectedGender = "";    // Added
String selectedItem = "";      // Added


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

lv_obj_t* status_bar = nullptr;
char current_status_msg[32] = "";
uint32_t current_status_color = 0xFFFFFF;

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

void setSystemTimeFromRTC() {
    m5::rtc_date_t DateStruct;
    m5::rtc_time_t TimeStruct;
    M5.Rtc.getDate(&DateStruct);
    M5.Rtc.getTime(&TimeStruct);

    struct tm timeinfo = {0};
    timeinfo.tm_year = DateStruct.year - 1900; // Years since 1900
    timeinfo.tm_mon = DateStruct.month - 1;    // Months 0-11
    timeinfo.tm_mday = DateStruct.date;
    timeinfo.tm_hour = TimeStruct.hours;
    timeinfo.tm_min = TimeStruct.minutes;
    timeinfo.tm_sec = TimeStruct.seconds;
    timeinfo.tm_isdst = -1; // Let system handle DST if applicable

    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    DEBUG_PRINT("System time set from RTC");

    // Optional: Log current time for verification
    struct tm timeinfo_check;
    if (getLocalTime(&timeinfo_check)) {
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%A, %B %d %Y %I:%M:%S %p", &timeinfo_check);
        DEBUG_PRINTF("Current local time: %s", timeStr);
    }
}

// Global vector for parsed log entries
static std::vector<LogEntry> parsedLogEntries;

lv_obj_t *item_list = nullptr;  // Add this with other global LVGL objects

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
lv_obj_t* view_logs_screen = nullptr;
lv_obj_t* settingsScreen = nullptr;

// Add these global variables with other UI elements
lv_obj_t* wifi_loading_screen = nullptr;
lv_obj_t* wifi_loading_spinner = nullptr;
lv_obj_t* wifi_loading_label = nullptr;
lv_obj_t* wifi_result_label = nullptr;

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

// Update parseTimestamp to handle the new format
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

// Example usage in logging
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

// Save entry to SD card and send to Zapier if connected
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

void createViewLogsScreen() {
    DEBUG_PRINT("Creating new view logs screen");

    parsedLogEntries.clear();

    lv_obj_t* logs_screen = lv_obj_create(NULL);
    lv_obj_add_style(logs_screen, &style_screen, 0);

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

// Function to release SPI bus
void releaseSPIBus() {
    SPI.end();
    delay(100);
}

// SPI-safe file system initialization
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

// Simplified setup function
void setup() {
    Serial.begin(115200);
    DEBUG_PRINT("Starting Loss Prevention Log...");

    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Power.begin();

    // Enable external bus power (unchanged)
    M5.Power.setExtOutput(true);
    Serial.println("External bus power enabled: " + String(M5.Power.getExtOutput() ? "Yes" : "No"));

    // Configure ALDO3 to 3.3V (unchanged)
    M5.Power.Axp2101.writeRegister8(0x94, 28); // Set ALDO3 to 3.3V
    uint8_t reg90 = M5.Power.Axp2101.readRegister8(0x90);
    M5.Power.Axp2101.writeRegister8(0x90, reg90 | 0x08); // Enable ALDO3
    Serial.printf("ALDO3 configured to 3.3V\n");

    // Check AW9523 PORT0/PORT1 (unchanged)
    uint8_t port0_val = M5.In_I2C.readRegister8(0x58, 0x02, 100000);
    Serial.printf("AW9523 PORT0: 0x%02X (BUS_EN: %d)\n", port0_val, (port0_val & 0x02) >> 1);
    uint8_t port1_val = M5.In_I2C.readRegister8(0x58, 0x03, 100000);
    Serial.printf("AW9523 PORT1: 0x%02X (BOOST_EN: %d)\n", port1_val, (port1_val & 0x80) >> 7);

    // Monitor battery and VBUS (unchanged)
    DEBUG_PRINTF("Battery Voltage: %d mV\n", M5.Power.getBatteryVoltage());
    DEBUG_PRINTF("VBUS Voltage: %d mV\n", M5.Power.getVBUSVoltage());

    // Initialize speaker (unchanged)
    M5.Speaker.begin();
    DEBUG_PRINT("Speaker initialized");

    // Load persistent sound settings (unchanged)
    Preferences prefs;
    prefs.begin("settings", false);
    uint8_t saved_volume = prefs.getUChar("volume", 128);
    bool sound_enabled = prefs.getBool("sound_enabled", true);
    M5.Speaker.setVolume(sound_enabled ? saved_volume : 0);
    DEBUG_PRINTF("Loaded sound settings - Enabled: %d, Volume: %d\n", sound_enabled, saved_volume);

    // Load persistent brightness settings (unchanged)
    uint8_t displayBrightness = prefs.getUChar("brightness", 128);
    M5.Display.setBrightness(displayBrightness);
    DEBUG_PRINTF("Loaded Brightness settings - Brightness: %d\n", displayBrightness);
    prefs.end();

    // Initialize LVGL (unchanged)
    DEBUG_PRINT("Before lv_init");
    lv_init();
    DEBUG_PRINT("After lv_init");

    // Initialize LVGL buffer (unchanged)
    static lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    static lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_WIDTH * 40);
    DEBUG_PRINT("Display buffer initialized");

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = M5.Display.width();
    disp_drv.ver_res = M5.Display.height();
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    DEBUG_PRINT("Display driver registered");

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
    DEBUG_PRINT("Input driver registered");

    // Create semaphore for LVGL thread safety (unchanged)
    xGuiSemaphore = xSemaphoreCreateMutex();

    // Set initial RTC time if unset
    m5::rtc_date_t DateStruct;
    M5.Rtc.getDate(&DateStruct);
    if (DateStruct.year < 2020) {
        // Set default date/time if RTC is unset
        DateStruct.year = 2025;
        DateStruct.month = 3;
        DateStruct.date = 13; // Today's date based on March 13, 2025
        DateStruct.weekDay = 3; // Thursday (simplified, not critical)
        M5.Rtc.setDate(&DateStruct);

        m5::rtc_time_t TimeStruct;
        TimeStruct.hours = 12;
        TimeStruct.minutes = 0;
        TimeStruct.seconds = 0;
        M5.Rtc.setTime(&TimeStruct);
        DEBUG_PRINT("RTC set to default: 2025-03-13 12:00:00");
    }
    setSystemTimeFromRTC();

    // Initialize WiFi Manager (unchanged)
    wifiManager.begin();
    wifiManager.setStatusCallback(onWiFiStatus);
    wifiManager.setScanCallback(onWiFiScanComplete);
    loadSavedNetworks();
    DEBUG_PRINT("WiFi Manager initialized");

    // Initialize file system after display is set up
    initFileSystem();

    initStyles();
    createMainMenu();
    DEBUG_PRINT("Setup complete!");
}

// Simplified loop function
void loop() {
    M5.update();
    uint32_t currentMillis = millis();
    lv_timer_handler();

    // Update time display every second
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 1000) {
        updateTimeDisplay();
        lastTimeUpdate = millis();
    }

    // Handle LVGL timing (unchanged)
    static uint32_t lastLvglTick = 0;
    if (currentMillis - lastLvglTick > 10) { // screenTickPeriod set to 10ms
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
            lastLvglTick = currentMillis;
        }
    }

    wifiManager.update(); // Process WiFi events (unchanged)

    // Battery and WiFi updates (unchanged)
    static unsigned long lastUpdate = 0;
    if (currentMillis - lastUpdate > 1000) {
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            if (lv_scr_act() == main_menu_screen) {
                int battery_level = M5.Power.getBatteryLevel();
                const char* battery_symbol = (battery_level > 75) ? LV_SYMBOL_BATTERY_FULL :
                                             (battery_level > 50) ? LV_SYMBOL_BATTERY_3 :
                                             (battery_level > 25) ? LV_SYMBOL_BATTERY_2 :
                                             (battery_level > 10) ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
                if (battery_icon) lv_label_set_text(battery_icon, battery_symbol);
                if (battery_label) {
                    char battery_text[5];
                    snprintf(battery_text, sizeof(battery_text), "%d%%", battery_level);
                    lv_label_set_text(battery_label, battery_text);
                }

                if (wifi_label) {
                    int rssi = WiFi.RSSI();
                    int wifi_strength = map(rssi, -100, -50, 0, 100);
                    wifi_strength = constrain(wifi_strength, 0, 100);
                    char wifi_text[5];
                    snprintf(wifi_text, sizeof(wifi_text), "%d%%", wifi_strength);
                    lv_label_set_text(wifi_label, wifi_text);
                }
            }
            xSemaphoreGive(xGuiSemaphore);
        }
        lastUpdate = currentMillis;
    }

    delay(5); // Small delay for stability (unchanged)
}

// Display flush function
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    M5.Display.startWrite();
    M5.Display.setAddrWindow(area->x1, area->y1, w, h);
    M5.Display.pushPixels((uint16_t *)color_p, w * h); // Replace pushColors with pushPixels
    M5.Display.endWrite();
    lv_disp_flush_ready(disp);
}

// Touchpad read function
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

// Swipe left function
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

// Swipe vertical function
void handleSwipeVertical(int amount) {
    if (current_scroll_obj && lv_obj_is_valid(current_scroll_obj)) {
        lv_obj_scroll_by(current_scroll_obj, 0, amount, LV_ANIM_ON);
        DEBUG_PRINTF("Scrolled %p by %d pixels\n", current_scroll_obj, amount);
        lv_obj_invalidate(current_scroll_obj); // Force UI refresh
    } else {
        DEBUG_PRINT("No valid scrollable object");
    }
}

// Add status bar function
void addStatusBar(lv_obj_t* screen) {
    if (status_bar) lv_obj_del(status_bar);
    status_bar = lv_label_create(screen);
    lv_obj_set_style_text_font(status_bar, &lv_font_montserrat_14, 0);
    lv_obj_align(status_bar, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_color(status_bar, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(status_bar, "Ready");
}

// Create main menu function
void createMainMenu() {
    DEBUG_PRINTF("Free heap before main menu: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    
    // Ensure SPI bus is set for display
    releaseSPIBus();
    SPI.begin();
    pinMode(TFT_DC, OUTPUT);
    digitalWrite(TFT_DC, HIGH);

    // Clean up existing main menu screen if it exists
    if (main_menu_screen) {
        lv_obj_clean(main_menu_screen); // Remove all child objects
        lv_obj_del(main_menu_screen);
        main_menu_screen = nullptr;
    }
    main_menu_screen = lv_obj_create(NULL);
    lv_obj_add_style(main_menu_screen, &style_screen, 0);

    // Reset global pointers to avoid dangling references
    current_scroll_obj = nullptr;
    status_bar = nullptr;
    battery_icon = nullptr;
    battery_label = nullptr;
    wifi_label = nullptr;
    time_label = nullptr;

    // Header
    lv_obj_t* header = lv_obj_create(main_menu_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 40);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Loss Prevention");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Scrollable card grid (2x3, vertical scrolling)
    lv_obj_t* grid = lv_obj_create(main_menu_screen);
    lv_obj_set_size(grid, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 50);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    static lv_coord_t col_dsc[] = {140, 140, LV_GRID_TEMPLATE_LAST}; // 2 columns
    static lv_coord_t row_dsc[] = {80, 80, 80, LV_GRID_TEMPLATE_LAST}; // 3 rows
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_content_width(grid, 300);
    // Scroll improvements
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);  // Vertical scrolling
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_snap_y(grid, LV_SCROLL_SNAP_NONE);  // Disable snap scrolling
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLL_MOMENTUM);  // Add momentum
    current_scroll_obj = grid;

    // Card 1: New Entry (Row 0, Col 0)
    lv_obj_t* new_card = lv_obj_create(grid);
    lv_obj_set_grid_cell(new_card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_style(new_card, &style_card_action, 0);
    lv_obj_add_style(new_card, &style_card_pressed, LV_STATE_PRESSED);
    lv_obj_t* new_icon = lv_label_create(new_card);
    lv_label_set_text(new_icon, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(new_icon, &lv_font_montserrat_20, 0);
    lv_obj_align(new_icon, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t* new_label = lv_label_create(new_card);
    lv_label_set_text(new_label, "New Entry");
    lv_obj_align(new_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(new_card, [](lv_event_t* e) {
        createGenderMenu(); 
    }, LV_EVENT_CLICKED, NULL);

    // Card 2: Logs (Row 0, Col 1)
    lv_obj_t* logs_card = lv_obj_create(grid);
    lv_obj_set_grid_cell(logs_card, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_style(logs_card, &style_card_action, 0);
    lv_obj_add_style(logs_card, &style_card_pressed, LV_STATE_PRESSED);
    lv_obj_t* logs_icon = lv_label_create(logs_card);
    lv_label_set_text(logs_icon, LV_SYMBOL_LIST); // List icon
    lv_obj_set_style_text_font(logs_icon, &lv_font_montserrat_20, 0);
    lv_obj_align(logs_icon, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t* logs_label = lv_label_create(logs_card);
    lv_label_set_text(logs_label, "Logs");
    lv_obj_align(logs_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(logs_card, [](lv_event_t* e) {
        createViewLogsScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Card 3: Settings (Row 1, Col 0)
    lv_obj_t* settings_card = lv_obj_create(grid);
    lv_obj_set_grid_cell(settings_card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_add_style(settings_card, &style_card_action, 0);
    lv_obj_add_style(settings_card, &style_card_pressed, LV_STATE_PRESSED);
    lv_obj_t* settings_icon = lv_label_create(settings_card);
    lv_label_set_text(settings_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(settings_icon, &lv_font_montserrat_20, 0);
    lv_obj_align(settings_icon, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t* settings_label = lv_label_create(settings_card);
    lv_label_set_text(settings_label, "Settings");
    lv_obj_align(settings_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(settings_card, [](lv_event_t* e) {
        createSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Card 4: Battery (Row 1, Col 1)
    lv_obj_t* battery_card = lv_obj_create(grid);
    lv_obj_set_grid_cell(battery_card, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_add_style(battery_card, &style_card_info, 0);
    battery_icon = lv_label_create(battery_card);
    int battery_level = M5.Power.getBatteryLevel();
    const char* battery_symbol = (battery_level > 75) ? LV_SYMBOL_BATTERY_FULL :
                                 (battery_level > 50) ? LV_SYMBOL_BATTERY_3 :
                                 (battery_level > 25) ? LV_SYMBOL_BATTERY_2 :
                                 (battery_level > 10) ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text(battery_icon, battery_symbol);
    lv_obj_set_style_text_font(battery_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(battery_icon, lv_color_hex(0x4A90E2), 0);
    lv_obj_align(battery_icon, LV_ALIGN_CENTER, -20, 0);
    battery_label = lv_label_create(battery_card);
    char battery_text[5];
    snprintf(battery_text, sizeof(battery_text), "%d%%", battery_level);
    lv_label_set_text(battery_label, battery_text);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_16, 0);
    lv_obj_align(battery_label, LV_ALIGN_CENTER, 20, 0);

    // Card 5: WiFi (Row 2, Col 0)
    lv_obj_t* wifi_card = lv_obj_create(grid);
    lv_obj_set_grid_cell(wifi_card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_add_style(wifi_card, &style_card_info, 0);
    lv_obj_t* wifi_icon = lv_label_create(wifi_card);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x4A90E2), 0);
    lv_obj_align(wifi_icon, LV_ALIGN_CENTER, -20, 0);
    wifi_label = lv_label_create(wifi_card);
    int rssi = WiFi.RSSI();
    int wifi_strength = map(rssi, -100, -50, 0, 100);
    wifi_strength = constrain(wifi_strength, 0, 100);
    char wifi_text[5];
    snprintf(wifi_text, sizeof(wifi_text), "%d%%", wifi_strength);
    lv_label_set_text(wifi_label, wifi_text);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_16, 0);
    lv_obj_align(wifi_label, LV_ALIGN_CENTER, 20, 0);

    // Card 6: Power Management (Row 2, Col 1)
    lv_obj_t* power_card = lv_obj_create(grid);
    lv_obj_set_grid_cell(power_card, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_add_style(power_card, &style_card_action, 0);
    lv_obj_add_style(power_card, &style_card_pressed, LV_STATE_PRESSED);
    lv_obj_t* power_icon = lv_label_create(power_card);
    lv_label_set_text(power_icon, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(power_icon, &lv_font_montserrat_20, 0);
    lv_obj_align(power_icon, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t* power_label = lv_label_create(power_card);
    lv_label_set_text(power_label, "Power");
    lv_obj_align(power_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(power_card, [](lv_event_t* e) {
        createPowerManagementScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Add status bar before loading the screen
    addStatusBar(main_menu_screen);
    updateStatus("Ready", 0xFFFFFF); // Set initial status

    // Load the screen and add time display
    lv_scr_load(main_menu_screen);
    addTimeDisplay(main_menu_screen);

    DEBUG_PRINTF("Free heap after main menu: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

// Update status function
void updateStatus(const char* message, uint32_t color) {
    if (!status_bar) return; // Ensure status bar exists

    // Get current time from RTC
    m5::rtc_time_t TimeStruct;
    M5.Rtc.getTime(&TimeStruct);

    // Format time in 12-hour format with AM/PM
    char time_str[12]; // Enough for "HH:MM:SS AM/PM" (e.g., "2:30 PM")
    struct tm timeinfo;
    timeinfo.tm_hour = TimeStruct.hours;
    timeinfo.tm_min = TimeStruct.minutes;
    timeinfo.tm_sec = TimeStruct.seconds;
    strftime(time_str, sizeof(time_str), "%I:%M %p", &timeinfo);

    // Remove leading zero if present (e.g., "02:30 PM" -> "2:30 PM")
    if (time_str[0] == '0') {
        memmove(time_str, time_str + 1, strlen(time_str));
    }

    // Update time label
    if (!time_label) {
        time_label = lv_label_create(status_bar);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, -10, 0);
    }
    lv_label_set_text(time_label, time_str);

    // Update status message if needed (assuming status_label exists)
    if (status_bar && current_status_msg[0] != '\0') {
        lv_label_set_text(status_bar, current_status_msg);
        lv_obj_set_style_text_color(status_bar, lv_color_hex(current_status_color), 0);
    }
}

// Create gender menu function
void createGenderMenu() {
    // Create the screen
    lv_obj_t* gender_screen = lv_obj_create(NULL);
    lv_obj_add_style(gender_screen, &style_screen, 0); // Assuming this sets basic screen style
    lv_obj_set_style_bg_color(gender_screen, lv_color_hex(0x1A1A1A), 0); // Dark gray background
    lv_obj_set_style_bg_opa(gender_screen, LV_OPA_COVER, 0);
    lv_obj_add_flag(gender_screen, LV_OBJ_FLAG_SCROLLABLE); // Screen scrolls if needed

    // Back Button (Top-Left)
    lv_obj_t* back_btn = lv_btn_create(gender_screen);
    lv_obj_set_size(back_btn, 60, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0); // Darker gray button
    lv_obj_set_style_radius(back_btn, 5, 0);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT); // Left arrow icon
    lv_obj_center(back_label);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0); // White text
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createMainMenu();
    }, LV_EVENT_CLICKED, NULL);

    // Title
    lv_obj_t* title = lv_label_create(gender_screen);
    lv_label_set_text(title, "Select Gender");
    lv_obj_add_style(title, &style_title, 0); // Assuming this sets font/size
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0); // White text
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Define styles for gender cards
    static lv_style_t style_card;
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0x2D2D2D)); // Dark gray for cards
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_border_color(&style_card, lv_color_hex(0x505050)); // Light gray border
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_radius(&style_card, 5);
    lv_style_set_shadow_color(&style_card, lv_color_hex(0x000000)); // Black shadow
    lv_style_set_shadow_width(&style_card, 10);
    lv_style_set_shadow_spread(&style_card, 2);
    lv_style_set_pad_all(&style_card, 5);

    static lv_style_t style_card_pressed;
    lv_style_init(&style_card_pressed);
    lv_style_set_bg_color(&style_card_pressed, lv_color_hex(0x404040)); // Lighter gray when pressed
    lv_style_set_bg_opa(&style_card_pressed, LV_OPA_COVER);

    // Gender Options
    const char* genders[] = {"Male", "Female"};
    int y_offset = 80; // Start below title
    for (int i = 0; i < 2; i++) { // Only Male and Female from your original
        lv_obj_t* card = lv_obj_create(gender_screen);
        lv_obj_set_size(card, SCREEN_WIDTH - 40, 60);
        lv_obj_set_pos(card, 20, y_offset);
        lv_obj_add_style(card, &style_card, 0);
        lv_obj_add_style(card, &style_card_pressed, LV_STATE_PRESSED);

        lv_obj_t* label = lv_label_create(card);
        lv_label_set_text(label, genders[i]);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

        // Event callback based on gender
        if (strcmp(genders[i], "Male") == 0) {
            lv_obj_add_event_cb(card, [](lv_event_t* e) {
                DEBUG_PRINT("Male selected");
                currentEntry = "Male,";
                DEBUG_PRINTF("Current entry: %s\n", currentEntry.c_str());
                createColorMenuShirt();
            }, LV_EVENT_CLICKED, NULL);
        } else if (strcmp(genders[i], "Female") == 0) {
            lv_obj_add_event_cb(card, [](lv_event_t* e) {
                DEBUG_PRINT("Female selected");
                currentEntry = "Female,";
                DEBUG_PRINTF("Current entry: %s\n", currentEntry.c_str());
                createColorMenuShirt();
            }, LV_EVENT_CLICKED, NULL);
        }

        y_offset += 70; // Spacing between cards
    }

    lv_scr_load(gender_screen);
}

// Create color menu function
void createColorMenuShirt() {
    DEBUG_PRINT("Creating Shirt Color Menu");

    if (colorMenu) {
        DEBUG_PRINTF("Cleaning existing color menu: 0x%08x\n", (unsigned)colorMenu);
        lv_obj_del_async(colorMenu);
        colorMenu = nullptr;
    }
    shirt_next_btn = nullptr;
    pants_next_btn = nullptr;
    shoes_next_btn = nullptr;
    current_scroll_obj = nullptr;

    DEBUG_PRINTF("Free heap before creation: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    colorMenu = lv_obj_create(NULL);
    if (!colorMenu) {
        DEBUG_PRINT("Failed to create colorMenu");
        return;
    }
    DEBUG_PRINTF("New color menu created: 0x%08x\n", (unsigned)colorMenu);
    lv_obj_add_style(colorMenu, &style_screen, 0);
    lv_obj_set_style_bg_color(colorMenu, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_grad_color(colorMenu, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_grad_dir(colorMenu, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(colorMenu, LV_OPA_COVER, 0);

    // Header
    lv_obj_t* header = lv_obj_create(colorMenu);
    lv_obj_set_size(header, SCREEN_WIDTH - 20, 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 10, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Shirt Color");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(title);

    // Grid of color buttons
    lv_obj_t* grid = lv_obj_create(colorMenu);
    lv_obj_set_size(grid, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 100);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    static lv_coord_t col_dsc[] = {140, 140, LV_GRID_TEMPLATE_LAST}; // 2 columns
    static lv_coord_t row_dsc[] = {60, 60, 60, 60, LV_GRID_TEMPLATE_LAST}; // 4 rows
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_all(grid, 5, 0);

    // Color mappings
    struct ColorInfo {
        const char* name;
        uint32_t color;
    };
    ColorInfo colorMap[] = {
        {"Red", 0xFF0000}, {"Orange", 0xFFA500}, {"Yellow", 0xFFFF00},
        {"Green", 0x00FF00}, {"Blue", 0x0000FF}, {"Purple", 0x800080},
        {"Black", 0x000000}, {"White", 0xFFFFFF}
    };
    const int numColors = sizeof(colorMap) / sizeof(colorMap[0]);
    DEBUG_PRINTF("Creating %d shirt color buttons\n", numColors);

    // Array to track selected buttons
    lv_obj_t* color_buttons[numColors] = {nullptr};
    selectedShirtColors = ""; // Reset selection

    for (int i = 0; i < numColors; i++) {
        lv_obj_t* btn = lv_btn_create(grid);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
        lv_obj_set_style_bg_color(btn, lv_color_hex(colorMap[i].color), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0); // Faded by default
        lv_obj_set_style_radius(btn, 15, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 5, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, colorMap[i].name);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);

        color_buttons[i] = btn;

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* btn = lv_event_get_target(e);
            lv_obj_t* label = lv_obj_get_child(btn, 0);
            if (label) {
                String color = lv_label_get_text(label);
                bool is_selected = lv_obj_get_style_border_width(btn, 0) > 0;
                if (is_selected) {
                    // Deselect
                    lv_obj_set_size(btn, 140, 60); // Reset size
                    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
                    lv_obj_set_style_border_width(btn, 0, 0);
                    lv_obj_set_style_shadow_width(btn, 5, 0);
                    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
                    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
                    if (selectedShirtColors.indexOf(color) != -1) {
                        selectedShirtColors.replace(color + "+", "");
                        selectedShirtColors.replace("+" + color, "");
                        selectedShirtColors.replace(color, "");
                    }
                } else {
                    // Select
                    lv_obj_set_size(btn, 150, 65); // Slightly larger
                    lv_obj_set_style_bg_opa(btn, LV_OPA_100, 0);
                    lv_obj_set_style_border_width(btn, 4, 0);
                    lv_obj_set_style_border_color(btn, lv_color_hex(0x00FFFF), 0); // Cyan outline
                    lv_obj_set_style_shadow_width(btn, 12, 0);
                    lv_obj_set_style_shadow_opa(btn, LV_OPA_60, 0);
                    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x00FFFF), 0); // Cyan glow
                    if (!selectedShirtColors.isEmpty()) selectedShirtColors += "+";
                    selectedShirtColors += color;
                }
                DEBUG_PRINTF("Shirt colors updated: %s\n", selectedShirtColors.c_str());
            }
        }, LV_EVENT_CLICKED, NULL);

        DEBUG_PRINTF("Created color button %d: %s\n", i, colorMap[i].name);
    }

    // Back Button
    lv_obj_t* back_btn = lv_btn_create(colorMenu);
    lv_obj_set_size(back_btn, 120, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x6A6A6A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 20, 0);
    lv_obj_set_style_shadow_width(back_btn, 5, 0);
    lv_obj_set_style_shadow_color(back_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(back_btn, LV_OPA_20, 0);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back " LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            lv_obj_t* old_menu = colorMenu;
            colorMenu = nullptr;
            selectedShirtColors = "";
            createGenderMenu();
            if (old_menu && old_menu != lv_scr_act()) {
                lv_obj_del_async(old_menu);
            }
            xSemaphoreGive(xGuiSemaphore);
        }
    }, LV_EVENT_CLICKED, NULL);
    DEBUG_PRINT("Back button created");

    // Next Button
    shirt_next_btn = lv_btn_create(colorMenu);
    lv_obj_set_size(shirt_next_btn, 120, 40);
    lv_obj_align(shirt_next_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(shirt_next_btn, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_bg_opa(shirt_next_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(shirt_next_btn, lv_color_hex(0x6A6A6A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(shirt_next_btn, 20, 0);
    lv_obj_set_style_shadow_width(shirt_next_btn, 5, 0);
    lv_obj_set_style_shadow_color(shirt_next_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(shirt_next_btn, LV_OPA_20, 0);
    lv_obj_t* next_label = lv_label_create(shirt_next_btn);
    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_16, 0);
    lv_obj_center(next_label);
    lv_obj_add_event_cb(shirt_next_btn, [](lv_event_t* e) {
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            if (selectedShirtColors.isEmpty()) {
                lv_obj_t* header = lv_obj_get_child(lv_scr_act(), 0);
                lv_obj_set_style_bg_color(header, lv_color_hex(0xFF0000), 0);
                lv_timer_create([](lv_timer_t* timer) {
                    lv_obj_t* header = (lv_obj_t*)timer->user_data;
                    lv_obj_set_style_bg_color(header, lv_color_hex(0x3A3A3A), 0);
                    lv_timer_del(timer);
                }, 200, header);
            } else {
                lv_obj_t* old_menu = colorMenu;
                colorMenu = nullptr;
                currentEntry += selectedShirtColors + ",";
                DEBUG_PRINTF("Transitioning to pants menu with Shirt: %s\n", selectedShirtColors.c_str());
                createColorMenuPants();
                if (old_menu && old_menu != lv_scr_act()) {
                    lv_obj_del_async(old_menu);
                }
            }
            xSemaphoreGive(xGuiSemaphore);
        }
    }, LV_EVENT_CLICKED, NULL);
    DEBUG_PRINT("Next button created");

    lv_scr_load(colorMenu);
    DEBUG_PRINT("Shirt color menu loaded");
}

// Create color menu function
void createColorMenuPants() {
    DEBUG_PRINT("Creating Pants Color Menu");

    if (colorMenu) {
        DEBUG_PRINTF("Cleaning existing color menu: 0x%08x\n", (unsigned)colorMenu);
        lv_obj_del_async(colorMenu);
        colorMenu = nullptr;
    }
    shirt_next_btn = nullptr;
    pants_next_btn = nullptr;
    shoes_next_btn = nullptr;
    current_scroll_obj = nullptr;

    DEBUG_PRINTF("Free heap before creation: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    colorMenu = lv_obj_create(NULL);
    if (!colorMenu) {
        DEBUG_PRINT("Failed to create colorMenu");
        return;
    }
    DEBUG_PRINTF("New color menu created: 0x%08x\n", (unsigned)colorMenu);
    lv_obj_add_style(colorMenu, &style_screen, 0);
    lv_obj_set_style_bg_color(colorMenu, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_grad_color(colorMenu, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_grad_dir(colorMenu, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(colorMenu, LV_OPA_COVER, 0);

    // Header
    lv_obj_t* header = lv_obj_create(colorMenu);
    lv_obj_set_size(header, SCREEN_WIDTH - 20, 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 10, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Pants Color");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(title);

    // Grid of color buttons
    lv_obj_t* grid = lv_obj_create(colorMenu);
    lv_obj_set_size(grid, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 100);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    static lv_coord_t col_dsc[] = {140, 140, LV_GRID_TEMPLATE_LAST}; // 2 columns
    static lv_coord_t row_dsc[] = {60, 60, 60, 60, LV_GRID_TEMPLATE_LAST}; // 4 rows
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_all(grid, 5, 0);

    // Color mappings
    struct ColorInfo {
        const char* name;
        uint32_t color;
    };
    ColorInfo colorMap[] = {
        {"Red", 0xFF0000}, {"Orange", 0xFFA500}, {"Yellow", 0xFFFF00},
        {"Green", 0x00FF00}, {"Blue", 0x0000FF}, {"Purple", 0x800080},
        {"Black", 0x000000}, {"White", 0xFFFFFF}
    };
    const int numColors = sizeof(colorMap) / sizeof(colorMap[0]);
    DEBUG_PRINTF("Creating %d pants color buttons\n", numColors);

    // Array to track selected buttons
    lv_obj_t* color_buttons[numColors] = {nullptr};
    selectedPantsColors = ""; // Reset selection

    for (int i = 0; i < numColors; i++) {
        lv_obj_t* btn = lv_btn_create(grid);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
        lv_obj_set_style_bg_color(btn, lv_color_hex(colorMap[i].color), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0); // Faded by default
        lv_obj_set_style_radius(btn, 15, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 5, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, colorMap[i].name);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);

        color_buttons[i] = btn;

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* btn = lv_event_get_target(e);
            lv_obj_t* label = lv_obj_get_child(btn, 0);
            if (label) {
                String color = lv_label_get_text(label);
                bool is_selected = lv_obj_get_style_border_width(btn, 0) > 0;
                if (is_selected) {
                    // Deselect
                    lv_obj_set_size(btn, 140, 60); // Reset size
                    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
                    lv_obj_set_style_border_width(btn, 0, 0);
                    lv_obj_set_style_shadow_width(btn, 5, 0);
                    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
                    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
                    if (selectedPantsColors.indexOf(color) != -1) {
                        selectedPantsColors.replace(color + "+", "");
                        selectedPantsColors.replace("+" + color, "");
                        selectedPantsColors.replace(color, "");
                    }
                } else {
                    // Select
                    lv_obj_set_size(btn, 150, 65); // Slightly larger
                    lv_obj_set_style_bg_opa(btn, LV_OPA_100, 0);
                    lv_obj_set_style_border_width(btn, 4, 0);
                    lv_obj_set_style_border_color(btn, lv_color_hex(0x00FFFF), 0); // Cyan outline
                    lv_obj_set_style_shadow_width(btn, 12, 0);
                    lv_obj_set_style_shadow_opa(btn, LV_OPA_60, 0);
                    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x00FFFF), 0); // Cyan glow
                    if (!selectedPantsColors.isEmpty()) selectedPantsColors += "+";
                    selectedPantsColors += color;
                }
                DEBUG_PRINTF("Pants colors updated: %s\n", selectedPantsColors.c_str());
            }
        }, LV_EVENT_CLICKED, NULL);

        DEBUG_PRINTF("Created color button %d: %s\n", i, colorMap[i].name);
    }

    // Back Button
    lv_obj_t* back_btn = lv_btn_create(colorMenu);
    lv_obj_set_size(back_btn, 120, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x6A6A6A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 20, 0);
    lv_obj_set_style_shadow_width(back_btn, 5, 0);
    lv_obj_set_style_shadow_color(back_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(back_btn, LV_OPA_20, 0);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back " LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            lv_obj_t* old_menu = colorMenu;
            colorMenu = nullptr;
            selectedPantsColors = "";
            createColorMenuShirt();
            if (old_menu && old_menu != lv_scr_act()) {
                lv_obj_del_async(old_menu);
            }
            xSemaphoreGive(xGuiSemaphore);
        }
    }, LV_EVENT_CLICKED, NULL);
    DEBUG_PRINT("Back button created");

    // Next Button
    pants_next_btn = lv_btn_create(colorMenu);
    lv_obj_set_size(pants_next_btn, 120, 40);
    lv_obj_align(pants_next_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(pants_next_btn, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_bg_opa(pants_next_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pants_next_btn, lv_color_hex(0x6A6A6A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(pants_next_btn, 20, 0);
    lv_obj_set_style_shadow_width(pants_next_btn, 5, 0);
    lv_obj_set_style_shadow_color(pants_next_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(pants_next_btn, LV_OPA_20, 0);
    lv_obj_t* next_label = lv_label_create(pants_next_btn);
    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_16, 0);
    lv_obj_center(next_label);
    lv_obj_add_event_cb(pants_next_btn, [](lv_event_t* e) {
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            if (selectedPantsColors.isEmpty()) {
                lv_obj_t* header = lv_obj_get_child(lv_scr_act(), 0);
                lv_obj_set_style_bg_color(header, lv_color_hex(0xFF0000), 0);
                lv_timer_create([](lv_timer_t* timer) {
                    lv_obj_t* header = (lv_obj_t*)timer->user_data;
                    lv_obj_set_style_bg_color(header, lv_color_hex(0x3A3A3A), 0);
                    lv_timer_del(timer);
                }, 200, header);
            } else {
                lv_obj_t* old_menu = colorMenu;
                colorMenu = nullptr;
                currentEntry += selectedPantsColors + ",";
                DEBUG_PRINTF("Transitioning to shoes menu with Pants: %s\n", selectedPantsColors.c_str());
                createColorMenuShoes();
                if (old_menu && old_menu != lv_scr_act()) {
                    lv_obj_del_async(old_menu);
                }
            }
            xSemaphoreGive(xGuiSemaphore);
        }
    }, LV_EVENT_CLICKED, NULL);
    DEBUG_PRINT("Next button created");

    lv_scr_load(colorMenu);
    DEBUG_PRINT("Pants color menu loaded");
}

void createColorMenuShoes() {
    DEBUG_PRINT("Creating Shoes Color Menu");

    if (colorMenu) {
        DEBUG_PRINTF("Cleaning existing color menu: 0x%08x\n", (unsigned)colorMenu);
        lv_obj_del_async(colorMenu);
        colorMenu = nullptr;
    }
    shirt_next_btn = nullptr;
    pants_next_btn = nullptr;
    shoes_next_btn = nullptr;
    current_scroll_obj = nullptr;

    DEBUG_PRINTF("Free heap before creation: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    colorMenu = lv_obj_create(NULL);
    if (!colorMenu) {
        DEBUG_PRINT("Failed to create colorMenu");
        return;
    }
    DEBUG_PRINTF("New color menu created: 0x%08x\n", (unsigned)colorMenu);
    lv_obj_add_style(colorMenu, &style_screen, 0);
    lv_obj_set_style_bg_color(colorMenu, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_grad_color(colorMenu, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_grad_dir(colorMenu, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(colorMenu, LV_OPA_COVER, 0);

    // Header
    lv_obj_t* header = lv_obj_create(colorMenu);
    lv_obj_set_size(header, SCREEN_WIDTH - 20, 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 10, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Shoes Color");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(title);

    // Grid of color buttons
    lv_obj_t* grid = lv_obj_create(colorMenu);
    lv_obj_set_size(grid, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 100);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    static lv_coord_t col_dsc[] = {140, 140, LV_GRID_TEMPLATE_LAST}; // 2 columns
    static lv_coord_t row_dsc[] = {60, 60, 60, 60, LV_GRID_TEMPLATE_LAST}; // 4 rows
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_all(grid, 5, 0);

    // Color mappings
    struct ColorInfo {
        const char* name;
        uint32_t color;
    };
    ColorInfo colorMap[] = {
        {"Red", 0xFF0000}, {"Orange", 0xFFA500}, {"Yellow", 0xFFFF00},
        {"Green", 0x00FF00}, {"Blue", 0x0000FF}, {"Purple", 0x800080},
        {"Black", 0x000000}, {"White", 0xFFFFFF}
    };
    const int numColors = sizeof(colorMap) / sizeof(colorMap[0]);
    DEBUG_PRINTF("Creating %d shoes color buttons\n", numColors);

    // Array to track selected buttons
    lv_obj_t* color_buttons[numColors] = {nullptr};
    selectedShoesColors = ""; // Reset selection

    for (int i = 0; i < numColors; i++) {
        lv_obj_t* btn = lv_btn_create(grid);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
        lv_obj_set_style_bg_color(btn, lv_color_hex(colorMap[i].color), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0); // Faded by default
        lv_obj_set_style_radius(btn, 15, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 5, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, colorMap[i].name);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);

        color_buttons[i] = btn;

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* btn = lv_event_get_target(e);
            lv_obj_t* label = lv_obj_get_child(btn, 0);
            if (label) {
                String color = lv_label_get_text(label);
                bool is_selected = lv_obj_get_style_border_width(btn, 0) > 0;
                if (is_selected) {
                    // Deselect
                    lv_obj_set_size(btn, 140, 60); // Reset size
                    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
                    lv_obj_set_style_border_width(btn, 0, 0);
                    lv_obj_set_style_shadow_width(btn, 5, 0);
                    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
                    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
                    if (selectedShoesColors.indexOf(color) != -1) {
                        selectedShoesColors.replace(color + "+", "");
                        selectedShoesColors.replace("+" + color, "");
                        selectedShoesColors.replace(color, "");
                    }
                } else {
                    // Select
                    lv_obj_set_size(btn, 150, 65); // Slightly larger
                    lv_obj_set_style_bg_opa(btn, LV_OPA_100, 0);
                    lv_obj_set_style_border_width(btn, 4, 0);
                    lv_obj_set_style_border_color(btn, lv_color_hex(0x00FFFF), 0); // Cyan outline
                    lv_obj_set_style_shadow_width(btn, 12, 0);
                    lv_obj_set_style_shadow_opa(btn, LV_OPA_60, 0);
                    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x00FFFF), 0); // Cyan glow
                    if (!selectedShoesColors.isEmpty()) selectedShoesColors += "+";
                    selectedShoesColors += color;
                }
                DEBUG_PRINTF("Shoes colors updated: %s\n", selectedShoesColors.c_str());
            }
        }, LV_EVENT_CLICKED, NULL);

        DEBUG_PRINTF("Created color button %d: %s\n", i, colorMap[i].name);
    }

    // Back Button
    lv_obj_t* back_btn = lv_btn_create(colorMenu);
    lv_obj_set_size(back_btn, 120, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x6A6A6A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 20, 0);
    lv_obj_set_style_shadow_width(back_btn, 5, 0);
    lv_obj_set_style_shadow_color(back_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(back_btn, LV_OPA_20, 0);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back " LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            lv_obj_t* old_menu = colorMenu;
            colorMenu = nullptr;
            selectedShoesColors = "";
            createColorMenuPants();
            if (old_menu && old_menu != lv_scr_act()) {
                lv_obj_del_async(old_menu);
            }
            xSemaphoreGive(xGuiSemaphore);
        }
    }, LV_EVENT_CLICKED, NULL);
    DEBUG_PRINT("Back button created");

    // Next Button
    shoes_next_btn = lv_btn_create(colorMenu);
    lv_obj_set_size(shoes_next_btn, 120, 40);
    lv_obj_align(shoes_next_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(shoes_next_btn, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_bg_opa(shoes_next_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(shoes_next_btn, lv_color_hex(0x6A6A6A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(shoes_next_btn, 20, 0);
    lv_obj_set_style_shadow_width(shoes_next_btn, 5, 0);
    lv_obj_set_style_shadow_color(shoes_next_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(shoes_next_btn, LV_OPA_20, 0);
    lv_obj_t* next_label = lv_label_create(shoes_next_btn);
    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_16, 0);
    lv_obj_center(next_label);
    lv_obj_add_event_cb(shoes_next_btn, [](lv_event_t* e) {
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            if (selectedShoesColors.isEmpty()) {
                lv_obj_t* header = lv_obj_get_child(lv_scr_act(), 0);
                lv_obj_set_style_bg_color(header, lv_color_hex(0xFF0000), 0);
                lv_timer_create([](lv_timer_t* timer) {
                    lv_obj_t* header = (lv_obj_t*)timer->user_data;
                    lv_obj_set_style_bg_color(header, lv_color_hex(0x3A3A3A), 0);
                    lv_timer_del(timer);
                }, 200, header);
            } else {
                lv_obj_t* old_menu = colorMenu;
                colorMenu = nullptr;
                currentEntry += selectedShoesColors + ",";
                DEBUG_PRINTF("Transitioning to next menu with Shoes: %s\n", selectedShoesColors.c_str());
                // Replace with your next menu function, e.g., createSummaryMenu()
                createItemMenu(); // Placeholder - adjust as needed
                if (old_menu && old_menu != lv_scr_act()) {
                    lv_obj_del_async(old_menu);
                }
            }
            xSemaphoreGive(xGuiSemaphore);
        }
    }, LV_EVENT_CLICKED, NULL);
    DEBUG_PRINT("Next button created");

    lv_scr_load(colorMenu);
    DEBUG_PRINT("Shoes color menu loaded");
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
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            currentEntry += String(lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0)));
            delay(100);
            createConfirmScreen();
        }, LV_EVENT_CLICKED, NULL);
    }
}

void createConfirmScreen() {
    DEBUG_PRINT("Creating confirmation screen");
    
    // Clean up any existing confirm screen first
    if (confirmScreen) {
        DEBUG_PRINT("Cleaning existing confirm screen");
        lv_obj_del(confirmScreen);
        confirmScreen = nullptr;
        lv_timer_handler(); // Process deletion immediately
    }
    
    // Create new confirmation screen
    confirmScreen = lv_obj_create(NULL);
    if (!confirmScreen) {
        DEBUG_PRINT("Failed to create confirmation screen");
        createMainMenu(); // Fallback to main menu
        return;
    }
    
    lv_obj_add_style(confirmScreen, &style_screen, 0);
    lv_scr_load(confirmScreen);
    DEBUG_PRINTF("Confirm screen created: %p\n", confirmScreen);
    
    lv_timer_handler(); // Process any pending UI updates

    lv_obj_t *header = lv_obj_create(confirmScreen);
    lv_obj_set_size(header, 320, 50);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Confirm Entry");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Create a local copy of the formatted entry to avoid memory issues
    String formattedEntryStr = getFormattedEntry(currentEntry);
    
    lv_obj_t *preview = lv_label_create(confirmScreen);
    lv_label_set_text(preview, formattedEntryStr.c_str());
    lv_obj_add_style(preview, &style_text, 0);
    lv_obj_set_size(preview, 280, 140);
    lv_obj_align(preview, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(preview, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_pad_all(preview, 10, 0);

    lv_obj_t *btn_container = lv_obj_create(confirmScreen);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, 320, 50);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Create a local copy of the current entry for the event callbacks
    String entryToSave = currentEntry;
    
    lv_obj_t *btn_confirm = lv_btn_create(btn_container);
    lv_obj_set_size(btn_confirm, 120, 40);
    lv_obj_add_style(btn_confirm, &style_btn, 0);
    lv_obj_add_style(btn_confirm, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t *confirm_label = lv_label_create(btn_confirm);
    lv_label_set_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);
    
    // Store a copy of the current entry in user data to avoid using the global variable
    lv_obj_set_user_data(btn_confirm, (void*)strdup(formattedEntryStr.c_str()));
    
    lv_obj_add_event_cb(btn_confirm, [](lv_event_t *e) {
        lv_obj_t *btn = lv_event_get_target(e);
        saveEntry(currentEntry); // Save raw entry
        createMainMenu();
        DEBUG_PRINT("Returning to main menu after confirmation");
    }, LV_EVENT_CLICKED, NULL);
    
    // Process UI updates to ensure everything is properly initialized
    lv_timer_handler();
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
    lv_obj_set_style_bg_color(wifi_manager_screen, lv_color_hex(0x1A1A1A), 0); // Dark gray background
    lv_obj_set_style_bg_opa(wifi_manager_screen, LV_OPA_COVER, 0);
    lv_obj_add_flag(wifi_manager_screen, LV_OBJ_FLAG_SCROLLABLE); // Screen scrolls if needed
    current_scroll_obj = wifi_manager_screen;

    // Back Button (Top-Left)
    lv_obj_t* back_btn = lv_btn_create(wifi_manager_screen);
    lv_obj_set_size(back_btn, 60, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0); // Darker gray button
    lv_obj_set_style_radius(back_btn, 5, 0);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT); // Left arrow icon
    lv_obj_center(back_label);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0); // White text
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Title
    lv_obj_t* title = lv_label_create(wifi_manager_screen);
    lv_label_set_text(title, "WiFi Manager");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0); // White text
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Saved Networks Label
    lv_obj_t* saved_label = lv_label_create(wifi_manager_screen);
    lv_label_set_text(saved_label, "Saved Networks");
    lv_obj_set_style_text_color(saved_label, lv_color_hex(0xBBBBBB), 0); // Light gray text
    lv_obj_set_style_text_font(saved_label, &lv_font_montserrat_16, 0);
    lv_obj_align(saved_label, LV_ALIGN_TOP_LEFT, 20, 60); // Aligned with cards

    // Define styles for network items
    static lv_style_t style_network;
    lv_style_init(&style_network);
    lv_style_set_bg_color(&style_network, lv_color_hex(0x2D2D2D)); // Dark gray for cards
    lv_style_set_bg_opa(&style_network, LV_OPA_COVER);
    lv_style_set_border_color(&style_network, lv_color_hex(0x505050)); // Light gray border
    lv_style_set_border_width(&style_network, 1);
    lv_style_set_radius(&style_network, 5);
    lv_style_set_shadow_color(&style_network, lv_color_hex(0x000000)); // Black shadow
    lv_style_set_shadow_width(&style_network, 10);
    lv_style_set_shadow_spread(&style_network, 2);
    lv_style_set_pad_all(&style_network, 5);

    static lv_style_t style_network_pressed;
    lv_style_init(&style_network_pressed);
    lv_style_set_bg_color(&style_network_pressed, lv_color_hex(0x404040)); // Lighter gray when pressed
    lv_style_set_bg_opa(&style_network_pressed, LV_OPA_COVER);

    // Populate Saved Networks
    auto savedNetworks = wifiManager.getSavedNetworks();
    int y_offset = 90; // Start below the "Saved Networks" label
    for (size_t i = 0; i < savedNetworks.size(); i++) {
        lv_obj_t* network_item = lv_obj_create(wifi_manager_screen);
        lv_obj_set_size(network_item, SCREEN_WIDTH - 40, 60);
        lv_obj_set_pos(network_item, 20, y_offset); // Consistent left margin
        lv_obj_add_style(network_item, &style_network, 0);
        lv_obj_add_style(network_item, &style_network_pressed, LV_STATE_PRESSED);

        // SSID Label
        lv_obj_t* ssid_label = lv_label_create(network_item);
        lv_label_set_text(ssid_label, savedNetworks[i].ssid.c_str());
        lv_obj_align(ssid_label, LV_ALIGN_LEFT_MID, 5, 0);
        lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0); // White text
        lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0); // Consistent font

        // Status Indicator
        if (savedNetworks[i].connected) {
            lv_obj_t* status_icon = lv_label_create(network_item);
            lv_label_set_text(status_icon, LV_SYMBOL_OK); // Checkmark for connected
            lv_obj_set_style_text_color(status_icon, lv_color_hex(0x00CC00), 0); // Green
            lv_obj_set_style_text_font(status_icon, &lv_font_montserrat_16, 0); // Match font size
            lv_obj_align(status_icon, LV_ALIGN_RIGHT_MID, -5, 0);
        }

        // Clickable area
        lv_obj_add_event_cb(network_item, [](lv_event_t* e) {
            lv_obj_t* item = lv_event_get_target(e);
            size_t idx = (size_t)lv_obj_get_user_data(item);
            auto savedNetworks = wifiManager.getSavedNetworks();
            if (idx < savedNetworks.size()) {
                createNetworkDetailsScreen(savedNetworks[idx].ssid);
            }
        }, LV_EVENT_CLICKED, (void*)i);
        lv_obj_set_user_data(network_item, (void*)i);

        y_offset += 70; // Spacing between cards
    }

    // Scan Button (Top-Right)
    lv_obj_t* scan_btn = lv_btn_create(wifi_manager_screen);
    lv_obj_set_size(scan_btn, 60, 40);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x333333), 0); // Darker gray button
    lv_obj_set_style_radius(scan_btn, 5, 0);
    lv_obj_add_style(scan_btn, &style_btn, 0);
    lv_obj_add_style(scan_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, LV_SYMBOL_REFRESH); // Search icon
    lv_obj_center(scan_label);
    lv_obj_set_style_text_color(scan_label, lv_color_hex(0xFFFFFF), 0); // White text
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) {
        if (wifiManager.isEnabled()) {
            wifiManager.startScan();
            createWiFiScreen();
        } else {
            lv_obj_t* msgbox = lv_msgbox_create(NULL, "WiFi Disabled", "Please enable WiFi to scan.", NULL, true);
            lv_obj_center(msgbox);
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(wifi_manager_screen);
}

// Create network details screen function
void createNetworkDetailsScreen(const String& ssid) {
    lv_obj_t* details_screen = lv_obj_create(NULL);
    lv_obj_add_style(details_screen, &style_screen, 0);

    // Header
    lv_obj_t* header = lv_obj_create(details_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 40);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text_fmt(title, "Network: %s", ssid.c_str());
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Container for details
    lv_obj_t* container = lv_obj_create(details_screen);
    lv_obj_set_size(container, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 60);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x000000), 0); // Black background
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0); // Opaque background

    // Connection Status
    bool isConnected = wifiManager.isConnected() && wifiManager.getCurrentSSID() == ssid;
    lv_obj_t* status_label = lv_label_create(container);
    lv_label_set_text(status_label, isConnected ? "Status: Connected" : "Status: Disconnected");
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_style(status_label, &style_text, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), 0); // White text

    // Signal Strength
    lv_obj_t* strength_label = lv_label_create(container);
    int rssi = isConnected ? wifiManager.getRSSI() : 0; // Only show RSSI if connected
    int strength = isConnected ? map(rssi, -100, -50, 0, 100) : 0;
    strength = constrain(strength, 0, 100);
    lv_label_set_text_fmt(strength_label, "Signal Strength: %d%%", strength);
    lv_obj_align(strength_label, LV_ALIGN_TOP_LEFT, 10, 40);
    lv_obj_add_style(strength_label, &style_text, 0);
    lv_obj_set_style_text_color(strength_label, lv_color_hex(0xFFFFFF), 0); // White text

    // Connect/Disconnect Button
    lv_obj_t* connect_btn = lv_btn_create(container);
    lv_obj_set_size(connect_btn, 140, 50);
    lv_obj_align(connect_btn, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_add_style(connect_btn, &style_btn, 0);
    lv_obj_add_style(connect_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, isConnected ? "Disconnect" : "Connect");
    lv_obj_center(connect_label);
    String* ssid_ptr = new String(ssid); // Allocate on heap to persist
    lv_obj_add_event_cb(connect_btn, connect_btn_event_cb, LV_EVENT_CLICKED, ssid_ptr);

    // Forget Button
    lv_obj_t* forget_btn = lv_btn_create(container);
    lv_obj_set_size(forget_btn, 140, 50);
    lv_obj_align(forget_btn, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_add_style(forget_btn, &style_btn, 0);
    lv_obj_add_style(forget_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* forget_label = lv_label_create(forget_btn);
    lv_label_set_text(forget_label, "Forget Network");
    lv_obj_center(forget_label);
    String* forget_ssid_ptr = new String(ssid); // Allocate on heap to persist
    lv_obj_add_event_cb(forget_btn, forget_btn_event_cb, LV_EVENT_CLICKED, forget_ssid_ptr);

    // Back Button (proportioned like other buttons)
    lv_obj_t* back_btn = lv_btn_create(details_screen);
    lv_obj_set_size(back_btn, 140, 50); // Changed from 100 x 40 to 140 x 50
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createWiFiManagerScreen();
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(details_screen);
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

static void connect_btn_event_cb(lv_event_t* e) {
    String* ssid = static_cast<String*>(lv_event_get_user_data(e));
    bool isConnected = wifiManager.isConnected() && wifiManager.getCurrentSSID() == *ssid;
    if (isConnected) {
        wifiManager.disconnect(true); // Manual disconnect
        createNetworkDetailsScreen(*ssid); // Refresh screen
    } else {
        auto savedNetworks = wifiManager.getSavedNetworks();
        for (const auto& net : savedNetworks) {
            if (net.ssid == *ssid) {
                showWiFiLoadingScreen(*ssid);
                wifiManager.connect(*ssid, net.password, false);
                break;
            }
        }
    }
    delete ssid; // Clean up allocated memory
}

// Static callback for forget button
static void forget_btn_event_cb(lv_event_t* e) {
    String* ssid = static_cast<String*>(lv_event_get_user_data(e));
    wifiManager.removeNetwork(*ssid);
    createWiFiManagerScreen(); // Return to WiFi Manager screen
    delete ssid; // Clean up allocated memory
}

String getFormattedEntry(const String& entry) {
    String entryData = entry;
    String timestamp = getTimestamp(); // Default to current timestamp

    // Check if the entry contains a timestamp
    int colonPos = entry.indexOf(": ");
    if (colonPos > 0 && colonPos <= 19 && entry.substring(0, 2).toInt() > 0) {
        timestamp = entry.substring(0, colonPos + 1);
        entryData = entry.substring(colonPos + 2);
        DEBUG_PRINTF("Extracted timestamp: %s\n", timestamp.c_str());
        DEBUG_PRINTF("Extracted entry data: %s\n", entryData.c_str());
    } else {
        entryData = entry;
        DEBUG_PRINTF("No timestamp found, using raw entry: %s\n", entryData.c_str());
    }

    // Parse the entry data into parts
    String parts[5];
    int partCount = 0, startIdx = 0;
    DEBUG_PRINTF("Parsing entryData: %s (length: %d)\n", entryData.c_str(), entryData.length());
    for (int i = 0; i < entryData.length() && partCount < 5; i++) {
        if (entryData.charAt(i) == ',') {
            parts[partCount] = entryData.substring(startIdx, i);
            DEBUG_PRINTF("Part %d: %s (from %d to %d)\n", partCount, parts[partCount].c_str(), startIdx, i);
            partCount++;
            startIdx = i + 1;
        }
    }
    if (startIdx < entryData.length()) {
        parts[partCount] = entryData.substring(startIdx);
        DEBUG_PRINTF("Part %d: %s (from %d to end)\n", partCount, parts[partCount].c_str(), startIdx);
        partCount++;
    }
    DEBUG_PRINTF("Total parts found: %d\n", partCount);

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

void initStyles() {
    // Screen style
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(0x1A1A1A));
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);

    // Button style
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x4A90E2));
    lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
    lv_style_set_text_color(&style_btn, lv_color_hex(0xFFFFFF));
    lv_style_set_border_width(&style_btn, 0);

    // Button pressed style
    lv_style_init(&style_btn_pressed);
    lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(0x357ABD));
    lv_style_set_bg_opa(&style_btn_pressed, LV_OPA_COVER);

    // Title style
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_20);
    lv_style_set_text_color(&style_title, lv_color_hex(0xFFFFFF));

    // Card action style
    lv_style_init(&style_card_action);
    lv_style_set_bg_color(&style_card_action, lv_color_hex(0x2D2D2D));
    lv_style_set_bg_opa(&style_card_action, LV_OPA_COVER);
    lv_style_set_border_color(&style_card_action, lv_color_hex(0x4A90E2));
    lv_style_set_border_width(&style_card_action, 1);

    // Card pressed style
    lv_style_init(&style_card_pressed);
    lv_style_set_bg_color(&style_card_pressed, lv_color_hex(0x357ABD));
    lv_style_set_bg_opa(&style_card_pressed, LV_OPA_COVER);

    // Add other styles as needed (e.g., style_text, style_card_info)
    lv_style_init(&style_text);
    lv_style_set_text_color(&style_text, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&style_text, &lv_font_montserrat_14);

    DEBUG_PRINT("Styles initialized");
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
        DEBUG_PRINT("File system not initialized - skipping listSavedEntries");
        return;
    }

    releaseSPIBus();
    SPI_SD.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    File file = SD.open(LOG_FILENAME, FILE_READ);
    if (!file) {
        DEBUG_PRINT("Failed to open log file for reading");
        SPI_SD.end();
        SPI.begin();
        return;
    }

    parsedLogEntries.clear();
    String entryText = "";

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        if (line.length() == 0 && entryText.length() > 0) {
            LogEntry entry;
            entry.text = entryText;
            entry.timestamp = parseTimestamp(entryText);
            parsedLogEntries.push_back(entry);
            entryText = "";
        } else if (line.length() > 0) {
            entryText += line + "\n";
        }
    }

    if (entryText.length() > 0) {
        LogEntry entry;
        entry.text = entryText;
        entry.timestamp = parseTimestamp(entryText);
        parsedLogEntries.push_back(entry);
    }

    file.close();
    SPI_SD.end();
    SPI.begin();
    pinMode(TFT_DC, OUTPUT);
    digitalWrite(TFT_DC, HIGH);

    DEBUG_PRINTF("Total entries loaded: %d\n", parsedLogEntries.size());
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
    
    // Update status bar if visible
    if (status_bar) {
        lv_label_set_text(status_bar, message.c_str());
        lv_obj_set_style_text_color(status_bar, 
            state == WiFiState::WIFI_CONNECTED ? lv_color_hex(0x00FF00) : 
            state == WiFiState::WIFI_CONNECTING ? lv_color_hex(0xFFFF00) : 
            lv_color_hex(0xFF0000), 0);
    }
    
    // Update loading screen if active
    if (wifi_loading_screen != nullptr && lv_obj_is_valid(wifi_loading_screen) && 
        lv_scr_act() == wifi_loading_screen) {
        if (state == WiFiState::WIFI_CONNECTED) {
            updateWiFiLoadingScreen(true, "WiFi Connected!");
        } else if (state == WiFiState::WIFI_DISCONNECTED && 
                  (message.indexOf("failed") >= 0 || message.indexOf("Failed") >= 0)) {
            updateWiFiLoadingScreen(false, "Connection Failed!");
        }
    }
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

static void createDateSettingsScreen() {
    static lv_obj_t* date_screen = nullptr;
    if (date_screen) {
        lv_obj_del(date_screen);
        date_screen = nullptr;
    }
    date_screen = lv_obj_create(NULL);
    lv_obj_add_style(date_screen, &style_screen, 0);

    // Header
    lv_obj_t* header = lv_obj_create(date_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 40);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Set Date");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Container
    lv_obj_t* container = lv_obj_create(date_screen);
    lv_obj_set_size(container, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 60);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, 10, 0);

    // Current Date Label
    lv_obj_t* current_date_label = lv_label_create(container);
    m5::rtc_date_t DateStruct;
    M5.Rtc.getDate(&DateStruct);
    char date_str[32];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", DateStruct.year, DateStruct.month, DateStruct.date);
    lv_label_set_text(current_date_label, date_str);
    lv_obj_set_style_text_font(current_date_label, &lv_font_montserrat_16, 0);

    // Date Picker
    lv_obj_t* date_picker = lv_calendar_create(container);
    lv_obj_set_size(date_picker, 280, 120);
    lv_calendar_set_today_date(date_picker, DateStruct.year, DateStruct.month, DateStruct.date);
    lv_calendar_set_showed_date(date_picker, DateStruct.year, DateStruct.month);
    lv_calendar_set_highlighted_dates(date_picker, &selected_date, 1);

    lv_obj_add_event_cb(date_picker, [](lv_event_t* e) {
        lv_calendar_date_t date;
        if (lv_calendar_get_pressed_date(lv_event_get_target(e), &date)) {
            if (date.year > 0 && date.month > 0 && date.day > 0) {
                selected_date = date;
                lv_calendar_set_highlighted_dates(lv_event_get_target(e), &selected_date, 1);
            }
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Continue Button
    lv_obj_t* continue_btn = lv_btn_create(container);
    lv_obj_set_size(continue_btn, 120, 40);
    lv_obj_add_style(continue_btn, &style_btn, 0);
    lv_obj_add_style(continue_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* continue_label = lv_label_create(continue_btn);
    lv_label_set_text(continue_label, "Continue");
    lv_obj_center(continue_label);
    lv_obj_add_event_cb(continue_btn, [](lv_event_t* e) {
        createTimeSelectionScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Back Button
    lv_obj_t* back_btn = lv_btn_create(container);
    lv_obj_set_size(back_btn, 120, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(date_screen);
}   

void createSettingsScreen() {
    DEBUG_PRINTF("Free heap before settings screen: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    if (settingsScreen) {
        lv_obj_del(settingsScreen);
        settingsScreen = nullptr;
    }
    settingsScreen = lv_obj_create(NULL);
    lv_obj_add_style(settingsScreen, &style_screen, 0);

    // Header
    lv_obj_t* header = lv_obj_create(settingsScreen);
    lv_obj_set_size(header, SCREEN_WIDTH, 40);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Settings");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Settings Container
    lv_obj_t* container = lv_obj_create(settingsScreen);
    lv_obj_set_size(container, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 50);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    current_scroll_obj = container;

    // WiFi Settings Button
    lv_obj_t* wifi_btn = lv_btn_create(container);
    lv_obj_set_size(wifi_btn, 280, 50);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_add_style(wifi_btn, &style_btn, 0);
    lv_obj_add_style(wifi_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, "WiFi Settings");
    lv_obj_center(wifi_label);
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t* e) {
        createWiFiManagerScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Brightness Settings Button (unchanged)
    lv_obj_t* bright_btn = lv_btn_create(container);
    lv_obj_set_size(bright_btn, 280, 50);
    lv_obj_align_to(bright_btn, wifi_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_add_style(bright_btn, &style_btn, 0);
    lv_obj_add_style(bright_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* bright_label = lv_label_create(bright_btn);
    lv_label_set_text(bright_label, "Brightness Settings");
    lv_obj_center(bright_label);
    lv_obj_add_event_cb(bright_btn, [](lv_event_t* e) {
        createBrightnessSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Sound Settings Button (unchanged)
    lv_obj_t* sound_btn = lv_btn_create(container);
    lv_obj_set_size(sound_btn, 280, 50);
    lv_obj_align_to(sound_btn, bright_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_add_style(sound_btn, &style_btn, 0);
    lv_obj_add_style(sound_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* sound_label = lv_label_create(sound_btn);
    lv_label_set_text(sound_label, "Sound Settings");
    lv_obj_center(sound_label);
    lv_obj_add_event_cb(sound_btn, [](lv_event_t* e) {
        createSoundSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Updated "Set Time" Button
    lv_obj_t* time_btn = lv_btn_create(container);
    lv_obj_set_size(time_btn, 280, 50);
    lv_obj_align_to(time_btn, sound_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_add_style(time_btn, &style_btn, 0);
    lv_obj_add_style(time_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* time_label = lv_label_create(time_btn);
    lv_label_set_text(time_label, "Set Time");
    lv_obj_center(time_label);
    lv_obj_add_event_cb(time_btn, [](lv_event_t* e) {
        createDateSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    // Back Button (unchanged)
    lv_obj_t* back_btn = lv_btn_create(settingsScreen);
    lv_obj_set_size(back_btn, 100, 40);
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
    DEBUG_PRINTF("Free heap after settings screen: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
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

    // Back button
    lv_obj_t* back_btn = lv_btn_create(container); // Change from brightness_settings_screen to container
    lv_obj_set_size(back_btn, 140, 50);
    lv_obj_align(back_btn, LV_ALIGN_TOP_MID, 0, 200); // Position it below auto brightness
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);
    
    DEBUG_PRINT("Finished createBrightnessSettingsScreen");
}
// Create a function to show the WiFi loading screen
void showWiFiLoadingScreen(const String& ssid) {
    if (wifi_loading_screen != nullptr) {
        lv_obj_del(wifi_loading_screen);
    }
    
    wifi_loading_screen = lv_obj_create(NULL);
    lv_obj_add_style(wifi_loading_screen, &style_screen, 0);
    
    // Create header
    lv_obj_t* header = lv_obj_create(wifi_loading_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "WiFi Connection");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    
    // Create spinner
    wifi_loading_spinner = lv_spinner_create(wifi_loading_screen, 1000, 60);
    lv_obj_set_size(wifi_loading_spinner, 100, 100);
    lv_obj_align(wifi_loading_spinner, LV_ALIGN_CENTER, 0, -20);
    
    // Create connecting label
    wifi_loading_label = lv_label_create(wifi_loading_screen);
    lv_label_set_text_fmt(wifi_loading_label, "Connecting to %s...", ssid.c_str());
    lv_obj_align(wifi_loading_label, LV_ALIGN_CENTER, 0, 60);
    
    // Create result label (hidden initially)
    wifi_result_label = lv_label_create(wifi_loading_screen);
    lv_obj_set_style_text_font(wifi_result_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(wifi_result_label, "");
    lv_obj_align(wifi_result_label, LV_ALIGN_CENTER, 0, 90);
    lv_obj_add_flag(wifi_result_label, LV_OBJ_FLAG_HIDDEN);
    
    // Create back button (hidden initially)
    lv_obj_t* back_btn = lv_btn_create(wifi_loading_screen);
    lv_obj_set_size(back_btn, 120, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createWiFiManagerScreen();
    }, LV_EVENT_CLICKED, NULL);
    
    // Store back button reference for later use
    lv_obj_set_user_data(wifi_loading_screen, back_btn);
    
    lv_scr_load(wifi_loading_screen);
}
// Function to update the WiFi loading screen based on connection result
void updateWiFiLoadingScreen(bool success, const String& message) {
    if (wifi_loading_screen == nullptr || !lv_obj_is_valid(wifi_loading_screen)) {
        return;
    }
    
    // Hide spinner
    if (wifi_loading_spinner != nullptr && lv_obj_is_valid(wifi_loading_spinner)) {
        lv_obj_add_flag(wifi_loading_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update result label
    if (wifi_result_label != nullptr && lv_obj_is_valid(wifi_result_label)) {
        lv_obj_clear_flag(wifi_result_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(wifi_result_label, message.c_str());
        lv_obj_set_style_text_color(wifi_result_label, 
            success ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000), 0);
    }
    
    // Show back button
    lv_obj_t* back_btn = (lv_obj_t*)lv_obj_get_user_data(wifi_loading_screen);
    if (back_btn != nullptr && lv_obj_is_valid(back_btn)) {
        lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Auto return to WiFi manager screen after successful connection
    if (success) {
        static lv_timer_t* return_timer = nullptr;
        if (return_timer != nullptr) {
            lv_timer_del(return_timer);
        }
        return_timer = lv_timer_create([](lv_timer_t* timer) {
            createWiFiManagerScreen();
            lv_timer_del(timer);
        }, 2000, NULL);
    }
}

// Function to add a time display to a screen// Implementation of time display
void addTimeDisplay(lv_obj_t *screen) {
    lv_obj_t* time_card = lv_obj_create(screen);
    lv_obj_set_size(time_card, 180, 40);
    lv_obj_align(time_card, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_add_style(time_card, &style_card_info, 0);

    lv_obj_t* time_icon = lv_label_create(time_card);
    lv_label_set_text(time_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(time_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(time_icon, lv_color_hex(0x4A90E2), 0);
    lv_obj_align(time_icon, LV_ALIGN_LEFT_MID, 15, 0);

    time_label = lv_label_create(time_card);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_16, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 10, 0);

    updateTimeDisplay(); // Initial update
}

void updateTimeDisplay() {
    if (time_label == nullptr) return;

    m5::rtc_time_t TimeStruct;
    M5.Rtc.getTime(&TimeStruct);
    
    // Convert 24-hour to 12-hour format
    int hour = TimeStruct.hours;
    const char* period = (hour >= 12) ? "PM" : "AM";
    if (hour == 0) {
        hour = 12; // Midnight
    } else if (hour > 12) {
        hour -= 12;
    }

    char timeStr[24];
    snprintf(timeStr, sizeof(timeStr), "%d:%02d:%02d %s", 
             hour, TimeStruct.minutes, TimeStruct.seconds, period);
    lv_label_set_text(time_label, timeStr);
}

// Add this new function for the Power Management screen
void createPowerManagementScreen() {
    lv_obj_t* power_screen = lv_obj_create(NULL);
    lv_obj_add_style(power_screen, &style_screen, 0);
    
    // Header
    lv_obj_t* header = lv_obj_create(power_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 40);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Power Management");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    
    // Container for buttons
    lv_obj_t* container = lv_obj_create(power_screen);
    lv_obj_set_size(container, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 60);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 10, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container, 15, 0);
    
    // Power Off Button
    lv_obj_t* power_off_btn = lv_btn_create(container);
    lv_obj_set_size(power_off_btn, 280, 60);
    lv_obj_add_style(power_off_btn, &style_btn, 0);
    lv_obj_add_style(power_off_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(power_off_btn, lv_color_hex(0xE74C3C), 0); // Red color
    
    lv_obj_t* power_off_label = lv_label_create(power_off_btn);
    lv_label_set_text(power_off_label, LV_SYMBOL_POWER " Power Off");
    lv_obj_set_style_text_font(power_off_label, &lv_font_montserrat_20, 0);
    lv_obj_center(power_off_label);
    
    // Restart Button
    lv_obj_t* restart_btn = lv_btn_create(container);
    lv_obj_set_size(restart_btn, 280, 60);
    lv_obj_add_style(restart_btn, &style_btn, 0);
    lv_obj_add_style(restart_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(restart_btn, lv_color_hex(0xF39C12), 0); // Orange color
    
    lv_obj_t* restart_label = lv_label_create(restart_btn);
    lv_label_set_text(restart_label, LV_SYMBOL_REFRESH " Restart");
    lv_obj_set_style_text_font(restart_label, &lv_font_montserrat_20, 0);
    lv_obj_center(restart_label);
    
    // Sleep Button
    lv_obj_t* sleep_btn = lv_btn_create(container);
    lv_obj_set_size(sleep_btn, 280, 60);
    lv_obj_add_style(sleep_btn, &style_btn, 0);
    lv_obj_add_style(sleep_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(sleep_btn, lv_color_hex(0x3498DB), 0); // Blue color
    
    lv_obj_t* sleep_label = lv_label_create(sleep_btn);
    lv_label_set_text(sleep_label, LV_SYMBOL_DOWNLOAD " Sleep Mode");
    lv_obj_set_style_text_font(sleep_label, &lv_font_montserrat_20, 0);
    lv_obj_center(sleep_label);
    
    // Back Button
    lv_obj_t* back_btn = lv_btn_create(container);
    lv_obj_set_size(back_btn, 140, 50);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    
    // Event handlers
    lv_obj_add_event_cb(power_off_btn, [](lv_event_t* e) {
        // Create countdown screen
        lv_obj_t* countdown_screen = lv_obj_create(NULL);
        lv_obj_add_style(countdown_screen, &style_screen, 0);
        lv_scr_load(countdown_screen);
        
        lv_obj_t* countdown_label = lv_label_create(countdown_screen);
        lv_obj_set_style_text_font(countdown_label, &lv_font_montserrat_24, 0);
        lv_obj_align(countdown_label, LV_ALIGN_CENTER, 0, 0);
        
        // Countdown from 3 to 1
        for (int i = 3; i > 0; i--) {
            lv_label_set_text_fmt(countdown_label, "Powering off in %d...", i);
            lv_task_handler();
            delay(1000);
        }
        
        // Power off
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.sleep();
        M5.Lcd.waitDisplay();
        
        // Use AXP to power off
        M5.In_I2C.bitOn(AXP2101_ADDR, 0x41, 1 << 1, 100000L);
        M5.In_I2C.writeRegister8(AXP2101_ADDR, 0x25, 0b00011011, 100000L);
        M5.In_I2C.writeRegister8(AXP2101_ADDR, 0x10, 0b00110001, 100000L);
        
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_add_event_cb(restart_btn, [](lv_event_t* e) {
        // Create countdown screen
        lv_obj_t* countdown_screen = lv_obj_create(NULL);
        lv_obj_add_style(countdown_screen, &style_screen, 0);
        lv_scr_load(countdown_screen);
        
        lv_obj_t* countdown_label = lv_label_create(countdown_screen);
        lv_obj_set_style_text_font(countdown_label, &lv_font_montserrat_24, 0);
        lv_obj_align(countdown_label, LV_ALIGN_CENTER, 0, 0);
        
        // Countdown from 3 to 1
        for (int i = 3; i > 0; i--) {
            lv_label_set_text_fmt(countdown_label, "Restarting in %d...", i);
            lv_task_handler();
            delay(1000);
        }
        
        // Restart
        ESP.restart();
        
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_add_event_cb(sleep_btn, [](lv_event_t* e) {
        // Create sleep screen
        lv_obj_t* sleep_screen = lv_obj_create(NULL);
        lv_obj_add_style(sleep_screen, &style_screen, 0);
        lv_scr_load(sleep_screen);
        
        lv_obj_t* sleep_label = lv_label_create(sleep_screen);
        lv_obj_set_style_text_font(sleep_label, &lv_font_montserrat_24, 0);
        lv_obj_align(sleep_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(sleep_label, "Entering sleep mode...\nTouch screen to wake");
        lv_task_handler();
        delay(2000);
        
        // Prepare for deep sleep
        // INTEN_P0 TF_DETECT ES7210 EN[0] DIS[1]
        M5.In_I2C.writeRegister8(AW9523_ADDR, 0x06, 0b11111111, 100000L);
        // INTEN_P1 AW88298 FT6336 EN[0] DIS[1] Clear INT
        M5.In_I2C.writeRegister8(AW9523_ADDR, 0x07, 0b11111011, 100000L);
        M5.In_I2C.readRegister8(AW9523_ADDR, 0x00, 100000L);
        M5.In_I2C.readRegister8(AW9523_ADDR, 0x01, 100000L);
        
        pinMode(21, INPUT_PULLUP);
        
        // GPIO21 <- AW9523 INT <- AW9523 P1.2 <- Touch INT
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 0);
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.sleep();
        M5.Lcd.waitDisplay();
        esp_deep_sleep_start();
        
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createMainMenu();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_scr_load(power_screen);
}

// Static function to save the time to RTC
static void save_time_to_rtc() {
    m5::rtc_date_t DateStruct;
    DateStruct.year = selected_date.year;
    DateStruct.month = selected_date.month;
    DateStruct.date = selected_date.day;
    DateStruct.weekDay = 0; // We'll skip weekday calculation for simplicity
    M5.Rtc.setDate(&DateStruct);

    m5::rtc_time_t TimeStruct;
    TimeStruct.hours = selected_hour;
    TimeStruct.minutes = selected_minute;
    TimeStruct.seconds = 0;
    M5.Rtc.setTime(&TimeStruct);

    setSystemTimeFromRTC();
    DEBUG_PRINTF("Time set to: %04d-%02d-%02d %02d:%02d:00\n",
                 selected_date.year, selected_date.month, selected_date.day,
                 selected_hour, selected_minute);
}
// Confirmation dialog event handler
static void confirm_dialog_event_cb(lv_event_t* e) {
    lv_obj_t* dialog = lv_event_get_current_target(e);
    const char* btn_text = lv_msgbox_get_active_btn_text(dialog);
    if (strcmp(btn_text, "Save & Continue") == 0) {
        save_time_to_rtc();
        createSettingsScreen();
    } else {
        createSettingsScreen();
    }
    lv_msgbox_close(dialog);
}
// Time settings screen
static void createTimeSelectionScreen() {
    static lv_obj_t* time_screen = nullptr;
    if (time_screen) {
        lv_obj_del(time_screen);
        time_screen = nullptr;
    }
    time_screen = lv_obj_create(NULL);
    lv_obj_add_style(time_screen, &style_screen, 0);

    // Header
    lv_obj_t* header = lv_obj_create(time_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 40);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Set Time");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Container
    lv_obj_t* container = lv_obj_create(time_screen);
    lv_obj_set_size(container, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 60);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, 10, 0);

    // Current Date Label
    lv_obj_t* current_time_label = lv_label_create(container);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "Date: %04d-%02d-%02d", selected_date.year, selected_date.month, selected_date.day);
    lv_label_set_text(current_time_label, time_str);
    lv_obj_set_style_text_font(current_time_label, &lv_font_montserrat_16, 0);

    // Time Rollers
    lv_obj_t* time_container = lv_obj_create(container);
    lv_obj_set_size(time_container, 280, 60);
    lv_obj_set_style_bg_opa(time_container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(time_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(time_container, 10, 0);

    lv_obj_t* hour_roller = lv_roller_create(time_container);
    lv_roller_set_options(hour_roller, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(hour_roller, 80);
    lv_obj_set_style_text_font(hour_roller, &lv_font_montserrat_16, 0);

    lv_obj_t* minute_roller = lv_roller_create(time_container);
    lv_roller_set_options(minute_roller, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59", LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(minute_roller, 80);
    lv_obj_set_style_text_font(minute_roller, &lv_font_montserrat_16, 0);

    // Set current RTC time
    m5::rtc_time_t TimeStruct;
    M5.Rtc.getTime(&TimeStruct);
    lv_roller_set_selected(hour_roller, TimeStruct.hours, LV_ANIM_OFF);
    lv_roller_set_selected(minute_roller, TimeStruct.minutes, LV_ANIM_OFF);

    // Continue Button with Confirmation
    lv_obj_t* continue_btn = lv_btn_create(container);
    lv_obj_set_size(continue_btn, 120, 40);
    lv_obj_add_style(continue_btn, &style_btn, 0);
    lv_obj_add_style(continue_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* continue_label = lv_label_create(continue_btn);
    lv_label_set_text(continue_label, "Continue");
    lv_obj_center(continue_label);
    lv_obj_add_event_cb(continue_btn, [](lv_event_t* e) {
        lv_obj_t* container = lv_obj_get_parent(lv_event_get_target(e));
        lv_obj_t* time_container = lv_obj_get_child(container, 1);
        lv_obj_t* hour_roller = lv_obj_get_child(time_container, 0);
        lv_obj_t* minute_roller = lv_obj_get_child(time_container, 1);

        selected_hour = lv_roller_get_selected(hour_roller);
        selected_minute = lv_roller_get_selected(minute_roller);

        static const char* btns[] = {"Save & Continue", "Cancel", ""};
        lv_obj_t* msgbox = lv_msgbox_create(NULL, "Confirm", "Save the new date and time?", btns, false);
        lv_obj_add_event_cb(msgbox, confirm_dialog_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_center(msgbox);
    }, LV_EVENT_CLICKED, NULL);

    // Back Button
    lv_obj_t* back_btn = lv_btn_create(container);
    lv_obj_set_size(back_btn, 120, 40);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_add_style(back_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        createSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(time_screen);
}
