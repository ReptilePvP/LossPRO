void syncTimeWithNTP() {
    if (WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINT("Syncing time with NTP server...");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            DEBUG_PRINTF("Time synchronized: %02d:%02d:%02d %02d/%02d/%04d\n", 
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        } else {
            DEBUG_PRINT("Failed to obtain time");
        }
    } else {
        DEBUG_PRINT("WiFi not connected, cannot sync time");
    }
}
