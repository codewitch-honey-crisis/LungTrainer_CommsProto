#include <atomic>
#include <SPIFFS.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#define SUCCESS (char *)haystack
#define FAILURE (char *)NULL
#define UPNP_BROADCAST_INTERVAL 500
#define UPNP_LOCAL_PORT 23990
/*
M-SEARCH * HTTP/1.1
HOST: 239.255.255.250:1900
MAN: "ssdp:discover"
MX: 5
ST: ssdp:all
*/

class UpnpServer
{
    AsyncUDP m_udp;
    AsyncWebServer m_deviceServer;
    std::atomic_bool m_connected;
    uint32_t m_lastBroadcastTS;
    char m_responseBuffer[4096];
    // string replacement code from Brandin @ https://stackoverflow.com/questions/4408170/for-string-find-and-replace
    char *str_replace(char *haystack, size_t haystacksize,
                      const char *oldneedle, const char *newneedle)
    {
        size_t oldneedle_len = strlen(oldneedle);
        size_t newneedle_len = strlen(newneedle);
        char *oldneedle_ptr;         // locates occurences of oldneedle
        char *read_ptr;              // where to read in the haystack
        char *write_ptr;             // where to write in the haystack
        const char *oldneedle_last = // the last character in oldneedle
            oldneedle +
            oldneedle_len - 1;

        // Case 0: oldneedle is empty
        if (oldneedle_len == 0)
            return SUCCESS; // nothing to do; define as success

        // Case 1: newneedle is not longer than oldneedle
        if (newneedle_len <= oldneedle_len)
        {
            // Pass 1: Perform copy/replace using read_ptr and write_ptr
            for (oldneedle_ptr = (char *)oldneedle,
                read_ptr = haystack, write_ptr = haystack;
                 *read_ptr != '\0';
                 read_ptr++, write_ptr++)
            {
                *write_ptr = *read_ptr;
                bool found = locate_forward(&oldneedle_ptr, read_ptr,
                                            oldneedle, oldneedle_last);
                if (found)
                {
                    // then perform update
                    write_ptr -= oldneedle_len;
                    memcpy(write_ptr + 1, newneedle, newneedle_len);
                    write_ptr += newneedle_len;
                }
            }
            *write_ptr = '\0';
            return SUCCESS;
        }

        // Case 2: newneedle is longer than oldneedle
        else
        {
            size_t diff_len =   // the amount of extra space needed
                newneedle_len - // to replace oldneedle with newneedle
                oldneedle_len;  // in the expanded haystack

            // Pass 1: Perform forward scan, updating write_ptr along the way
            for (oldneedle_ptr = (char *)oldneedle,
                read_ptr = haystack, write_ptr = haystack;
                 *read_ptr != '\0';
                 read_ptr++, write_ptr++)
            {
                bool found = locate_forward(&oldneedle_ptr, read_ptr,
                                            oldneedle, oldneedle_last);
                if (found)
                {
                    // then advance write_ptr
                    write_ptr += diff_len;
                }
                if (write_ptr >= haystack + haystacksize)
                    return FAILURE; // no more room in haystack
            }

            // Pass 2: Walk backwards through haystack, performing copy/replace
            for (oldneedle_ptr = (char *)oldneedle_last;
                 write_ptr >= haystack;
                 write_ptr--, read_ptr--)
            {
                *write_ptr = *read_ptr;
                bool found = locate_backward(&oldneedle_ptr, read_ptr,
                                             oldneedle, oldneedle_last);
                if (found)
                {
                    // then perform replacement
                    write_ptr -= diff_len;
                    memcpy(write_ptr, newneedle, newneedle_len);
                }
            }
            return SUCCESS;
        }
    }

    // locate_forward: compare needle_ptr and read_ptr to see if a match occured
    // needle_ptr is updated as appropriate for the next call
    // return true if match occured, false otherwise
    static inline bool
    locate_forward(char **needle_ptr, char *read_ptr,
                   const char *needle, const char *needle_last)
    {
        if (**needle_ptr == *read_ptr)
        {
            (*needle_ptr)++;
            if (*needle_ptr > needle_last)
            {
                *needle_ptr = (char *)needle;
                return true;
            }
        }
        else
            *needle_ptr = (char *)needle;
        return false;
    }

    // locate_backward: compare needle_ptr and read_ptr to see if a match occured
    // needle_ptr is updated as appropriate for the next call
    // return true if match occured, false otherwise
    static inline bool
    locate_backward(char **needle_ptr, char *read_ptr,
                    const char *needle, const char *needle_last)
    {
        if (**needle_ptr == *read_ptr)
        {
            (*needle_ptr)--;
            if (*needle_ptr < needle)
            {
                *needle_ptr = (char *)needle_last;
                return true;
            }
        }
        else
            *needle_ptr = (char *)needle_last;
        return false;
    }
    static String templateProcessor(const String &var)
    {
        if (var == "IP") {
            return WiFi.localIP().toString();
        }
        else if (var == "UPC")
        {
            return "";
        } else if (var=="UUID") {
            return "2B8A8AC2-ABF2-4B87-92EA-4A6A182230D7";
        }
        else if (var == "NAME")
        {
            if(!g_config.isNameSet())
                return "New";
            return g_config.name();
        }
        return String();
    }

public:
    UpnpServer() : m_deviceServer(49152)
    {
    }
    bool begin()
    {
        if (!SPIFFS.begin(false))
        {
            Serial.println(F("UPnP unable to mount SPIFFS"));
            return false;
        }
        m_responseBuffer[0] = 0;
        m_lastBroadcastTS = 0;
        m_connected = false;
        return true;
    }

    void update()
    {
        //uint32_t ms = millis();
        if (WL_CONNECTED == WiFi.status())
        {
            if (!m_connected)
            {
                m_connected = true;
                m_deviceServer.serveStatic("/wps_device.xml", SPIFFS, "/upnpdevice.xml").setTemplateProcessor(templateProcessor);
                m_deviceServer.begin();
                if (m_udp.listenMulticast(IPAddress(239, 255, 255, 250), 1900, 5))
                {
                    Serial.println(F("UPnP multicast listener started."));
                    m_udp.onPacket([&](AsyncUDPPacket packet) {
                        if (packet.isMulticast())
                        {
                            Serial.println("UPnP parsing incoming multicast packet.");
                            int c = strlen("M-SEARCH * HTTP/1.1\r\n");
                            if (!strncasecmp(((char *)packet.data()), "M-SEARCH * HTTP/1.1\r\n", c))
                            {
                                Serial.println(F("UPnP received search request."));
                                File f = SPIFFS.open("/upnpresp.txt", "r");
                                if (!f)
                                {
                                    Serial.println(F("UPnP response data not present."));
                                }
                                else
                                {
                                    size_t s = f.readBytes(m_responseBuffer, sizeof(m_responseBuffer) - 1);
                                    m_responseBuffer[s] = 0;
                                    char dtbuf[512];
                                    time_t now = g_clock.now().unixtime();
                                    struct tm tm = *gmtime(&now);
                                    strftime(dtbuf, sizeof dtbuf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
                                    str_replace(m_responseBuffer, sizeof(m_responseBuffer), "%DATETIME%", dtbuf);
                                    str_replace(m_responseBuffer, sizeof(m_responseBuffer), "%IP%", WiFi.localIP().toString().c_str());
                                    Serial.print(F("UPnP sending response: "));
                                    Serial.println(m_responseBuffer);
                                    packet.print(m_responseBuffer);
                                }
                            }
                            else
                            {
                                Serial.println(F("UPnP multicast data invalid."));
                            }
                        }
                    });
                }
                else
                {
                    Serial.println(F("UPnP multicast listen failed."));
                }
            }
        }
        else
        {
            if (m_connected)
            {
                m_deviceServer.end();
                m_udp.close();
            }
            m_connected = false;
        }
        /*if(WL_CONNECTED==WiFi.status() && UPNP_BROADCAST_INTERVAL<ms-m_lastBroadcastTS) {
            m_lastBroadcastTS=ms;
            Serial.println(F("UPnP broadcasting device."));
            File f = SPIFFS.open("/upnpresp.txt","r");
            if(!f) {
                Serial.println(F("UPnP response data not present."));
            } else {
                size_t s = f.readBytes(m_responseBuffer,sizeof(m_responseBuffer)-1);
                m_responseBuffer[s]=0;
                char dtbuf[512];
                time_t now = time(0);
                struct tm tm = *gmtime(&now);
                strftime(dtbuf, sizeof dtbuf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
                str_replace(m_responseBuffer,sizeof(m_responseBuffer),"$DATETIME",dtbuf);
                str_replace(m_responseBuffer,sizeof(m_responseBuffer),"$IP",WiFi.localIP().toString().c_str());
                WiFiUDP udp;
                if(udp.beginMulticast(IPAddress(239,255,255,0),1900)) {
                    udp.print(m_responseBuffer);
                    udp.endPacket();
                } else {
                    Serial.println(F("UPnP multicast UDP transmission failed."));
                }
            }
        }*/
    }
};