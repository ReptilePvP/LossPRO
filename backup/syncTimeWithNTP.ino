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

        DEBUG_PRINT("RTC updated with NTP time");
    } else {
        DEBUG_PRINT("Failed to sync with NTP");
    }
}