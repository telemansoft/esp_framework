#include "Framework.h"
#include "Module.h"
#include "Rtc.h"
#include "Http.h"
#include "Util.h"

uint16_t Framework::rebootCount = 0;
#ifndef DISABLE_MQTT
void Framework::callback(char *topic, byte *payload, unsigned int length)
{
    Log::Info(PSTR("Subscribe: %s payload: %.*s"), topic, length, payload);

    char *cmnd = strrchr(topic, '/');
    if (cmnd == nullptr)
    {
        return;
    }
    cmnd++;
    payload[length] = 0;

    if (strcmp(cmnd, "ota") == 0)
    {
        String str = String((char *)payload);
        Http::OTA(str.endsWith(F(".bin")) ? str : OTA_URL);
    }
    else if (strcmp(cmnd, "restart") == 0)
    {
        ESP_Restart();
    }
    else if (module)
    {
        module->mqttCallback(topic, (char *)payload, cmnd);
    }

    Led::led(200);
}

void Framework::connectedCallback()
{
    Mqtt::subscribe(Mqtt::getCmndTopic(F("#")));
    Led::blinkLED(40, 8);
    if (module)
    {
        module->mqttConnected();
    }
}
#endif

void Framework::tickerPerSecondDo()
{
    perSecond++;
    if (perSecond == 30)
    {
        Rtc::rtcReboot.fast_reboot_count = 0;
        Rtc::rtcRebootSave();
    }
    if (rebootCount >= 3)
    {
        return;
    }
    Rtc::addSecond();
    bitSet(Config::operationFlag, 0);
}

void Framework::one(unsigned long baud)
{
    Rtc::rtcRebootLoad();
    Rtc::rtcReboot.fast_reboot_count++;
    Rtc::rtcRebootSave();
    rebootCount = Rtc::rtcReboot.fast_reboot_count > BOOT_LOOP_OFFSET ? Rtc::rtcReboot.fast_reboot_count - BOOT_LOOP_OFFSET : 0;

    Serial.begin(baud);
    globalConfig.debug.type = 1;
}

void Framework::setup()
{
    Log::Error(PSTR("---------------------  v%s  %s  %d-------------------"), module->getModuleVersion().c_str(), Rtc::GetBuildDateAndTime().c_str(), rebootCount);
    if (rebootCount == 1)
    {
        Config::readConfig();
        module->resetConfig();
    }
    else if (rebootCount == 2)
    {
        Config::readConfig();
        module->resetConfig();
    }
    else if (rebootCount == 3)
    {
        Config::resetConfig();
    }
    else
    {
        Config::readConfig();
    }
    if (globalConfig.uid[0] != '\0')
    {
        strcpy(UID, globalConfig.uid);
    }
    else
    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        sprintf(UID, "%s_%02x%02x%02x", module->getModuleName().c_str(), mac[3], mac[4], mac[5]);
    }
    Util::strlowr(UID);

    Log::Info(PSTR("UID: %s"), UID);
    // Log::Info(PSTR("Config Len: %d"), GlobalConfigMessage_size + 6);

    //Config::resetConfig();
    if (MQTT_MAX_PACKET_SIZE == 128)
    {
        Log::Error(PSTR("WRONG PUBSUBCLIENT LIBRARY USED PLEASE INSTALL THE ONE FROM OMG LIB FOLDER"));
    }

    WifiMgr::connectWifi();
    if (rebootCount >= 3)
    {
        module = NULL;
    }
    else
    {
#ifndef DISABLE_MQTT
        Mqtt::setClient(WifiMgr::wifiClient);
        Mqtt::mqttSetConnectedCallback(connectedCallback);
        Mqtt::mqttSetLoopCallback(callback);
#endif
        module->init();
        Rtc::init();
    }
    Http::begin();

    tickerPerSecond = new Ticker();
    tickerPerSecond->attach(1, tickerPerSecondDo);
}

void Framework::loop()
{
    if (rebootCount >= 3)
    {
        WifiMgr::loop();
        Http::loop();
        return;
    }

    yield();
    Led::loop();
#ifndef DISABLE_MQTT
    yield();
    Mqtt::loop();
#endif
    if (module)
    {
        yield();
        module->loop();
    }
    yield();
    WifiMgr::loop();
    yield();
    Http::loop();

    if (bitRead(Config::operationFlag, 0))
    {
        bitClear(Config::operationFlag, 0);

        yield();
        Rtc::perSecondDo();
        yield();
        Config::perSecondDo();
        yield();
        WifiMgr::perSecondDo();
#ifndef DISABLE_MQTT
        yield();
        Mqtt::perSecondDo();
#endif
        if (module)
        {
            yield();
            module->perSecondDo();
        }
    }
}
