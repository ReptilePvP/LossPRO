# Loss Prevention Log System

## Project Overview
The Loss Prevention Log system is an embedded IoT solution built on the M5Stack CoreS3 platform that enables retail staff to quickly document suspicious activities or incidents in stores. The system provides a standardized digital interface for recording loss prevention data including gender, clothing colors (shirt, pants, shoes), and items of interest. This digital approach replaces traditional paper-based logging methods with a streamlined, consistent format that ensures complete information is captured for each incident.

## Hardware Requirements

### Core Components
- **M5Stack CoreS3**
  - ESP32-S3 microcontroller
  - 2" IPS LCD touchscreen (320x240 resolution)
  - Built-in battery management and charging
  - RTC (Real-Time Clock) for timekeeping
  - SD card slot for data storage
  - Wi-Fi connectivity
  - Details: [M5Stack CoreS3 Official Documentation](https://docs.m5stack.com/en/core/CoreS3)

### Input Peripherals
- **M5Stack Dual Button & Key Unit**
  - Connected via I2C (address 0x58, AW9523 chip)
  - Provides physical navigation controls
  - Complements the touchscreen interface
  - Details: [Dual Button Unit Documentation](https://docs.m5stack.com/en/unit/dual_button)

### Storage
- **Micro SD Card**
  - Used for log file storage
  - Minimum 4GB recommended
  - Class 10 or higher for better performance

## Software Dependencies

### Required Libraries
- M5Unified (1.0.0 or later)
- M5GFX (0.1.6 or later)
- LVGL (8.4.0)
- WiFi (Arduino ESP32 core)
- Preferences (Arduino ESP32 core)
- SPI (Arduino ESP32 core)
- SD (Arduino ESP32 core)
- time.h (Arduino ESP32 core)
- ESP32Time
- HTTPClient (Arduino ESP32 core)
- Wire (Arduino ESP32 core)

### Development Environment
- Arduino IDE 2.2.0 or later
- ESP32 Arduino Core 2.0.0 or later
- Board selection: M5Stack CoreS3

## Installation and Setup

### Hardware Setup
1. **Assembly**
   - Connect the M5Stack Dual Button & Key Unit to the M5Stack CoreS3's Port A (I2C)
   - Insert a formatted micro SD card into the SD card slot
   - Ensure the battery is connected and charged

2. **Power On**
   - Press the power button to turn on the M5Stack CoreS3
   - Verify the display turns on and shows startup information

### Software Installation
1. **Install Arduino IDE**
   - Download and install [Arduino IDE](https://www.arduino.cc/en/software)
   - Configure it for ESP32 development

2. **Install Required Board Packages**
   - Add M5Stack board support URL in Preferences: `https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json`
   - Install "M5Stack" from Boards Manager

3. **Install Required Libraries**
   - Use the Arduino Library Manager to install:
     - M5Unified
     - M5GFX
     - LVGL (version 8.4.0)
     - ESP32Time
   - Other required libraries (WiFi, Preferences, SPI, SD, HTTPClient, Wire) are included with the ESP32 Arduino Core

4. **Configure LVGL**
   - Create an `lv_conf.h` file in your Arduino libraries folder based on the provided template
   - Ensure correct settings for M5Stack CoreS3 (colors, screen size, etc.)
   - See the Reference section for recommended LVGL configuration parameters

5. **Upload the Code**
   - Connect the M5Stack CoreS3 to your computer via USB-C
   - Select "M5Stack CoreS3" as the board type
   - Select the appropriate COM port
   - Set Upload Speed to "921600"
   - Click the Upload button

### Initial Configuration
1. **First Boot**
   - The system will initialize the filesystem (SD card)
   - If no WiFi is configured, it will prompt for WiFi configuration
   - Use the touchscreen or physical buttons to navigate

2. **WiFi Setup**
   - Navigate to Settings > WiFi
   - Select a network from the list or scan for available networks
   - Enter the password using the on-screen keyboard
   - The system will save configured networks for future use

3. **System Settings**
   - Adjust brightness through Settings > Brightness
   - Configure sound settings through Settings > Sound
   - Time synchronization occurs automatically when WiFi is connected

## System Architecture

### Software Structure
The system follows a modular design with the following key components:

1. **Main Application (`Loss_Prevention_Log.ino`)**
   - Core application logic and workflow
   - LVGL UI management
   - Hardware initialization and monitoring
   - Event handling and user input processing

2. **WiFi Management (`WiFiManager.cpp` & `WiFiManager.h`)**
   - Custom implementation for WiFi management
   - Network scanning, connection, and state handling
   - Saved network management with prioritization
   - Complete state machine design for robust operation

3. **Screen Transitions (`screen_transition.h`)**
   - Custom implementation for smooth screen transitions
   - Multiple transition effects (fade, slide, zoom)
   - Consistent navigation experience
   - Performance-optimized animations

4. **User Interface**
   - Built with LVGL 8.4.0
   - Card-style UI components for modern look and feel
   - Multiple well-defined screens for different functions
   - Enhanced with custom styles and layouts
   - Optimized scrollable lists and non-scrollable headers
   - Smooth animations and transitions between screens

5. **Data Storage**
   - SD card-based log storage
   - Structured log format with timestamps
   - Optional webhook integration for remote logging

6. **System Utilities**
   - Battery monitoring
   - Time synchronization via NTP
   - Status indicators and notifications

### State Management
The system uses state machines for managing:
- WiFi connection and scanning
- User interface workflow
- Data entry process
- Log navigation
- Screen transitions

### Data Flow
1. User selects log entry details (gender, colors, items)
2. System formats entry with timestamp
3. Entry is saved to SD card
4. Optional webhook sends data to remote server
5. User can view saved entries from the logs screen

## File Structure

### Core Files
- **`Loss_Prevention_Log.ino`** - Main application with UI and logic
- **`WiFiManager.h`** - Header defining the WiFiManager class
- **`WiFiManager.cpp`** - Implementation of WiFi management functionality
- **`screen_transition.h`** - Screen transition effects and animations
- **`lv_conf.h`** - LVGL configuration file
- **`README.md`** - This documentation file
- **`PROJECT_DOCUMENTATION.md`** - Detailed technical documentation
- **`References.md`** - Reference materials and function listings
- **`Card Style UI.md`** - Card-style UI implementation details
- **`SCREEN TRANSITIONS IMPLEMENTATION GUIDE.md`** - Guide for implementing screen transitions

### Data Files
- **`log.txt`** - Main log file stored on SD card
- **`wifi_config`** - Stored in Preferences for WiFi credentials
- **`settings`** - Stored in Preferences for system settings

## Usage Guide

### Main Menu Navigation
- **Log New Entry**: Start the process of logging a new suspicious activity
- **View Logs**: Browse through previously saved log entries
- **Settings**: Access system configuration options
- **WiFi**: Configure wireless network settings
- **Power Management**: Control device power options (power off, restart, sleep)

### Adding a New Entry
1. From the main menu, select "Log New Entry"
2. Select the gender (Male/Female)
3. Select clothing colors (shirt, pants, shoes) - multiple selections allowed
4. Select items of interest - multiple selections allowed
5. Review the entry on the confirmation screen
6. Confirm to save the entry to the log file

### Viewing Logs
1. From the main menu, select "View Logs"
2. Navigate through entries using the pagination controls
3. Entries are displayed with timestamps in reverse chronological order (newest first)
4. Use physical buttons or touchscreen to scroll through entries

### System Settings
1. **WiFi Configuration**
   - Scan for networks
   - Connect to selected network
   - Manage saved networks
   - View connection status

2. **Display Settings**
   - Adjust brightness (Low, Medium, High, or custom)
   - Toggle auto-brightness

3. **Sound Settings**
   - Toggle sound on/off
   - Adjust volume level

### Power Management
1. From the main menu, select "Power"
2. Choose from three options:
   - **Power Off**: Safely powers down the device with a 3-second countdown
   - **Restart**: Restarts the device with a 3-second countdown
   - **Sleep Mode**: Puts the device into deep sleep mode until the screen is touched
3. For Sleep Mode, the device enters a low-power state and can be awakened by touching the screen

### Physical Controls
- **Dual Button & Key Unit**
  - Use buttons for menu navigation
  - Scroll through lists and options
  - Confirm selections
- **Touchscreen**
  - Tap buttons and menu items
  - Swipe for scrolling
  - Touch keyboard for text input

## Customization Options

### Adding Custom Colors
To add additional color options for shirt, pants, or shoes:
1. Locate the `createColorMenuShirt()`, `createColorMenuPants()`, or `createColorMenuShoes()` functions
2. Add new color entries to the respective arrays
3. Ensure proper color values and labels

### Adding Custom Items
To add additional items of interest:
1. Locate the `createItemMenu()` function
2. Add new item entries to the items array
3. Update UI layout if necessary

### Modifying Log Format
To change the log entry format:
1. Modify the `getFormattedEntry()` function
2. Update the `createViewLogsScreen()` function to properly display the new format

### Webhook Integration
To enable remote logging via webhook:
1. Configure the webhook URL in `sendWebhook()` function
2. Enable webhook sending by uncommenting the relevant code in `saveEntry()`
3. Customize the payload format as needed

## Troubleshooting

### WiFi Connection Issues
- **Problem**: Unable to connect to WiFi networks
  - **Solution**: Ensure correct password entry, check signal strength, verify network compatibility (2.4GHz)
  - **Technical**: WiFi connection process is managed by the WiFiManager class with proper timeout handling

- **Problem**: WiFi disconnects frequently
  - **Solution**: Check network stability, ensure power settings allow persistent WiFi
  - **Technical**: WiFiManager implements reconnection logic with exponential backoff

### SD Card Issues
- **Problem**: SD card not detected
  - **Solution**: Ensure card is properly inserted, formatted as FAT32, and functional
  - **Technical**: SPI connections are configured for CoreS3 pinout (SCK: 36, MISO: 35, MOSI: 37, CS: 4)

- **Problem**: Unable to save or read logs
  - **Solution**: Check SD card write protection, available space, and file system integrity
  - **Technical**: File operations include proper error handling and status reporting

### Display Issues
- **Problem**: Screen unresponsive
  - **Solution**: Check power status, try resetting the device
  - **Technical**: Touch calibration is managed through `my_touchpad_read()` function

- **Problem**: UI elements misaligned
  - **Solution**: Ensure proper LVGL configuration in lv_conf.h
  - **Technical**: UI is designed for 320x240 resolution specific to CoreS3

### Performance Optimization
- **For slow UI responsiveness**: Adjust LVGL refresh rate in `screenTickPeriod`
- **For battery optimization**: Adjust WiFi reconnection intervals and scan frequency
- **For memory management**: Monitor stack usage and optimize data structures

## Advanced Features

### Time Synchronization
The system uses NTP for time synchronization when connected to WiFi, with fallback to the RTC:
- NTP servers are configured in `syncTimeWithNTP()`
- RTC is used when WiFi is unavailable
- Time format is 12-hour with AM/PM indication

### Battery Management
Battery status is monitored and displayed with:
- Percentage display
- Color-coded indicator (green, yellow, red)
- Charging status detection
- Low battery warnings

### SPI Bus Management
The system implements proper SPI bus management for SD card operations:
- Custom SPI instance for SD card
- Proper release of SPI bus after operations
- Error handling for SPI communication

## Future Enhancements

### Potential Improvements
1. **Remote Synchronization**
   - Cloud backup of logs
   - Two-way synchronization
   - API integration with security systems

2. **Enhanced User Interface**
   - Multiple UI themes
   - Customizable menus
   - Enhanced visualization of log data

3. **Advanced Features**
   - Photo capture with ESP32-CAM integration
   - Voice notes using I2S microphone
   - Barcode/QR code scanning for item identification

4. **Security Enhancements**
   - Encrypted log storage
   - User authentication
   - Role-based access control

## Development Guidelines

### Code Style
- Follow Arduino coding conventions
- Use consistent indentation (2 spaces)
- Include comments for complex logic
- Use descriptive variable and function names

### UI Development
- Maintain consistent styling using LVGL themes
- Follow the existing pattern for screen creation
- Use the defined style variables for UI elements
- Properly clean up resources when screens are destroyed

### State Management
- Follow the state machine pattern in WiFiManager
- Implement proper error handling and recovery
- Consider edge cases in user interaction flow
- Manage memory carefully, especially with string operations

### Testing
- Test all features after modifications
- Verify proper operation on actual hardware
- Check edge cases (network disconnections, power loss)
- Validate log file integrity after changes

## Performance Considerations

### Memory Management
- ESP32-S3 has limited RAM (512KB)
- LVGL requires significant memory for UI elements
- Use static allocation where possible
- Monitor stack usage during development

### Power Optimization
- WiFi is a major power consumer
- Implement sleep modes for extended battery life
- Consider reducing screen brightness
- Optimize refresh rates and processing intervals

### Storage Considerations
- SD card write operations are relatively slow
- Batch writes when possible
- Implement proper error checking for I/O operations
- Consider wear leveling for flash-based storage

## License and Attribution
This project is provided as open-source software under the MIT License.

### Acknowledgments
- M5Stack for hardware and basic libraries
- LVGL project for the UI framework
- Contributors to the ESP32 Arduino core
- All contributors to the project

---

This README provides comprehensive documentation for the Loss Prevention Log System. For more detailed technical information, refer to the `PROJECT_DOCUMENTATION.md` file.
