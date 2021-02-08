#include <atomic>
#include <EEPROM.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <esp_wps.h>
#include <esp_wifi.h>
#include <WiFiClient.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>
#include "configuration.h"
#include "upnp.h"
#include "status_led.h"
#define CONNECT_TIMEOUT 30000
class Connection {

    
    UpnpServer m_upnpServer;
    NimBLEServer *m_pbleServer;
    NimBLEService *m_pbleLTService;
    bool m_oldConnect;
    bool m_connect;
    struct {
        int type : 2; // 0 = none, 1=wifi, 2=ble
        char address[256];
    } m_pair;
    std::atomic_bool m_pairing;
    uint32_t m_pairTS;
    /**  None of these are required as they will be handled by the library with defaults. **
     **                       Remove as you see fit for your needs                        */  
    class ServerCallbacks: public NimBLEServerCallbacks {
        void onConnect(NimBLEServer* server) {
            Serial.println("Client connected");
            Serial.println("Multi-connect support: start advertising");
            NimBLEDevice::startAdvertising();
        };
        /** Alternative onConnect() method to extract details of the connection. 
         *  See: src/ble_gap.h for the details of the ble_gap_conn_desc struct.
         */  
        void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) {
            Serial.print("Client address: ");
            Serial.println(NimBLEAddress(desc->peer_ota_addr).toString().c_str());
            /** We can use the connection handle here to ask for different connection parameters.
             *  Args: connection handle, min connection interval, max connection interval
             *  latency, supervision timeout.
             *  Units; Min/Max Intervals: 1.25 millisecond increments.
             *  Latency: number of intervals allowed to skip.
             *  Timeout: 10 millisecond increments, try for 5x interval time for best results.  
             */
            server->updateConnParams(desc->conn_handle, 80, 80, 0, 100);
        };
        void onDisconnect(NimBLEServer* server) {
            Serial.println("Client disconnected - start advertising");
            NimBLEDevice::startAdvertising();
        };
        
    /********************* Security handled here **********************
    ****** Note: these are the same return values as defaults ********/
        uint32_t onPassKeyRequest(){
            Serial.println("Server Passkey Request");
            /** This should return a random 6 digit number for security 
             *  or make your own static passkey as done here.
             */
            return 0; 
        };

        bool onConfirmPIN(uint32_t pass_key){
            Serial.print("The passkey YES/NO number: ");
            Serial.println(pass_key);
            /** Return false if passkeys don't match. */
            return true; 
        };

        void onAuthenticationComplete(ble_gap_conn_desc* desc){
            /** Check that encryption was successful, if not we disconnect the client */  
            if(!desc->sec_state.encrypted) {
                NimBLEDevice::getServer()->disconnect(desc->conn_handle);
                Serial.println("Encrypt connection failed - disconnecting client");
                return;
            }
            Serial.println("Starting BLE work");
        };
    };
public:
    bool begin() {
        m_pair.address[0]=0;
        m_pairing = false;
        m_oldConnect=false;
        m_connect=false;
        m_pairTS = 0;
        
        pinMode(PIN_CONNECT, INPUT);

        if(!m_upnpServer.begin()) {
            Serial.println("Unable to start UPNP publishing service");
            return false;
        }
        return true;
    }
    void update() {
        m_connect = HIGH==digitalRead(PIN_CONNECT);
        if(!g_config.isConfiguring()) {
            bool initconnreq = (false==m_oldConnect && true==m_connect);
            m_oldConnect = m_connect;
            if(initconnreq) {
                if(0==m_pair.type && !m_pairing) {
                    Serial.println(F("Pairing initiated"));
                    m_pairTS=millis();
                    g_statusLed.set(2,2*g_config.isWiFiSet(),g_config.needsAdditionalConfig(true)); 
                    m_pairing=true;
                    char sz[128];
                    sz[0]=0;
                    if(g_config.isNameSet()) {
                        sprintf(sz,"Lung Trainer (%s)",g_config.name());
                    } else {
                        strcpy(sz,"Lung Trainer (New)");
                    }
                    Serial.println(F("BLE starting radio"));
                    NimBLEDevice::init(sz);
                    /** Optional: set the transmit power, default is 3db */
                    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
                    NimBLEDevice::setSecurityAuth(/*BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM |*/ BLE_SM_PAIR_AUTHREQ_SC);
                    m_pbleServer = NimBLEDevice::createServer();
                    m_pbleServer->setCallbacks(new ServerCallbacks());
                    m_pbleLTService = m_pbleServer->createService(g_blesvcid);
                    if(!m_pbleLTService->start()) {
                        Serial.println(F("BLE error starting Lung Trainer service."));
                    } else
                    {
                        Serial.println(F("BLE started Lung Trainer service."));
                    }
                    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
                    pAdvertising->setAppearance(832u);
                    pAdvertising->addServiceUUID(g_blesvcid);
                    pAdvertising->setScanResponse(true);
                    
                    if(!pAdvertising->start()) {
                        Serial.println(F("BLE error advertising Lung Trainer service."));
                    } else {
                        Serial.println(F("BLE advertising Lung Trainer service."));
                    }
                    if(g_config.isWiFiSet()) {
                        Serial.println(F("WiFi starting radio"));
                        WiFi.mode(WIFI_MODE_STA);
                        Serial.print(F("WiFi SSID "));
                        Serial.println(g_config.ssid());
                        Serial.print(F("WiFi passkey "));
                        Serial.println(g_config.passkey());
                        WiFi.begin(g_config.ssid(),g_config.passkey());//,0,nullptr,true);
                        
                            esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
                        //WiFi.begin();
                        
                    }

                }
            }
            if(m_pairing) {
                m_upnpServer.update();

                if(CONNECT_TIMEOUT<millis()-m_pairTS) {
                    // timeout
                    m_pairTS = 0;
                    m_pair.type=0;
                    m_pairing=false;
                    Serial.println(F("Pairing timeout"));
                    g_statusLed.set(0,0,0);
                    // ensure the radio is off
                    //WiFi.mode(WIFI_MODE_NULL);
                    //esp_wifi_stop();
                    
                    WiFi.disconnect();//true,true);
                    if(g_bleIsInitialized) {
                        NimBLEDevice::deinit(true);
                        g_bleIsInitialized = false;
                    }
                }
            }
            
        }
        m_oldConnect = m_connect;
    }

};
Connection g_connection;