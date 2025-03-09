# Loss Prevention Log System Documentation

## Project Overview

The Loss Prevention Log system is an IoT device built on the M5Stack CoreS3 platform with M5Stack Dual button & Key unit for input. It serves as a digital logging system for retail loss prevention, allowing staff to quickly document suspicious activities or incidents with standardized information including gender, clothing colors, and items of interest.

## Hardware Components

### Primary Hardware
- **M5Stack CoreS3**: The main computing and display unit featuring:
  - ESP32-S3 microcontroller
  - 2" IPS LCD touchscreen (320x240 resolution)
  - Built-in battery management
  - RTC (Real-Time Clock)
  - SD card slot for data storage
  - Wi-Fi connectivity

### Input Peripherals
- **M5Stack Dual button & Key unit**: Provides physical button inputs for navigation and interaction
  - Used for scrolling through menus and confirming selections
  - Complements the touchscreen interface

## Software Architecture

### Core Components

1. **Main Application (`Loss_Prevention_Log.ino`)**
   - Handles the overall application flow and user interface
   - Manages the LVGL graphics library for UI rendering
   - Implements the loss prevention logging workflow
   - Manages file system operations for log storage

2. **WiFi Management (`WiFiManager.cpp` & `WiFiManager.h`)**
   - Custom WiFi connection management system
   - Handles network scanning, connection, and reconnection
   - Stores and prioritizes saved networks
   - Provides callbacks for connection status updates

3. **User Interface**
   - Built with LVGL (Light and Versatile Graphics Library) (8.4.0)
   - Features multiple screens for different functions:
     - Main menu
     - Gender selection
     - Color selection (shirt, pants, shoes)
     - Item selection
     - Confirmation screen
     - WiFi configuration
     - Log viewing
     - Settings screens

4. **Data Storage**
   - Uses SD card for log file storage
   - Implements file operations with error handling
   - Stores logs in a text-based format with timestamps

5. **Time Synchronization**
   - NTP (Network Time Protocol) for internet time synchronization
   - Fallback to RTC when internet is unavailable
   - Time display in 12-hour format with AM/PM

6. **Network Connectivity**
   - WiFi connection for time synchronization
   - Optional webhook functionality for remote logging

## Functional Workflow

1. **Startup Sequence**
   - Initialize hardware components (display, touch, SD card)
   - Set up LVGL UI framework
   - Load saved WiFi networks and attempt connection
   - Synchronize time with NTP if connected
   - Display main menu

2. **Logging Process**
   - Select gender (Male/Female)
   - Select clothing colors (shirt, pants, shoes)
   - Select items of interest
   - Confirm and save entry
   - Entry is timestamped and saved to SD card
   - Optional: Send entry to webhook if configured

3. **Log Management**
   - View saved logs with pagination
   - Logs are displayed with timestamps
   - Sorted by recency (newest first)

4. **Settings Management**
   - WiFi configuration
   - Sound settings
   - Brightness control
   - Time settings

## Key Files and Their Functions

### Main Application Files
- **`Loss_Prevention_Log.ino`**: Main application entry point and core functionality
- **`WiFiManager.cpp`**: Implementation of WiFi management functionality
- **`WiFiManager.h`**: Header file defining the WiFiManager class and related structures

### Data Files
- **`log.txt`**: Main log file stored on the SD card
- **`wifi_config`**: Saved in Preferences storage for WiFi credentials

## Important Functions

### UI Management
- `createMainMenu()`: Creates the main application menu
- `createGenderMenu()`: Creates the gender selection screen
- `createColorMenuShirt()`, `createColorMenuPants()`, `createColorMenuShoes()`: Color selection screens
- `createItemMenu()`: Creates the item selection screen
- `createConfirmScreen()`: Creates the confirmation screen
- `createViewLogsScreen()`: Creates the log viewing screen
- `createSettingsScreen()`: Creates the settings menu

### WiFi Management
- `WiFiManager::connect()`: Connects to a specific WiFi network
- `WiFiManager::startScan()`: Initiates a WiFi network scan
- `WiFiManager::addNetwork()`: Adds a network to saved networks
- `scanNetworks()`: Scans for available WiFi networks
- `connectToSavedNetworks()`: Attempts to connect to previously saved networks

### Data Management
- `appendToLog()`: Appends a new entry to the log file
- `getFormattedEntry()`: Formats the entry with all selected attributes
- `getTimestamp()`: Gets the current timestamp for log entries
- `listSavedEntries()`: Lists all saved log entries
- `saveEntry()`: Saves the current entry to storage

### System Functions
- `initFileSystem()`: Initializes the SD card file system
- `syncTimeWithNTP()`: Synchronizes time with NTP servers
- `updateBatteryIndicator()`: Updates the battery level indicator
- `updateWifiIndicator()`: Updates the WiFi connection indicator

## Hardware Interfaces

### Display
- 320x240 pixel IPS LCD touchscreen
- Managed through LVGL library
- Touch input handled by `my_touchpad_read()` function

### SD Card
- Connected via SPI interface
- Pins defined in the main sketch:
  - SCK: 36
  - MISO: 35
  - MOSI: 37
  - CS: 4

### Button Interface
- M5Stack Dual button & Key unit connected via I2C
- I2C address: 0x58 (AW9523 chip)
- Used for navigation and selection

## Development References

### M5Stack CoreS3 Resources
- [M5Stack CoreS3 Official Documentation](https://docs.m5stack.com/en/core/CoreS3)
- [M5Stack CoreS3 Pinout Diagram](https://docs.m5stack.com/en/core/CoreS3)
- [M5Stack CoreS3 GitHub Repository](https://github.com/m5stack/M5CoreS3)

### M5Stack Dual Button & Key Unit Resources
- [Dual Button Unit Documentation](https://docs.m5stack.com/en/unit/dual_button)
- [Key Unit Documentation](https://docs.m5stack.com/en/unit/key)

### Libraries
- [M5Unified](https://github.com/m5stack/M5Unified): Unified library for M5Stack devices
- [M5GFX](https://github.com/m5stack/M5GFX): Graphics library for M5Stack
- [LVGL](https://lvgl.io/): Light and Versatile Graphics Library
- [ESP32Time](https://github.com/fbiego/ESP32Time): Time management library for ESP32

## Future Development Considerations

### Potential Enhancements
1. **Cloud Integration**
   - Implement full cloud synchronization of logs
   - Add user authentication for secure access

2. **Advanced UI Features**
   - Add photo capture capability
   - Implement search functionality for logs
   - Add filtering options for log viewing

3. **Hardware Expansion**
   - Support for additional M5Stack modules
   - Barcode scanner integration
   - RFID reader support

4. **Performance Optimizations**
   - Battery life improvements
   - Memory usage optimization
   - Faster UI rendering

### Known Limitations
1. Limited battery life when WiFi is enabled
2. SD card operations can be slow for large log files
3. Touch interface may require calibration in some environments

## Troubleshooting Guide

### Common Issues

1. **WiFi Connection Problems**
   - Check saved network credentials
   - Ensure network is within range
   - Verify router settings (2.4GHz networks are more reliable)

2. **SD Card Errors**
   - Ensure SD card is properly formatted (FAT32)
   - Check if SD card is properly inserted
   - Try a different SD card if problems persist

3. **Time Synchronization Issues**
   - Verify WiFi connection is working
   - Check if NTP servers are accessible
   - Manually set time if automatic sync fails

4. **Battery Issues**
   - Ensure device is properly charged
   - Reduce screen brightness to extend battery life
   - Disable WiFi when not needed

## Conclusion

The Loss Prevention Log system provides a robust, portable solution for retail loss prevention documentation. Built on the versatile M5Stack CoreS3 platform with the Dual button & Key unit, it offers an intuitive interface for quick logging of suspicious activities. The system's modular design allows for future enhancements and customizations to meet evolving needs.
