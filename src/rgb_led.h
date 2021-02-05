#include <Arduino.h>
class RgbLed {
public:
  void begin(uint8_t pinR, uint8_t pinG, uint8_t pinB) {
    ledcSetup(0,5000,8);
    ledcSetup(1,5000,8);
    ledcSetup(2,5000,8);
    ledcAttachPin(pinR,0);
    ledcAttachPin(pinG,1);
    ledcAttachPin(pinB,2);
  }
  void color(uint8_t r,uint8_t g, uint8_t b) {
    r =(uint8_t)(r *.63 +.5); // compensate for voltage differential with red.
    g =(uint8_t)(g* .95+.5); // compensate for voltage differential with green.
    ledcWrite(0,r);
    ledcWrite(1,g);
    ledcWrite(2,b);
  }
  void color(uint32_t argb) {
    float a = (uint8_t)(255-((argb & 0xFF000000)/0x1000000))/255.0;
    color((uint8_t)(((argb & 0xFF0000)/0x10000)*a+.5),(uint8_t)(((argb & 0xFF00) / 0x100)*a+.5),(uint8_t)((argb & 0xFF)*a+.5));
  }
};

class RgbLedCycle {
  RgbLed* m_pLed;
  uint32_t *m_pColors;
  size_t m_colorCount;
  size_t m_colorIndex;
  uint32_t m_millis;
public:
  void begin(RgbLed* pLed, uint32_t* pColors,size_t colorCount,uint32_t interval) {
    m_pLed = pLed;
    m_pColors = pColors;
    m_colorCount = colorCount;
    m_colorIndex=0;
    this->interval = interval;
    if(0==colorCount)
      return;
    m_pLed->color(pColors[0]);
  }
  void set(uint32_t* pColors,size_t colorCount,uint32_t interval) {
    m_pColors = pColors;
    m_colorCount = colorCount;
    m_colorIndex=0;
    this->interval = interval;
    if(0==colorCount)
      return;
    m_pLed->color(pColors[0]);
  }
  
  uint32_t interval;

  void update() {
    if(0==m_colorCount || 0==interval) return;
    uint32_t ms = millis();
    if(ms-m_millis>interval) {
      m_millis=ms;
      m_colorIndex = (m_colorIndex+1)%m_colorCount;
      m_pLed->color(m_pColors[m_colorIndex]);
    }
  }
  // RgbLed *pLed() { return m_pLed; }
  
};