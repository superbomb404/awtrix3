#ifndef RTCManager_h
#define RTCManager_h

#include <Arduino.h>

class RTCManager_
{
private:
    RTCManager_() = default;

public:
    static RTCManager_ &getInstance();
    void setup();
    void tick(bool networkConnected);
    bool syncSystemFromRtc();
    bool syncRtcFromSystem();
    bool isAvailable();
    bool hasValidTime();
};

extern RTCManager_ &RTCManager;

#endif
