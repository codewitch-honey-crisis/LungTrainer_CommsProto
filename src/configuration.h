#ifndef LT_CONFIGURATION_H
#define LT_CONFIGURATION_H
#include <atomic>
#include <EEPROM.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <esp_wps.h>
#include <WiFiClient.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>
#include "status_led.h"
#define BLE_SERVICE_UUID "F9A2F78B-EE0A-43C6-A72D-EB606EA56D9C"
static NimBLEUUID g_blesvcid(BLE_SERVICE_UUID);
#define BLE_CFG_SERVICE_UUID "5AB457FD-FBAD-475B-97A0-29900940A47B"
static NimBLEUUID g_blecfgsvcid(BLE_CFG_SERVICE_UUID);

#define CONFIG_TIMEOUT 30000

#define ESP_WPS_MODE WPS_TYPE_PBC
#define ESP_MANUFACTURER "ESPRESSIF"
#define ESP_MODEL_NUMBER "ESP32"
#define ESP_MODEL_NAME "ESPRESSIF IOT"
#define ESP_DEVICE_NAME "ESP STATION"

#define PIN_CONNECT 39

#define NAME_GEN_SVC_URL "http://namey.muffinlabs.com/name.json?type=female"
#define NAME_GEN_SVC_HOST "namey.muffinlabs.com"
#define NAME_GEN_SVC_PATH "/name.json?type=female"

#define TIME_SVC_URL "http://worldtimeapi.org/api/ip"
#define TIME_SVC_HOST "worldtimeapi.org"
#define TIME_SVC_PATH "/api/ip"
static void ble_scan_ended_callback(NimBLEScanResults results);
static void wps_init_config();
static void wifi_callback(WiFiEvent_t event, system_event_info_t info);
static esp_wps_config_t g_wifi_wps_config;
static NimBLEAdvertisedDevice *g_ble_adv_device;
class Configuration
{
private:
    struct
    {
        char name[64];
        char ssid[33];
        char passkey[64];
        struct
        {
            int32_t offsetX;
            int32_t offsetY;
        } calibration;
        struct
        {
            int isWifiSet : 1;
            int isNameSet : 1;
            int isCalibrated : 1;
            int isClockSet : 1;
        } flags;
    } m_stored;

    std::atomic_bool m_isConfiguring;
    std::atomic_bool m_isBTInitialized;

    uint32_t m_cfgStartTS;

    bool recalibrate()
    {
        // TODO: do recalibration here.
        return false;
    }

    class BleAdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks
    {

        void onResult(NimBLEAdvertisedDevice *advertisedDevice)
        {
            Serial.println(advertisedDevice->toString().c_str());
            if (advertisedDevice->isAdvertisingService(g_blecfgsvcid))
            {
                Serial.println(F("BLE Advertised Application found"));

                NimBLEDevice::getScan()->stop();
                WiFi.disconnect(true, true);
                g_statusLed.set(1, 0, 1);

                g_ble_adv_device = advertisedDevice;
            }
        }
    };
    class ClientCallbacks : public NimBLEClientCallbacks
    {
        void onConnect(NimBLEClient *pClient)
        {
            // After connection we should change the parameters if we don't need fast response times.
            //pClient->updateConnParams(40, 40, 0, 100);
        }

        void onDisconnect(NimBLEClient *pClient)
        {
            //Serial.print(F("BLE "));
            //Serial.print(pClient->getPeerAddress().toString().c_str());
            //Serial.println(F(" disconnected - Starting scan"));
            //NimBLEDevice::getScan()->start(0, ble_scan_ended_callback);
        }

        /** Called when the peripheral requests a change to the connection parameters.
     *  Return true to accept and apply them or false to reject and keep
     *  the currently used parameters. Default will return true.
     */
        bool onConnParamsUpdateRequest(NimBLEClient *pClient, const ble_gap_upd_params *params)
        {
            if (params->itvl_min < 24)
            { /** 1.25ms units */
                return false;
            }
            else if (params->itvl_max > 40)
            { /** 1.25ms units */
                return false;
            }
            else if (params->latency > 2)
            { /** Number of intervals allowed to skip */
                return false;
            }
            else if (params->supervision_timeout > 100)
            { /** 10ms units */
                return false;
            }

            return true;
        }

        /********************* Security handled here **********************
    ****** Note: these are the same return values as defaults ********/
        uint32_t onPassKeyRequest()
        {
            Serial.println(F("BLE client passkey request"));
            /** return the passkey to send to the server */
            return 0;
        }

        bool onConfirmPIN(uint32_t pass_key)
        {
            Serial.print(F("BLE the passkey YES/NO number: "));
            Serial.println(pass_key);
            /** Return false if passkeys don't match. */
            return true;
        }

        /** Pairing process complete, we can check the results in ble_gap_conn_desc */
        void onAuthenticationComplete(ble_gap_conn_desc *desc)
        {
            if (!desc->sec_state.encrypted)
            {
                Serial.println(F("BLE encrypt connection failed - disconnecting"));
                /** Find the client with the connection handle provided in desc */
                NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
                return;
            }
        }
    };

public:
    void begin()
    {
        m_cfgStartTS = 0;
        m_isBTInitialized = false;
        g_ble_adv_device = nullptr;
        pinMode(PIN_CONNECT, INPUT);
        WiFi.onEvent(wifi_callback);
        m_isConfiguring = false;

        uint32_t cfgTS = millis();
        while (0 != digitalRead(PIN_CONNECT))
        {
            g_statusLed.set(0, 0, 2);
        }
        if (!EEPROM.begin(sizeof(m_stored)))
        {
            Serial.println(F("EEPROM Error - could not mount."));
            while (true)
                ; // halt
        }
        memset(&m_stored, 0, sizeof(m_stored));
        g_statusLed.set(0, 0, 0);
        size_t size = EEPROM.readBytes(0, &m_stored, sizeof(m_stored));
        // we'll timeout here if the connect button was held down and always rewrite the flash
        if (5000 < millis() - cfgTS || size != sizeof(m_stored))
        {
            if (size != sizeof(m_stored))
            {
                Serial.println("EEPROM Error - Size mismatch");
            }
            Serial.println(F("EEPROM writing initial configuration"));
            // for sanity - safety
            memset(this, 0, sizeof(m_stored));
            // write the default zeroed config
            size = EEPROM.writeBytes(0, &m_stored, sizeof(m_stored));
            if (size != sizeof(m_stored) || !EEPROM.commit())
            {
                Serial.println(F("EEPROM Error - failure writing initial config"));
                while (true)
                    ; // halt
            }
            // this is so it doesn't later confuse the reset op with a connect op during update()
            delay(1000);
        }

        if (needsAdditionalConfig())
        {
            m_cfgStartTS = millis();
            m_isConfiguring = true;
            Serial.println(F("Configuring"));
            g_statusLed.set(0, 0, 1);
            if (!m_stored.flags.isCalibrated)
            {
                bool shouldStore = true;
                if (!recalibrate())
                {
                    shouldStore = false;
                    Serial.println(F("Recalibration failed"));
                    // TODO: Should this halt?
                }
                else
                    m_stored.flags.isCalibrated = 1;
                if (shouldStore)
                {
                    if ((sizeof(m_stored) != EEPROM.writeBytes(0, &m_stored, sizeof(m_stored)) || !EEPROM.commit()))
                    {
                        Serial.println(F("EEPROM error writing configuration"));
                    }
                    else
                    {
                        Serial.println(F("EEPROM configuration saved"));
                    }
                }
            }
            if (needsAdditionalConfig(true))
            {
                // initialize the bluetooth radio and start discovery
                char sz[80];
                snprintf(sz, 80, "Lung Trainer (%s)", displayName());
                NimBLEDevice::init(sz);
                m_isBTInitialized = true;
                NimBLEScan *pScan = NimBLEDevice::getScan();
                pScan->setAdvertisedDeviceCallbacks(new BleAdvertisedDeviceCallbacks());
                pScan->setInterval(45);
                pScan->setWindow(15);
                pScan->setActiveScan(true);
                pScan->start(0, ble_scan_ended_callback);
                Serial.println(F("BLE scanning for application"));
                if (0 != m_stored.flags.isWifiSet)
                {
                    // make sure our creds are actually *valid*
                    Serial.print(F("WiFi found stored credentials - connecting to "));
                    Serial.println(m_stored.ssid);
                    WiFi.begin(m_stored.ssid, m_stored.passkey);
                    uint32_t wifiTS = millis();
                    while (6000 > (millis() - wifiTS) && WL_CONNECTED != WiFi.status())
                        ;
                    if (WL_CONNECTED != WiFi.status())
                    {
                        m_stored.flags.isWifiSet = 0;
                        Serial.println(F("WiFi found stored credentials invalid - disregarding"));
                    }
                    else
                    {
                        Serial.println(F("WiFi connected"));
                    }
                }
                int ble = 0;
                int wifi = 0;
                bool hasMore = true;
                if (0 == m_stored.flags.isWifiSet)
                {
                    wifi = 2;
                    if (!needsAdditionalConfig(true))
                        hasMore = false;
                }
                else
                    wifi = needsAdditionalConfig(true);
                if (hasMore)
                {
                    ble = 2 * needsAdditionalConfig(true);
                }
                //Serial.printf("setLed(ble=%d,wifi=%d,1);\r\n",ble,wifi);
                g_statusLed.set(ble, wifi, 1);
                if (0 == m_stored.flags.isWifiSet)
                {
                    Serial.println(F("WiFi starting WPS scan"));
                    // begin WPS scan
                    WiFi.mode(WIFI_MODE_STA);
                    wps_init_config();
                    esp_wifi_wps_enable(&::g_wifi_wps_config);
                    esp_wifi_wps_start(0);
                }
            }
            else
            {
                //Serial.println("setLed(0,0,0);\r\n");
                m_isConfiguring = false;
                g_statusLed.set(0, 0, 0);
            }
        }
        else
        {
            //Serial.println("setLed(0,0,0);\r\n");
            m_isConfiguring = false;
            g_statusLed.set(0, 0, 0);
        }
    }
    bool needsAdditionalConfig(bool connectedOnly = false)
    {
        return !m_stored.flags.isNameSet || (!m_stored.flags.isCalibrated && !connectedOnly) || !m_stored.flags.isClockSet;
    }
    bool isConfiguring()
    {
        return m_isConfiguring;
    }
    bool isNameSet()
    {
        return m_stored.flags.isNameSet;
    }
    const char *name()
    {
        return m_stored.name;
    }
    const char *displayName()
    {
        if (!isNameSet())
        {
            return "New";
        }
        return name();
    }
    bool isWiFiSet()
    {
        return m_stored.flags.isWifiSet;
    }
    bool setWiFi(const char *ssid, const char *passkey)
    {
        strncpy(m_stored.ssid, ssid, 33);
        strncpy(m_stored.passkey, passkey, 64);
        m_stored.flags.isWifiSet = 1;
        return true;
    }
    const char *ssid()
    {
        return m_stored.ssid;
    }
    const char *passkey()
    {
        return m_stored.passkey;
    }
    void update()
    {
        if (m_isConfiguring)
        {
            if (m_isBTInitialized && nullptr != g_ble_adv_device)
            {
                WiFi.disconnect(true, true);
                g_statusLed.set(1, 0, 1);

                NimBLEAdvertisedDevice *advdev = g_ble_adv_device;
                g_ble_adv_device = nullptr;

                NimBLEClient *pClient = nullptr;

                if (NimBLEDevice::getClientListSize())
                {
                    pClient = NimBLEDevice::getClientByPeerAddress(advdev->getAddress());
                    if (pClient)
                    {
                        if (pClient->connect(advdev, false))
                        {
                            Serial.println(F("BLE Reconnected client"));
                        }
                        else
                        {
                            Serial.println(F("BLE reconnect failed"));
                        }
                    }
                    else
                    {
                        pClient = NimBLEDevice::getDisconnectedClient();
                    }
                }

                // No client to reuse? Create a new one.
                if (!pClient)
                {
                    while (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS)
                    {
                        Serial.println(F("BLE max clients reached - no more connections available. Waiting for free"));
                        delay(1000);
                    }

                    pClient = NimBLEDevice::createClient();

                    Serial.println(F("BLE new client created"));

                    pClient->setClientCallbacks(new ClientCallbacks, true);
                    /** Set initial connection parameters: These settings are 15ms interval, 0 latency, 120ms timout.
                     *  These settings are safe for 3 clients to connect reliably, can go faster if you have less
                     *  connections. Timeout should be a multiple of the interval, minimum is 100ms.
                     *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 51 * 10ms = 510ms timeout
                     */
                    pClient->setConnectionParams(12, 12, 0, 51);
                    // Set how long we are willing to wait for the connection to complete (seconds), default is 30.
                    pClient->setConnectTimeout(30);

                    if (!pClient->connect(advdev))
                    {
                        // Created a client but failed to connect, don't need to keep it as it has no data
                        NimBLEDevice::deleteClient(pClient);
                        Serial.println(F("BLE failed to connect, deleted client"));
                        return;
                    }
                }

                if (!pClient->isConnected())
                {
                    if (!pClient->connect(advdev))
                    {
                        Serial.println(F("BLE failed to connect"));
                        return;
                    }
                }

                Serial.print(F("BLE connected to: "));
                Serial.println(pClient->getPeerAddress().toString().c_str());
                Serial.print(F("BLE connection RSSI: "));
                Serial.println(pClient->getRssi());

                /** Now we can read/write/subscribe the charateristics of the services we are interested in */
                NimBLERemoteService *pSvc = nullptr;
                NimBLERemoteCharacteristic *pChr = nullptr;
                NimBLERemoteDescriptor *pDsc = nullptr;

                pSvc = pClient->getService(g_blecfgsvcid);
                if (pSvc)
                { /** make sure it's not null */
                    pChr = pSvc->getCharacteristic("BCF0");

                    if (pChr)
                    { /** make sure it's not null */
                        if (pChr->canRead())
                        {
                            Serial.print(F("BLE read from "));
                            Serial.print(pChr->getUUID().toString().c_str());
                            Serial.print(F(" Value: "));
                            Serial.println(pChr->readValue().c_str());
                        }

                        /** registerForNotify() has been deprecated and replaced with subscribe() / unsubscribe().
                         *  Subscribe parameter defaults are: notifications=true, notifyCallback=nullptr, response=false.
                         *  Unsubscribe parameter defaults are: response=false.
                         */
                        if (pChr->canNotify())
                        {
                            //if(!pChr->registerForNotify(notifyCB)) {
                            /*if(!pChr->subscribe(true, notifyCB)) {
                                // Disconnect if subscribe failed
                                pClient->disconnect();
                                return;
                            }*/
                        }
                    }
                }
                else
                {
                    Serial.println(F("BLE configuration service not found."));
                }
            }
            else if (0 != m_stored.flags.isWifiSet && WL_CONNECTED == WiFi.status())
            {
                m_isBTInitialized = false;
                NimBLEDevice::deinit(true);
                g_statusLed.set(0, 1, 1);
                //Serial.println(F("WiFi retrieving configuration"));
                bool shouldSave = false;
                if (!m_stored.flags.isNameSet)
                {
                    WiFiClient client;
                    Serial.println(F("WiFi fetching generated name"));
                    if (0 != client.connect(NAME_GEN_SVC_HOST, 80))
                    {
                        client.print(String("GET ") + NAME_GEN_SVC_PATH + " HTTP/1.1\r\n" +
                                     "Host: " + NAME_GEN_SVC_HOST + "\r\n" +
                                     "Connection: close\r\n\r\n");
                        unsigned long timeout = millis();
                        while (!client.available())
                        {
                            if (millis() - timeout > 5000)
                            {
                                Serial.println(F("WiFi name web service timeout"));
                                client.stop();
                                break;
                            }
                        }
                        if (client.connected())
                        {
                            Serial.println(F("WiFi connected to name service"));
                            while (client.available())
                            {
                                String line = client.readStringUntil('\r');
                                line.trim();
                                if (0 == line.length() && -1 != client.read() && client.available())
                                {

                                    ArduinoJson::StaticJsonDocument<2048> doc;
                                    deserializeJson(doc, client);
                                    const char *sz = doc[0].as<char *>();
                                    Serial.print(F("WiFi retrieved name: "));
                                    Serial.println(sz);
                                    m_stored.name[63] = 0;
                                    strncpy(m_stored.name, sz, 63);
                                    m_stored.flags.isNameSet = 1;
                                    shouldSave = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (!m_stored.flags.isClockSet)
                {
                    WiFiClient client;
                    Serial.println(F("WiFi fetching time"));
                    if (0 != client.connect(TIME_SVC_HOST, 80))
                    {
                        client.print(String("GET ") + TIME_SVC_PATH + " HTTP/1.1\r\n" +
                                     "Host: " + TIME_SVC_HOST + "\r\n" +
                                     "Connection: close\r\n\r\n");
                        unsigned long timeout = millis();
                        while (!client.available())
                        {
                            if (millis() - timeout > 5000)
                            {
                                Serial.println(F("WiFi time web service timeout"));
                                client.stop();
                                break;
                            }
                        }
                        if (client.connected())
                        {
                            Serial.println(F("WiFi connected to time service"));
                            while (client.available())
                            {
                                String line = client.readStringUntil('\r');
                                line.trim();
                                if (0 == line.length() && -1 != client.read() && client.available())
                                {
                                    ArduinoJson::StaticJsonDocument<2048> doc;
                                    deserializeJson(doc, client);
                                    uint32_t t = doc["unixtime"].as<long>();
                                    const char *sz = doc["datetime"].as<char *>();
                                    Serial.print(F("WiFi retrieved time: "));
                                    Serial.print(sz);
                                    Serial.print(F(" - "));
                                    Serial.println(t);
                                    // TODO: set the clock hardware
                                    m_stored.flags.isClockSet = 1;
                                    shouldSave = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (shouldSave)
                {
                    if (sizeof(m_stored) != EEPROM.writeBytes(0, &m_stored, sizeof(m_stored)) || !EEPROM.commit())
                    {
                        Serial.println(F("EEPROM error writing configuration"));
                    }
                    else
                    {

                        Serial.println(F("Configuration saved to EEPROM"));
                    }
                    if (needsAdditionalConfig(true))
                    {
                        //Serial.println("setLed(2,1,1);\r\n");
                        g_statusLed.set(2, 1, 1);
                    }
                    else
                    {
                        //Serial.println("setLed(0,0,0);\r\n");
                        g_statusLed.set(0, 0, 0);
                        m_isConfiguring = false;
                        if (m_isBTInitialized)
                        {
                            NimBLEDevice::deinit(true);
                            m_isBTInitialized = false;
                        }
                        WiFi.disconnect(true, true);
                    }
                }
                else
                {
                    if (!needsAdditionalConfig(true))
                    {
                        //Serial.println("setLed(0,0,0);\r\n");
                        g_statusLed.set(0, 0, 0);
                        m_isConfiguring = false;
                        if (m_isBTInitialized)
                        {
                            NimBLEDevice::deinit(true);
                            m_isBTInitialized = false;
                        }
                        WiFi.disconnect(true, true);
                    }
                }
            }
            if (needsAdditionalConfig(true) && !m_isBTInitialized)
            {
                char sz[80];
                snprintf(sz, 80, "Lung Trainer (%s)", displayName());
                NimBLEDevice::init(sz);
                m_isBTInitialized = true;
                NimBLEScan *pScan = NimBLEDevice::getScan();
                pScan->setAdvertisedDeviceCallbacks(new BleAdvertisedDeviceCallbacks());
                pScan->setInterval(45);
                pScan->setWindow(15);
                pScan->setActiveScan(true);
                pScan->start(0, ble_scan_ended_callback);
                Serial.println(F("BLE scanning for application"));
            }
            if (CONFIG_TIMEOUT < millis() - m_cfgStartTS)
            {
                g_statusLed.set(0, 0, 0);
                m_isConfiguring = false;
                if (m_isBTInitialized)
                {
                    NimBLEDevice::deinit(true);
                    m_isBTInitialized = false;
                }
                WiFi.disconnect(true, true);
            }
        }
    }
};
Configuration g_config;

static void wps_init_config()
{
    g_wifi_wps_config.crypto_funcs = &g_wifi_default_wps_crypto_funcs;
    g_wifi_wps_config.wps_type = ESP_WPS_MODE;
    strcpy(g_wifi_wps_config.factory_info.manufacturer, ESP_MANUFACTURER);
    strcpy(g_wifi_wps_config.factory_info.model_number, ESP_MODEL_NUMBER);
    strcpy(g_wifi_wps_config.factory_info.model_name, ESP_MODEL_NAME);
    strcpy(g_wifi_wps_config.factory_info.device_name, ESP_DEVICE_NAME);
}

static String wpspin2string(uint8_t a[])
{
    char wps_pin[9];
    for (int i = 0; i < 8; i++)
    {
        wps_pin[i] = a[i];
    }
    wps_pin[8] = '\0';
    return (String)wps_pin;
}
static void wifi_callback(WiFiEvent_t event, system_event_info_t info)
{
    switch (event)
    {
    case SYSTEM_EVENT_STA_START:
        Serial.println(F("WiFi station mode started"));
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.print(F("WiFi connected to: "));
        Serial.println(WiFi.SSID());
        if (!g_config.setWiFi(WiFi.SSID().c_str(), WiFi.psk().c_str()))
        {
            Serial.println(F("WiFi configuration error setting credentials"));
        }
        //Serial.print(F("WiFi Passkey: "));
        //Serial.println(WiFi.psk().c_str());
        Serial.print(F("WiFi got IP: "));
        Serial.println(WiFi.localIP());
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        if (!g_config.isWiFiSet())
        {
            Serial.println(F("WiFi disconnected from station, attempting reconnection"));
            WiFi.reconnect();
        }
        break;
    case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
        Serial.println(F("WiFi WPS successful, stopping WPS and connecting."));
        esp_wifi_wps_disable();
        delay(10);
        WiFi.begin();
        break;
    case SYSTEM_EVENT_STA_WPS_ER_FAILED:
        Serial.println(F("WiFi WPS failed, retrying"));
        esp_wifi_wps_disable();
        esp_wifi_wps_enable(&g_wifi_wps_config);
        esp_wifi_wps_start(0);
        break;
    case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
        Serial.println(F("WiFi WPS timeout, retrying"));
        esp_wifi_wps_disable();
        esp_wifi_wps_enable(&g_wifi_wps_config);
        esp_wifi_wps_start(0);
        break;
    case SYSTEM_EVENT_STA_WPS_ER_PIN:
        Serial.print(F("WiFi WPS_PIN = "));
        Serial.println(wpspin2string(info.sta_er_pin.pin_code));
        break;
    default:
        break;
    }
}
static void ble_scan_ended_callback(NimBLEScanResults results)
{
}
#endif