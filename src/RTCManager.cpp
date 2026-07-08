#include "RTCManager.h"
#include "Globals.h"

#include <Wire.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>

namespace
{
    constexpr uint8_t DS3231_ADDRESS = 0x68;
    constexpr uint8_t DS3231_STATUS_REGISTER = 0x0F;
    constexpr uint8_t DS3231_OSF_BIT = 0x80;
    constexpr uint32_t RTC_SYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
    constexpr time_t MIN_VALID_EPOCH = 1672531200; // 2023-01-01 00:00:00 UTC

    bool rtcAvailable = false;
    bool rtcTimeValid = false;
    bool networkTimeAvailable = false;
    volatile bool ntpSyncPending = false;
    unsigned long lastRtcSync = 0;

    uint8_t bcdToDec(uint8_t value)
    {
        return ((value >> 4) * 10) + (value & 0x0F);
    }

    uint8_t decToBcd(uint8_t value)
    {
        return ((value / 10) << 4) | (value % 10);
    }

    bool readRegisters(uint8_t reg, uint8_t *buffer, size_t length)
    {
        Wire.beginTransmission(DS3231_ADDRESS);
        Wire.write(reg);
        if (Wire.endTransmission() != 0)
            return false;

        if (Wire.requestFrom(DS3231_ADDRESS, static_cast<uint8_t>(length)) != length)
            return false;

        for (size_t i = 0; i < length; i++)
        {
            buffer[i] = Wire.read();
        }
        return true;
    }

    bool writeRegister(uint8_t reg, uint8_t value)
    {
        Wire.beginTransmission(DS3231_ADDRESS);
        Wire.write(reg);
        Wire.write(value);
        return Wire.endTransmission() == 0;
    }

    bool writeRegisters(uint8_t reg, const uint8_t *buffer, size_t length)
    {
        Wire.beginTransmission(DS3231_ADDRESS);
        Wire.write(reg);
        for (size_t i = 0; i < length; i++)
        {
            Wire.write(buffer[i]);
        }
        return Wire.endTransmission() == 0;
    }

    bool devicePresent()
    {
        Wire.beginTransmission(DS3231_ADDRESS);
        return Wire.endTransmission() == 0;
    }

    void applyTimezone()
    {
        setenv("TZ", NTP_TZ.c_str(), 1);
        tzset();
    }

    bool isDateValid(const tm &timeInfo)
    {
        const int year = timeInfo.tm_year + 1900;
        if (year < 2023 || year > 2099)
            return false;
        if (timeInfo.tm_mon < 0 || timeInfo.tm_mon > 11)
            return false;
        if (timeInfo.tm_mday < 1 || timeInfo.tm_mday > 31)
            return false;
        if (timeInfo.tm_hour < 0 || timeInfo.tm_hour > 23)
            return false;
        if (timeInfo.tm_min < 0 || timeInfo.tm_min > 59)
            return false;
        if (timeInfo.tm_sec < 0 || timeInfo.tm_sec > 59)
            return false;
        return true;
    }

    bool oscillatorStopped()
    {
        uint8_t status = 0;
        if (!readRegisters(DS3231_STATUS_REGISTER, &status, 1))
            return true;
        return (status & DS3231_OSF_BIT) != 0;
    }

    void clearOscillatorStopFlag()
    {
        uint8_t status = 0;
        if (readRegisters(DS3231_STATUS_REGISTER, &status, 1))
        {
            writeRegister(DS3231_STATUS_REGISTER, status & ~DS3231_OSF_BIT);
        }
    }

    bool readRtcLocalTime(tm &timeInfo)
    {
        uint8_t data[7];
        if (oscillatorStopped())
            return false;
        if (!readRegisters(0x00, data, sizeof(data)))
            return false;

        memset(&timeInfo, 0, sizeof(timeInfo));
        timeInfo.tm_sec = bcdToDec(data[0] & 0x7F);
        timeInfo.tm_min = bcdToDec(data[1] & 0x7F);

        if (data[2] & 0x40)
        {
            int hour = bcdToDec(data[2] & 0x1F);
            if (data[2] & 0x20)
            {
                if (hour < 12)
                    hour += 12;
            }
            else if (hour == 12)
            {
                hour = 0;
            }
            timeInfo.tm_hour = hour;
        }
        else
        {
            timeInfo.tm_hour = bcdToDec(data[2] & 0x3F);
        }

        timeInfo.tm_wday = bcdToDec(data[3] & 0x07) % 7;
        timeInfo.tm_mday = bcdToDec(data[4] & 0x3F);
        timeInfo.tm_mon = bcdToDec(data[5] & 0x1F) - 1;
        timeInfo.tm_year = bcdToDec(data[6]) + 100;
        timeInfo.tm_isdst = -1;

        return isDateValid(timeInfo);
    }

    bool currentSystemTimeValid()
    {
        time_t current = time(nullptr);
        return current >= MIN_VALID_EPOCH;
    }

    void onSntpTimeSync(struct timeval *tv)
    {
        ntpSyncPending = true;
    }
}

RTCManager_ &RTCManager_::getInstance()
{
    static RTCManager_ instance;
    return instance;
}

RTCManager_ &RTCManager = RTCManager_::getInstance();

void RTCManager_::setup()
{
    applyTimezone();
    rtcAvailable = devicePresent();
    rtcTimeValid = false;
    ntpSyncPending = false;
    networkTimeAvailable = false;
    lastRtcSync = 0;
    sntp_set_time_sync_notification_cb(onSntpTimeSync);

    if (DEBUG_MODE)
    {
        DEBUG_PRINTLN(rtcAvailable ? F("DS3231 detected") : F("DS3231 not detected"));
    }
}

void RTCManager_::tick(bool networkConnected)
{
    if (!rtcAvailable || !networkConnected || WiFi.status() != WL_CONNECTED)
        return;

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED || ntpSyncPending)
    {
        networkTimeAvailable = true;
    }

    const bool intervalElapsed = networkTimeAvailable && (lastRtcSync == 0 || millis() - lastRtcSync >= RTC_SYNC_INTERVAL_MS);
    if (!ntpSyncPending && !intervalElapsed)
        return;

    ntpSyncPending = false;
    const unsigned long syncAttemptTime = millis();
    if (syncRtcFromSystem() && DEBUG_MODE)
    {
        DEBUG_PRINTLN(F("DS3231 synced from network time"));
    }
    else if (lastRtcSync == 0)
    {
        lastRtcSync = syncAttemptTime;
    }
}

bool RTCManager_::syncSystemFromRtc()
{
    if (!rtcAvailable)
        return false;

    applyTimezone();

    tm timeInfo;
    if (!readRtcLocalTime(timeInfo))
    {
        rtcTimeValid = false;
        if (DEBUG_MODE)
            DEBUG_PRINTLN(F("DS3231 time invalid"));
        return false;
    }

    time_t rtcEpoch = mktime(&timeInfo);
    if (rtcEpoch < MIN_VALID_EPOCH)
    {
        rtcTimeValid = false;
        return false;
    }

    timeval tv = {rtcEpoch, 0};
    settimeofday(&tv, nullptr);
    rtcTimeValid = true;

    if (DEBUG_MODE)
        DEBUG_PRINTLN(F("System time restored from DS3231"));

    return true;
}

bool RTCManager_::syncRtcFromSystem()
{
    if (!rtcAvailable || !currentSystemTimeValid())
        return false;

    applyTimezone();

    time_t current = time(nullptr);
    tm timeInfo;
    localtime_r(&current, &timeInfo);

    if (!isDateValid(timeInfo))
        return false;

    uint8_t data[7] = {
        decToBcd(timeInfo.tm_sec),
        decToBcd(timeInfo.tm_min),
        decToBcd(timeInfo.tm_hour),
        decToBcd(timeInfo.tm_wday == 0 ? 7 : timeInfo.tm_wday),
        decToBcd(timeInfo.tm_mday),
        decToBcd(timeInfo.tm_mon + 1),
        decToBcd((timeInfo.tm_year + 1900) - 2000)};

    if (!writeRegisters(0x00, data, sizeof(data)))
        return false;

    clearOscillatorStopFlag();
    rtcTimeValid = true;
    lastRtcSync = millis();
    return true;
}

bool RTCManager_::isAvailable()
{
    return rtcAvailable;
}

bool RTCManager_::hasValidTime()
{
    return rtcTimeValid;
}
