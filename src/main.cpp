
/*    ___           ___                       ___                       ___
     /  /\         /__/\          ___        /  /\        ___          /__/|
    /  /::\       _\_ \:\        /  /\      /  /::\      /  /\        |  |:|
   /  /:/\:\     /__/\ \:\      /  /:/     /  /:/\:\    /  /:/        |  |:|
  /  /:/~/::\   _\_ \:\ \:\    /  /:/     /  /:/~/:/   /__/::\      __|__|:|
 /__/:/ /:/\:\ /__/\ \:\ \:\  /  /::\    /__/:/ /:/___ \__\/\:\__  /__/::::\____
 \  \:\/:/__\/ \  \:\ \:\/:/ /__/:/\:\   \  \:\/:::::/    \  \:\/\    ~\~~\::::/
  \  \::/       \  \:\ \::/  \__\/  \:\   \  \::/~~~~      \__\::/     |~~|:|~~
   \  \:\        \  \:\/:/        \  \:\   \  \:\          /__/:/      |  |:|
    \  \:\        \  \::/          \__\/    \  \:\         \__\/       |  |:|
     \__\/         \__\/                     \__\/                     |__|/

 ***************************************************************************
 *                                                                         *
 *   AWTRIX 3, a custom firmware for the Ulanzi clock                  *
 *                                                                         *
 *   Copyright (C) 2024  Stephan Mühl aka Blueforcer                       *
 *                                                                         *
 *   This work is licensed under a                                         *
 *   Creative Commons Attribution-NonCommercial-ShareAlike                 *
 *   4.0 International License.                                            *
 *                                                                         *
 *   More information:                                                     *
 *   https://github.com/Blueforcer/awtrix3/blob/main/LICENSE.md       *
 *                                                                         *
 *   This firmware is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 *                                                                         *
 ***************************************************************************/


#include <Arduino.h>
#include "DisplayManager.h"
#include "PeripheryManager.h"
#include "MQTTManager.h"
#include "ServerManager.h"
#include "Globals.h"
#include "UpdateManager.h"
#include "RTCManager.h"
#include "Functions.h"
#include "timer.h"
#include <Preferences.h>

TaskHandle_t taskHandle;
volatile bool StopTask = false;
bool stopBoot;
const char *FIRST_WIFI_SETUP_KEY = "WIFI_INIT_V1";

void BootAnimation(void *parameter)
{
  const TickType_t xDelay = 1 / portTICK_PERIOD_MS;
  while (true)
  {
    if (StopTask)
    {
      break;
    }
    DisplayManager.HSVtext(4, 6, "AWTRIX", true, 0);
    vTaskDelay(xDelay);
  }
  vTaskDelete(NULL);
}

bool firstWifiSetupPending()
{
  Preferences settings;
  settings.begin("awtrix", false);
  bool done = settings.getBool(FIRST_WIFI_SETUP_KEY, false);
  settings.end();
  return !done;
}

void markFirstWifiSetupDone()
{
  Preferences settings;
  settings.begin("awtrix", false);
  settings.putBool(FIRST_WIFI_SETUP_KEY, true);
  settings.end();
}

int16_t centeredTextX(const char *text, byte textCase)
{
  return (32 - static_cast<int16_t>(getTextWidth(text, textCase))) / 2;
}

void drawBootTextSegment(const char *text, int16_t &x, uint32_t color, byte textCase)
{
  DisplayManager.setTextColor(color);
  DisplayManager.setCursor(x, 6);
  DisplayManager.matrixPrint(text);
  x += static_cast<int16_t>(getTextWidth(text, textCase));
}

void showCenteredBootText(const char *text, uint32_t color, unsigned long durationMs, byte textCase = 2)
{
  DisplayManager.clear();
  DisplayManager.setTextColor(color);
  DisplayManager.setCursor(centeredTextX(text, textCase), 6);
  DisplayManager.matrixPrint(text);
  DisplayManager.show();
  if (durationMs > 0)
    delay(durationMs);
}

void showRtc404()
{
  const byte textCase = 2;
  const char *part1 = "RTC ";
  const char *part2 = "404";
  int16_t x = (32 - static_cast<int16_t>(getTextWidth(part1, textCase) + getTextWidth(part2, textCase))) / 2;
  DisplayManager.clear();
  drawBootTextSegment(part1, x, 0xFFFFFF, textCase);
  drawBootTextSegment(part2, x, 0xFF0000, textCase);
  DisplayManager.show();
  delay(2000);
}

void showWifiOff()
{
  const byte textCase = 2;
  const char *part1 = "WiFi ";
  const char *part2 = "OFF";
  int16_t x = (32 - static_cast<int16_t>(getTextWidth(part1, textCase) + getTextWidth(part2, textCase))) / 2;
  DisplayManager.clear();
  drawBootTextSegment(part1, x, 0xFFFFFF, textCase);
  drawBootTextSegment(part2, x, 0xFF0000, textCase);
  DisplayManager.show();
  delay(2000);
}

void blinkOffline()
{
  for (uint8_t i = 0; i < 3; i++)
  {
    showCenteredBootText("OFFLINE", 0xFF0000, 400, 2);
    DisplayManager.clear();
    DisplayManager.show();
    delay(400);
  }
}

void showDecorativeWifiDisabledBoot()
{
  const unsigned long startTime = millis();
  while (millis() - startTime < 3000)
  {
    DisplayManager.HSVtext(centeredTextX("AWTRIX", 2), 6, "AWTRIX", true, 2);
    delay(25);
  }
}

void setup()
{
  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  delay(2000);
  Serial.begin(115200);
  loadSettings();
  bool wifiSetupPending = firstWifiSetupPending();
  PeripheryManager.setup();
  ServerManager.loadSettings();
  RTCManager.setup();
  bool rtcPresent = RTCManager.isAvailable();
  if (rtcPresent)
  {
    RTCManager.syncSystemFromRtc();
  }
  timer_tick();
  DisplayManager.setup();
  if (!rtcPresent)
  {
    showRtc404();
  }
  showCenteredBootText(VERSION, 0xFFFFFF, 500);

  bool networkRequired = !rtcPresent || wifiSetupPending;
  if (networkRequired && !WIFI_ENABLED)
  {
    WIFI_ENABLED = true;
    saveSettings();
  }

  xTaskCreatePinnedToCore(BootAnimation, "Task", 10000, NULL, 1, &taskHandle, 0);
  ServerManager.setup();
  if (ServerManager.isConnected)
  {
    if (wifiSetupPending)
    {
      markFirstWifiSetupDone();
      wifiSetupPending = false;
    }
    DisplayManager.loadNativeApps();
    DisplayManager.loadCustomApps();
    UpdateManager.setup();
    DisplayManager.startArtnet();
    StopTask = true;
    float x = 4;
    String textForDisplay = "AWTRIX   " + ServerManager.myIP.toString();

    if (WEB_PORT != 80)
    {
      textForDisplay += ":" + String(WEB_PORT);
    }

    int textLength = textForDisplay.length() * 4;
    while (x >= -textLength)
    {
      DisplayManager.HSVtext(x, 6, textForDisplay.c_str(), true, 0);
      x -= 0.18;
    }

    
      if (MQTT_HOST != "")
      {
        DisplayManager.HSVtext(4, 6, "MQTT...", true, 0);
        MQTTManager.setup();
        MQTTManager.tick();
      }
    
  }
  else
  {
    StopTask = true;
    if (networkRequired)
    {
      AP_MODE = true;
    }
    else
    {
      DisplayManager.loadNativeApps();
      DisplayManager.loadCustomApps();
      if (WIFI_ENABLED)
      {
        blinkOffline();
      }
      else
      {
        showDecorativeWifiDisabledBoot();
        showWifiOff();
      }
    }
  }
  delay(200);
  DisplayManager.setBrightness(BRIGHTNESS);
}

void loop()
{
  timer_tick();
  RTCManager.tick(ServerManager.isConnected);
  ServerManager.tick();
  DisplayManager.tick();
  PeripheryManager.tick();
  if (WIFI_ENABLED && ServerManager.isConnected)
  {
    MQTTManager.tick();
  }
}
