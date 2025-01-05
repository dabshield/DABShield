/*
 * Hybrid Radio Project with RadioDNS functionality
 * DAB Radio with Colour LCD, and Hybrid Radio Functionality
 * AVIT Research Ltd
 *
 * RadioDNS.ino (Library)
 * Provides RadioDNS implmentation for logo download
 * v0.1 18/10/2022 - initial release
 * v0.2 07/11/2022 - minor bug fixes 
 * v0.3 15/11/2022 - minor bug fixes
 * v0.4 03/05/2023 - Updated for TFT_eSPI library
 *
 */
#include "RadioDNS.h"
#include "WiFi.h"

StaticJsonDocument<48> filter;
StaticJsonDocument<128> doc;
const char *Answer_0_data = doc["Answer"][0]["data"];  // "rdns.musicradio.com."

char buff[1024];
char *servicexml;

void RadioDNSsetup(const char *ssid, const char *password) {
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  //WiFi.persistent(true);

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500);
    Serial.print(".");
    StatusWiFi=0;
  }

  Serial.println("");
  Serial.println("WiFi connected");
  StatusWiFi=1;
  WifiStatusUpdate = true;
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

const char amptext[] = "&amp;";
char imageurl[128];
char mime[32];

uint16_t GetDABLogo(uint16_t ServiceID, uint16_t EnsembleID, uint16_t ECC) {
  char radioDNS[128];
  char bearer[128];

  sprintf(radioDNS, "0.%04x.%04x.%x.dab.radiodns.org", ServiceID, EnsembleID, ECC);
  sprintf(bearer, "dab:%x.%04x.%04x.0", ECC, EnsembleID, ServiceID);
  return GetLogo(radioDNS, bearer, ServiceID);
}

uint16_t GetFMLogo(uint16_t Freq, uint16_t ServiceID, uint16_t ECC) {
  char radioDNS[128];
  char bearer[128];

  sprintf(radioDNS, "%05d.%04x.%x.fm.radiodns.org", Freq, ServiceID, ECC);
  sprintf(bearer, "fm:%x.%04x.%05d", ECC, ServiceID, Freq);
  return GetLogo(radioDNS, bearer, ServiceID);
}

uint16_t GetLogo(char *service, char *bearer, uint32_t id) {
  uint16_t newlogo = 0;
  char cname[128];
  char srv[128];
  char filename[32];

  Serial.printf("radioDNS = %s\n", service);
  Serial.printf("bearer = %s\n", bearer);

  if (GetCNAME(cname, service) == 0)
    return 0;

  sched_yield();

  if (GetSRV(srv, cname) == 0)
    return 0;

  sched_yield();

  servicexml = (char *)malloc(16 * 1024);
  if (GetServiceInfo(servicexml, (16 * 1024), srv, bearer) == 0) {
    free(servicexml);
    return 0;
  }
  sched_yield();

  if (ParseXMLImageURL(imageurl, mime, servicexml) == 0) {
    free(servicexml);
    return 0;
  }

  free(servicexml);

  sched_yield();

  if (strlen(imageurl)) {
    //find the extension...
    char *ext = strrchr(imageurl, '.');
    if (!ext || ext == imageurl)
      return 0;

    if (strcmp(ext, ".png") || strcmp(ext, ".jpg")) {
      ext[4] = '\0';
      sprintf(filename, "/%04x%s", id, ext);
      Serial.printf("filename = %s\n", filename);

      if (GetImage(filename, imageurl) == 1)
        return id;
    }
  }

  return 0;
}

int GetCNAME(char *cname, const char *service) {
  char dns_string[128];

  WiFiClientSecure *client = new WiFiClientSecure;

  if (client) {
    client->setInsecure();
    // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is
    HTTPClient https;

    //Serial.print("[HTTPS] begin...\n");
    sprintf(dns_string, "https://dns.google/resolve?name=%s&type=CNAME", service);
    Serial.printf("[HTTPS] begin... %s\n", dns_string);

    if (https.begin(*client, dns_string)) {  // HTTPS
      // start connection and send HTTP header
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          String payload = https.getString();

          filter["Answer"][0]["data"] = true;
          DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

          if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            delete client;
            return 0;
          } else {
            if (doc["Answer"][0]["data"] == nullptr) {
              Serial.println("no data\n");
              https.end();
              delete client;
              return 0;
            }

            Serial.printf("%s", (const char *)doc["Answer"][0]["data"]);
            strcpy(cname, (const char *)doc["Answer"][0]["data"]);

            Serial.println("cname\n");

            if (strlen(cname) > 0) {

              if (cname[strlen(cname) - 1] == '.') {
                cname[strlen(cname) - 1] = '\0';
              }
              Serial.println(cname);
            } else {
              Serial.println("No CNAME\n");
              https.end();
              delete client;
              return 0;
            }
          }
        }
      } else {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        client->flush();
        client->stop();
        delete client;
        return 0;
      }
      https.end();
    } else {
      Serial.printf("[HTTPS] Unable to connect\n");
    }
    // End extra scoping block

    delete client;
  } else {
    Serial.println("Unable to create client");
  }
  return 1;
}

int GetSRV(char *srv, const char *cname) {
  char dns_string[128];

  WiFiClientSecure *client = new WiFiClientSecure;

  if (client) {
    client->setInsecure();
    // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is
    HTTPClient https;

    sprintf(dns_string, "https://dns.google/resolve?name=_radioepg._tcp.%s&type=SRV", cname);
    Serial.printf("[HTTPS] begin... %s\n", dns_string);

    if (https.begin(*client, dns_string)) {  // HTTPS
      // start connection and send HTTP header
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        //Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          String payload = https.getString();
          filter["Answer"][0]["data"] = true;
          DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

          if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            https.end();
            delete client;
            return 0;
          } else {
            if (doc["Answer"][0]["data"] == nullptr) {
              Serial.println("no data\n");
              https.end();
              delete client;
              return 0;
            }

            int service_priority;
            int service_weight;
            int service_port;

            sscanf((const char *)doc["Answer"][0]["data"], "%d %d %d %s", &service_priority, &service_weight, &service_port, srv);
            if (srv[strlen(srv) - 1] == '.') {
              srv[strlen(srv) - 1] = '\0';
            }
            Serial.println(srv);
          }
        }
      } else {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }
      https.end();
    } else {
      Serial.printf("[HTTPS] Unable to connect\n");
      delete client;
      return 0;
    }
    // End extra scoping block
    delete client;
  } else {
    Serial.println("Unable to create client");
    return 0;
  }
  return 1;
}

int GetServiceInfo(char *serviceinfo, uint16_t maxsize, const char *srv, const char *bearer) {

  char xmlurl[128];
  imageurl[0] = '\0';
  int found = 0;

  WiFiClientSecure *client = NULL;
  HTTPClient https;

  sprintf(xmlurl, "http://%s/radiodns/spi/3.1/SI.xml", srv);
  Serial.printf("[HTTPS] begin... %s\n", xmlurl);

  if (https.begin(xmlurl)) {  // HTTPS
    int i;
    const char *headerKeys[] = { "Content-Type", "Location" };
    const size_t numberOfHeaders = 2;
    https.collectHeaders(headerKeys, numberOfHeaders);
    int httpCode = https.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      //Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      if (httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String NewLocation = https.header("Location");
        Serial.printf("Code 301 (moved permanently): %s", NewLocation.c_str());
        https.end();
        client = new WiFiClientSecure;
        client->setInsecure();
        https.begin(*client, NewLocation);
        https.setTimeout(16000);
        httpCode = https.GET();
      }

      // file found at server
      if (httpCode == HTTP_CODE_OK)  // || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
      {
        int len = https.getSize();

        Serial.println(https.header("Content-Type"));
        Serial.printf("FileSize %d\n", len);

        WiFiClient *stream = https.getStreamPtr();

        while (https.connected() && (len > 0 || len == -1)) {
          size_t size = stream->available();
          if (size) {
            int c = stream->readBytes(buff, (size > (sizeof(buff) - 1)) ? (sizeof(buff) - 1) : size);
            if (len == -1)
              if (dechunk(buff, &c, buff, c))
                len = 0;

            buff[c] = '\0';
            if (!found) {
              if (ParseService(serviceinfo, maxsize, buff, bearer)) {
                //Perhaps we could end the stream...
                found = 1;
              }
            }
            if (len > 0)
              len -= c;
          }
          sched_yield();
        }
      } else {
        Serial.printf("[HTTP] GET2... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }

    https.end();

    if (client)
      delete client;
  } else {
    Serial.printf("[HTTP] Unable to connect\n");
  }
  return found;
}

int GetImage(const char *filename, const char *url) {

  int success = 0;

  WiFiClientSecure *client = NULL;
  HTTPClient https;

  if (strlen(url)) {
    //download image
    fs::File f = SPIFFS.open(filename, "w");
    Serial.printf("[HTTPS] begin... %s\n", url);

    bool http_rc;
    if (strncmp(url, "https://", 8) == 0) {
      client = new WiFiClientSecure;
      client->setInsecure();
      http_rc = https.begin(*client, url);
    } else {
      http_rc = https.begin(url);
    }

    if (http_rc) {
      int i;

      // Serial.print("[HTTP] GET...\n");
      // start connection and send HTTP header
      const char *headerKeys[] = { "Content-Type" };
      const size_t numberOfHeaders = 1;
      https.collectHeaders(headerKeys, numberOfHeaders);
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // file found at server
        if (httpCode == HTTP_CODE_OK) {
          int len = https.getSize();
          Serial.println(https.header("Content-Type"));
          Serial.printf("FileSize %d\n", len);

          WiFiClient *stream = https.getStreamPtr();
          int total = 0;

          while (https.connected() && (len > 0 || len == -1)) {
            size_t size = stream->available();
            if (size) {
              int c = stream->readBytes(buff, (size > sizeof(buff)) ? sizeof(buff) : size);
              if (len == -1)
                if (dechunk(buff, &c, buff, c))
                  len = 0;

              f.write((const uint8_t *)buff, c);
              total += c;
              if (len > 0)
                len -= c;
            }
            sched_yield();
          }
          if (total > 0)
            success = 1;
          Serial.printf("total %d\n", total);
        }
      }
      https.end();
    }
    f.close();
  }

  if (client)
    delete client;

  Serial.printf("GetImage %s\n", success ? "success" : "fail");
  return success;
}

//This is the chunk decoder...
bool dechunk(char *outbuff, int *outsize, const char *inbuff, int insize) {
  static int chunklen = 0;
  static int chunkparse = 0;

  bool finished = false;
  int outindex = 0;
  int nibble;
  int i;
  char hex[2];

  for (i = 0; i < insize; i++) {
    //printf("%c", buff[i]);
    switch (chunkparse) {
      case 0:  //len
        hex[0] = inbuff[i];
        hex[1] = '\0';
        if (sscanf(hex, "%x", &nibble) == 1) {
          chunklen <<= 4;
          chunklen |= nibble;
        } else {
          if (inbuff[i] == '\r')
            chunkparse = 2;
        }
        break;
      case 1:  //0x0d
      case 2:  //0x0a
        if (inbuff[i] == '\n') {
          chunkparse = 3;
          if (chunklen == 0) {
            chunkparse = 0;
            finished = true;
          }
        }
        break;
      case 3:  //data
        outbuff[outindex] = inbuff[i];
        outindex++;

        chunklen--;
        if (chunklen == 0)
          chunkparse = 4;
        break;

      case 4:
        if (inbuff[i] == '\r')
          chunkparse = 5;
        break;
      case 5:
        if (inbuff[i] == '\n')
          chunkparse = 0;
        break;
    }
  }
  *outsize = outindex;
  outbuff[outindex] = '\0';
  return finished;
}

bool ParseService(char *serviceinfo, uint16_t maxsize, const char *text, const char *bearer) {
  const char servicestart[] = "<service>";
  const char serviceend[] = "</service>";
  static int stringindex = 0;
  static int servicestate = 0;
  static int serviceindex = 0;

  int i;
  for (i = 0; i < strlen(text); i++) {
    switch (servicestate) {
      case 0:
        //find "<service>"" or "<service " but not <serviceInfo etc..
        if ((text[i] == servicestart[stringindex]) || ((servicestart[stringindex] == '>') && (text[i] == ' '))) {
          serviceinfo[serviceindex] = text[i];
          serviceindex++;
          stringindex++;
          if (stringindex == strlen(servicestart)) {
            //Serial.printf("stringindex = %d, %d, %c", stringindex, strlen(servicestart), text[i]);
            stringindex = 0;
            servicestate = 1;
          }
        } else {
          stringindex = 0;
          serviceindex = 0;
        }
        break;
      case 1:
        serviceinfo[serviceindex] = text[i];
        if (serviceindex < (maxsize - 1))
          serviceindex++;

        if (text[i] == serviceend[stringindex]) {
          stringindex++;
          if (stringindex == strlen(serviceend)) {
            serviceinfo[serviceindex] = '\0';
            stringindex = 0;
            serviceindex = 0;
            servicestate = 0;

            if (FindOurService(serviceinfo, bearer)) {
              //Serial.printf("%s\n",serviceinfo);
              return true;
            }
          }
        } else {
          stringindex = 0;
        }
        break;
    }
  }
  return false;
}

bool FindOurService(const char *text, const char *bearer) {
  if (strstr(text, bearer) != 0) {
    //this serivce xml contains the station we are looking for
    Serial.printf("Found it\n");
    return true;
  }
  return false;
}

int ParseXMLImageURL(char *url, char *mime, const char *xml) {
  using namespace tinyxml2;
  XMLDocument xmlDocument;
  if (xmlDocument.Parse(xml) != tinyxml2::XML_SUCCESS) {
    Serial.println("Error parsing");
    return 0;
  }

  XMLNode *service = xmlDocument.FirstChild();
  XMLElement *mediaDescription = service->FirstChildElement("mediaDescription");
  bool found = true;
  while (found) {
    if (mediaDescription) {
      XMLElement *multimedia = mediaDescription->FirstChildElement("multimedia");
      if (multimedia) {
        int height;
        int width;
        multimedia->QueryAttribute("height", &height);
        multimedia->QueryAttribute("width", &width);
        if ((height == 128) && (width == 128)) {
          url[0] = '\0';
          mime[0] = '\0';
          if (multimedia->Attribute("url"))
            strcpy(url, multimedia->Attribute("url"));
          if (multimedia->Attribute("mimeValue"))
            strcpy(mime, multimedia->Attribute("mimeValue"));
          Serial.printf("Found (%dx%d), %s type:%s\n", height, width, url, mime);
          return 1;
        }
      }
      mediaDescription = mediaDescription->NextSiblingElement("mediaDescription");
    } else {
      found = false;
    }
  }
  return 0;
}