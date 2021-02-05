#ifndef LT_STATUS_LED_H
#define LT_STATUS_LED_H
#include "rgb_led.h"

#define PIN_LED_R 32
#define PIN_LED_G 33
#define PIN_LED_B 25

#define WIFI_COLOR 0x00FFFF7F
#define BLE_COLOR 0x003F3FFF
#define WIFI_BLE_COLOR 0x007FFF7F
#define CONFIG_COLOR 0x00FF7FFF
#define RESET_COLOR 0x00FF0000

class StatusLed {
    uint32_t m_pColors[8];
    RgbLed m_led;
    RgbLedCycle m_cycle;
public:
    void begin() {
        m_led.begin(PIN_LED_R,PIN_LED_G,PIN_LED_B);
        
    }
    void update() {
        m_cycle.update();
    }
    void set(int bluetooth, int wifi, int config) {
        if(2==config) {
            m_pColors[0]=RESET_COLOR;
            m_cycle.set(m_pColors,1,250);
            return;
        }
        uint32_t boff = 0;
        if(bluetooth==1) {
            if(wifi==1) {
                boff=WIFI_BLE_COLOR;
            } else 
                boff=BLE_COLOR;
        } else if(wifi==1) {
            boff=WIFI_COLOR;
        } 
        size_t cc = 0;
        if(0!=config) {
            m_pColors[cc++]=CONFIG_COLOR;
            m_pColors[cc++]=boff;
        }
        if(2==wifi) {
            m_pColors[cc++]=WIFI_COLOR;
            m_pColors[cc++]=boff;
        } 
        if(2==bluetooth) {
            m_pColors[cc++]=BLE_COLOR;
            m_pColors[cc++]=boff;
        }
        if(0==cc) {
            m_pColors[cc++]=boff;
        }
        m_cycle.set(m_pColors,cc,250);
    }
};
StatusLed g_statusLed;
#endif