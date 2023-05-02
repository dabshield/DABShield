/*
 * Hybrid Radio Project with RadioDNS functionality
 * DAB Radio with Colour LCD, and Hybrid Radio Functionality
 * AVIT Research Ltd
 *
 * Platform:
 *    WeMos D1 R32 ESP32 
 *    DABShield
 *    480x320 Display e.g. DFROBOT DFR0669 / RowlandTechnology
 *
 * Required Libraries:
 *  o DAB Shield
 *  o TFT_eSPI
 *  o FT6236/TAMC_GT911/
 *  o ArduinoJson
 *  o TinyXML2
 *  o SPIFFS
 *  o PNGDec
 *
 * HybridRadio.ino
 * Main Code File
 * v0.1 18/10/2022 - initial release
 * v0.2 07/11/2022 - minor bug fixes
 * v0.3 15/11/2022 - minor bug fixes
 * v0.4 03/05/2023 - Updated for TFT_eSPI library
 *
 */

#define DFROBOT_DISPLAY
//#define RESISTIVE_TOUCH
//#define ROWLAND_DISPLAY

const char *ssid = "WIFI_SSID";
const char *password = "WIFI_PASSWORD";

#include <DABShield.h>
#include "Icons.h"

#include "SPI.h"
#include <TFT_eSPI.h>
#include "ComfortaaBold9pt7b.h"

#if defined ROWLAND_DISPLAY
#include <FT6236.h>
#elif defined DFROBOT_DISPLAY
#include "TAMC_GT911.h"
#elif defined RESISTIVE_TOUCH
#include <XPT2046_Touchscreen.h>
#endif

#include <pthread.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_pthread.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>
#include <tinyxml2.h>
#include "FS.h"
#include <SPIFFS.h>
#include <PNGdec.h>

#include "RadioDNS.h"

//Ensure User_Setup.h in TFT_eSPI library has these settings...
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   5  // Chip select control pin
#define TFT_DC   16  // Data Command control pin
#define TFT_RST  13  // Reset pin (could connect to RST pin)

//Pin Configs
#define I2C_SCL 22
#define I2C_SDA 21
#define TS_RESET 13
#define DAB_CS 12

#ifdef RESISTIVE_TOUCH
#define TS_CS 25
#endif


#define RGB565(x) (((x >> 8) & 0xF800) | ((x >> 5) & 0x07E0) | ((x >> 3) & 0x001F))

#define COLOR_RGB565_WHITE RGB565(0xffffff)
#define COLOR_RGB565_BLACK RGB565(0x000000)

//https://imagecolorpicker.com/color-code/00BDD2
#define COLOR_BACKGROUND RGB565(0x005f69)     //50% shade
#define COLOR_ENSEMBLE_TEXT RGB565(0x4dd1e0)  //80% shade
#define COLOR_SERVICE_TEXT RGB565(0x4dd1e0)   //30% tint

//https://imagecolorpicker.com/color-code/86DD25
#define COLOR_DATE_TIME RGB565(0xaae766)  //30% tint

#define COLOR_RADIO_TEXT RGB565(0xffffff)  //white

#define JUSTIFIED_LEFT 0
#define JUSTIFIED_RIGHT 1
#define JUSTIFIED_CENTER 2

#define BUTTON_BOOT_RESCAN 0
#define BUTTON_DAB_PREVIOUS 10
#define BUTTON_DAB_NEXT 11
#define BUTTON_DAB_VOL_DOWN 12
#define BUTTON_DAB_VOL_UP 13
#define BUTTON_DAB_LOGO 14

typedef enum DisplayScreen_ {
  DISPLAY_SCREEN_BOOT = 0,
  DISPLAY_SCREEN_SETTINGS,
  DISPLAY_SCREEN_DAB,
  DSIPLAY_SCREEN_FM
} DisplayScreen_t;

typedef struct _touchpoint {
  uint16_t x;
  uint16_t y;
  uint16_t z;
} TouchPoint;
pthread_t RadioDNSThread;

DAB Dab;
PNG png;

TFT_eSPI tft = TFT_eSPI();

#if defined ROWLAND_DISPLAY
FT6236 ts = FT6236();
#elif defined DFROBOT_DISPLAY
TAMC_GT911 ts = TAMC_GT911(I2C_SDA, I2C_SCL, NULL, TS_RESET, 480, 320);
#elif defined RESISTIVE_TOUCH
XPT2046_Touchscreen ts(TS_CS);
#endif

//Maxium number of services per ensembles
#define MAX_SERVICES_PER_ENSEMBLE 24
typedef struct _DABEnsembles {
  uint8_t freq_index;
  uint32_t EnsembleID;
  uint16_t ECC;
  char Ensemble[17];
  uint8_t numberofservices;
  DABService service[MAX_SERVICES_PER_ENSEMBLE];
} DABEnsembles;

//Maximum number of ensembles to be stored.
#define MAX_ENSEMBLES 16
DABEnsembles Ensemble[MAX_ENSEMBLES];
uint8_t NumOfEnsembles;
bool WifiStatusUpdate = false;
uint8_t StatusWiFi = 0;

uint8_t vol = 32;
uint8_t service = 0;
uint8_t ensemble = 0;
uint8_t freq = 0;

char service_text[DAB_MAX_SERVICEDATA_LEN];
uint8_t text_index;
uint16_t TimeTimer = 1000;
uint8_t lastMinutes = 99;
uint16_t seek_timer = 0;
uint16_t save_settings_timer = 0;

int8_t ActiveButton = -1;
uint16_t RepeatButtonTimer = 0;
uint8_t touch_timer = 0;
uint8_t logo_update = 0;

bool dab_mode = true;

uint16_t StateTimer = 0;
DisplayScreen_t display_screen = DISPLAY_SCREEN_BOOT;

void UIloop();
void ServiceData(void);
void Display_Boot_Screen(void);
void Display_DAB_Screen(void);
void displayText(int16_t x, int16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size, uint8_t justified);

uint8_t DAB_scan(void);
void DAB_NextService(void);
void DAB_PreviousService(void);
void DAB_VolUp(void);
void DAB_VolDown(void);

uint16_t UpdateLogo = 0;

uint16_t GetLogoServiceID = 0;
uint16_t GetLogoEnsembleID = 0;
uint16_t GetLogoFreq = 0;
uint16_t GetLogoGCC = 0;
bool GetLogoSignal = false;
bool GettingLogo = false;

void *RadioDNSProcess(void *p) {
  Serial.println("RadioDNSsetup Thread");
  RadioDNSsetup(ssid, password);
  while (1) {
    if (GetLogoSignal == true) {
      GetLogoSignal = false;
      GettingLogo = true;
      UpdateLogo = 0;
      if (dab_mode)
        UpdateLogo = GetDABLogo(GetLogoServiceID, GetLogoEnsembleID, GetLogoGCC);
      else
        UpdateLogo = GetFMLogo(GetLogoFreq, GetLogoServiceID, GetLogoGCC);
      GettingLogo = false;
    }
    vTaskDelay(1);
  }
}

void *UIProcess(void *p) {
  Serial.println("UIProcess Thread");
  while (1) {
    UIloop();
    vTaskDelay(1);
  }
}

void setup() {

  Serial.begin(115200);
  delay(10);

  //Start the File system
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
  }

// Reformat all the File System by changing to #if 1 and running.
// Must be put back to #if 0 otherwise will reformat ever boot.
#if 0
  SPIFFS.format();
#endif

  //DAB Setup
  pinMode(DAB_CS, OUTPUT);
  digitalWrite(DAB_CS, HIGH);
  SPI.begin();

  Dab.setCallback(ServiceData);
  Dab.begin();

  //FT6236 reset pin - drive high
  pinMode(TS_RESET,OUTPUT);
  digitalWrite(TS_RESET,HIGH);

#if defined ROWLAND_DISPLAY
  ts.begin(40, I2C_SDA, I2C_SCL);
#elif defined DFROBOT_DISPLAY
  ts.begin();
  ts.setRotation(0);
#elif defined RESISTIVE_TOUCH
  ts.begin();
  ts.setRotation(1); 
#endif

  tft.init();
  tft.setSwapBytes(true);
  tft.setRotation(3);

  Display_Boot_Screen();

  esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
  cfg.stack_size = (16 * 1024);
  Serial.printf("RadioDNSProcess priority = %d\n", cfg.prio);
  cfg.prio += 0;
  esp_pthread_set_cfg(&cfg);
  pthread_create(&RadioDNSThread, NULL, RadioDNSProcess, (void *)NULL);

  cfg = esp_pthread_get_default_config();
  cfg.stack_size = (16 * 1024);
  Serial.printf("UIProcess priority = %d\n", cfg.prio);
  cfg.prio += 2;
  esp_pthread_set_cfg(&cfg);
  pthread_create(&RadioDNSThread, NULL, UIProcess, (void *)NULL);
}

void Display_Boot_Screen(void) {
  display_screen = DISPLAY_SCREEN_BOOT;
  tft.fillScreen(COLOR_RGB565_WHITE);

  tft.pushImage(/*x=*/0, /*y=*/0,  /*w=*/480, /*h=*/100, /*bitmap gImage_Bitmap=*/(const uint16_t *)splash);

  tft.setFreeFont(&FreeSans9pt7b);

  displayText(240, 120, "AVIT Hybrid Radio", COLOR_BACKGROUND, COLOR_RGB565_WHITE, 1, JUSTIFIED_CENTER);
  displayText(240, 140, "v1.0.4 2023", COLOR_BACKGROUND, COLOR_RGB565_WHITE, 1, JUSTIFIED_CENTER);

  //rescan...
  tft.pushImage(/*x=*/396, /*y=*/246, /*w=*/64, /*h=*/64,  /*bitmap gImage_Bitmap=*/(const uint16_t *)icon_rescan);

  ensemble = 0;
  service = 0;
}

void Display_DAB_Screen(void) {
  display_screen = DISPLAY_SCREEN_DAB;
  tft.fillScreen(COLOR_BACKGROUND);
  tft.fillRect(20, 20, 128, 128, COLOR_RGB565_BLACK);
  tft.fillRect(0, 230, 480, 100, COLOR_RGB565_WHITE);
  tft.pushImage(/*x=*/0, /*y=*/226, /*w=*/480, /*h=*/8, /*bitmap gImage_Bitmap=*/(const uint16_t *)bar);
  tft.pushImage(/*x=*/20, /*y=*/246, /*w=*/64, /*h=*/64, /*bitmap gImage_Bitmap=*/(const uint16_t *)icon_previous);
  tft.pushImage(/*x=*/100, /*y=*/246, /*w=*/64, /*h=*/64, /*bitmap gImage_Bitmap=*/(const uint16_t *)icon_next);
  tft.pushImage(/*x=*/316, /*y=*/246, /*w=*/64, /*h=*/64, /*bitmap gImage_Bitmap=*/(const uint16_t *)icon_voldown);
  tft.pushImage(/*x=*/396, /*y=*/246, /*w=*/64, /*h=*/64, /*bitmap gImage_Bitmap=*/(const uint16_t *)icon_volup);
  tft.pushImage(/*x=*/176, /*y=*/242, /*w=*/128, /*h=*/71, /*bitmap gImage_Bitmap=*/(const uint16_t *)RadioDNSLogo);

  //Date
  displayText(480 - 20, 20, "--/--/----", COLOR_DATE_TIME, COLOR_BACKGROUND, 1, JUSTIFIED_RIGHT);
  //Time
  displayText(480 - 20, 40, "--:--", COLOR_DATE_TIME, COLOR_BACKGROUND, 1, JUSTIFIED_RIGHT);

  WifiStatusUpdate = false;
  if (StatusWiFi == 1){
    displayText(480 - 20, 60, "WiFi Connected", COLOR_DATE_TIME, COLOR_BACKGROUND, 1, JUSTIFIED_RIGHT);
  }
  else {
    displayText(480 - 20, 60, "No WiFi", COLOR_DATE_TIME, COLOR_BACKGROUND, 1, JUSTIFIED_RIGHT); 
  }
  
  //Ensemble
  displayText(160, 20, Ensemble[ensemble].Ensemble, COLOR_ENSEMBLE_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
  //Service
  displayText(160, 40, Ensemble[ensemble].service[service].Label, COLOR_SERVICE_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
}

void displayText(int16_t x, int16_t y, char *c, uint16_t color, uint16_t bg, uint8_t size, uint8_t justified) {
  int16_t x1, y1;
  uint16_t w, h;

  tft.setTextSize(size);
  tft.setTextColor(color, bg, true);
  tft.setFreeFont(&Comfortaa_Bold9pt7b);

  if (justified == JUSTIFIED_LEFT) {
    tft.setCursor(x, y + 13);
  } else if (justified == JUSTIFIED_RIGHT) {
    w = tft.textWidth(c);
    tft.setCursor(x - w, y + 13);
  } else {  // JUSTIFIED_CENTER
    w = tft.textWidth(c);
    tft.setCursor(x - (w / 2), y + 13);
  }
  tft.print(c);
}

bool DABFileRead() {
  bool rc = false;
  int e;
  int s;

  DynamicJsonDocument doc(32 * 1048);

  fs::File myfile = SPIFFS.open("/DAB.json", "r");
  if (myfile) {
    Serial.printf("myfile open");
    DeserializationError error = deserializeJson(doc, myfile);
    if (error == DeserializationError::Ok) {
      dab_mode = doc["mode"];
      vol = doc["vol"];

      JsonObject DAB = doc["DAB"];

      ensemble = DAB["ensemble"];
      service = DAB["service"];
      NumOfEnsembles = DAB["NumOfEnsembles"];

      Serial.printf("NumOfEnsembles %d\n", NumOfEnsembles);
      //JsonArray DAB_EnsembleArray = DAB["Ensemble"];
      //for(e=0; e<NumOfEnsembles; e++)
      e = 0;
      for (JsonObject DAB_Ensemble : DAB["Ensemble"].as<JsonArray>()) {
        //JsonObject DAB_Ensemble = DAB_EnsembleArray[e];
        Ensemble[e].freq_index = DAB_Ensemble["freq"];
        strcpy(Ensemble[e].Ensemble, DAB_Ensemble["name"]);
        Ensemble[e].EnsembleID = DAB_Ensemble["ensembleID"];
        Ensemble[e].ECC = DAB_Ensemble["ECC"];
        Ensemble[e].numberofservices = DAB_Ensemble["numberofservices"];

        Serial.printf("\nEnsemble %s\n", Ensemble[e].Ensemble);
        //JsonArray DAB_ServiceArray = DAB_Ensemble["Service"];
        //for(s=0; s<Ensemble[e].numberofservices; s++)
        s = 0;
        for (JsonObject DAB_Service : DAB_Ensemble["Service"].as<JsonArray>()) {
          //JsonObject DAB_Service = DAB_ServiceArray[s];
          strcpy(Ensemble[e].service[s].Label, DAB_Service["name"]);
          Serial.printf("Service %s", Ensemble[e].service[s].Label);
          Ensemble[e].service[s].ServiceID = DAB_Service["serviceID"];
          Ensemble[e].service[s].CompID = DAB_Service["compID"];
          s++;
        }
        vTaskDelay(1);
        e++;
      }
      rc = true;
    } else {
      Serial.printf("deserialize Failed");
      Serial.println(error.f_str());
    }
    myfile.close();
    Serial.printf("file close");
  }

  Serial.printf("Return");
  return rc;
}

void DABFileWrite() {
  //Cal
  int e;
  int s;
  DynamicJsonDocument doc(32 * 1048);

  doc["mode"] = dab_mode;
  doc["vol"] = vol;

  JsonObject DAB = doc.createNestedObject("DAB");
  DAB["ensemble"] = ensemble;
  DAB["service"] = service;

  DAB["NumOfEnsembles"] = NumOfEnsembles;
  JsonArray DAB_Ensemble = DAB.createNestedArray("Ensemble");
  for (e = 0; e < NumOfEnsembles; e++) {
    JsonObject DAB_Ensemble_e = DAB_Ensemble.createNestedObject();
    DAB_Ensemble_e["freq"] = Ensemble[e].freq_index;
    DAB_Ensemble_e["name"] = Ensemble[e].Ensemble;
    DAB_Ensemble_e["ensembleID"] = Ensemble[e].EnsembleID;
    DAB_Ensemble_e["ECC"] = Ensemble[e].ECC;
    DAB_Ensemble_e["numberofservices"] = Ensemble[e].numberofservices;
    JsonArray DAB_Ensemble_e_Service = DAB_Ensemble_e.createNestedArray("Service");
    for (s = 0; s < Ensemble[e].numberofservices; s++) {
      JsonObject DAB_Ensemble_e_Service_s = DAB_Ensemble_e_Service.createNestedObject();
      DAB_Ensemble_e_Service_s["name"] = Ensemble[e].service[s].Label;
      DAB_Ensemble_e_Service_s["serviceID"] = Ensemble[e].service[s].ServiceID;
      DAB_Ensemble_e_Service_s["compID"] = Ensemble[e].service[s].CompID;
    }
  }

  //Write the Settings to Json file.
  fs::File myfile = SPIFFS.open("/DAB.json", "w");
  serializeJson(doc, myfile);
  myfile.close();

  myfile = SPIFFS.open("/DAB.json", "r");
  while (myfile.available()) {
    Serial.write(myfile.read());
    vTaskDelay(1);
  }
  myfile.close();

}

uint8_t DAB_scan(void) {
  uint8_t freq_index;
  uint8_t ensemble_index;
  uint8_t numofservices;
  uint8_t totalservcies;
  uint8_t i;

  dab_mode = true;
  ensemble_index = 0;
  totalservcies = 0;
  for (freq_index = 0; freq_index < DAB_FREQS; freq_index++) {
    char scanning[64];

    tft.fillRect(160, 180, 80, 40, COLOR_RGB565_WHITE);
    sprintf(scanning, "%d/37", freq_index);
    displayText(160, 180, scanning, COLOR_BACKGROUND, COLOR_RGB565_WHITE, 1, JUSTIFIED_LEFT);

    Dab.tune(freq_index);
    if (Dab.servicevalid() == true) {     
      //Copy Services into Array...
      Ensemble[ensemble_index].freq_index = freq_index;
      Ensemble[ensemble_index].EnsembleID = Dab.EnsembleID;
      Ensemble[ensemble_index].ECC = Dab.ECC;
      strcpy(Ensemble[ensemble_index].Ensemble, Dab.Ensemble);

      tft.setTextFont(0);
      tft.setTextColor(COLOR_RGB565_BLACK);
      tft.setTextSize(0);
      if (ensemble_index == 0)
      {
        tft.drawString("Found:", 20, 210);
      }
      sprintf(scanning, "%s (%d)", Dab.Ensemble, Dab.numberofservices);
      tft.drawString(scanning, 20, 220 + (ensemble_index * 10));

      //ensure we don't overflow our array.
      numofservices = Dab.numberofservices;
      if (numofservices > MAX_SERVICES_PER_ENSEMBLE) {
        numofservices = MAX_SERVICES_PER_ENSEMBLE;
      }
      Ensemble[ensemble_index].numberofservices = numofservices;
      for (i = 0; i < numofservices; i++) {
        Ensemble[ensemble_index].service[i].ServiceID = Dab.service[i].ServiceID;
        Ensemble[ensemble_index].service[i].CompID = Dab.service[i].CompID;
        strcpy(Ensemble[ensemble_index].service[i].Label, Dab.service[i].Label);
      }
      totalservcies += numofservices;
      ensemble_index++;
      
      tft.fillRect(240, 180, 240, 40, COLOR_RGB565_WHITE);
      sprintf(scanning, "found: %d", totalservcies);
      displayText(240, 180, scanning, COLOR_BACKGROUND, COLOR_RGB565_WHITE, 1, JUSTIFIED_LEFT);

      //Limit to our array size
      if (ensemble_index == MAX_ENSEMBLES) {
        break;
      }
    }
  }
  return ensemble_index;
}

void DABInit(void) {
  Dab.vol(vol);
  if (NumOfEnsembles > 0) {

    if((ensemble >= NumOfEnsembles) || (service >= Ensemble[ensemble].numberofservices)) {
      ensemble = 0;
      service = 0;      
    }

    Dab.tuneservice(Ensemble[ensemble].freq_index, Ensemble[ensemble].service[service].ServiceID, Ensemble[ensemble].service[service].CompID);

    GetLogoServiceID = Ensemble[ensemble].service[service].ServiceID;
    GetLogoEnsembleID = Ensemble[ensemble].EnsembleID;
    GetLogoFreq = 0;
    GetLogoGCC = Ensemble[ensemble].ECC + ((GetLogoServiceID >> 4) & 0xF00);
    UpdateLogo = GetLogoServiceID;
  }
}

void ServiceData(void) { 
  //remove spaces from end of string.
  for (uint8_t i = strlen(Dab.ServiceData) - 1; i > 0; i--) {
    if (Dab.ServiceData[i] == ' ') {
      Dab.ServiceData[i] = '\0';
    } else {
      break;
    }
  }

  //new text...but we compare to the previous text, as an update isn't always different.
  if (strcmp(service_text, Dab.ServiceData) != 0) {
    //Text has changed, copy to current text and reset scrolling to beginning
    strcpy(service_text, Dab.ServiceData);
    uint8_t text_len = strlen(service_text);

    tft.fillRect(0, 160, 480, 60, COLOR_BACKGROUND);

    //don't put any text on screen if we are awating a tune...
    if (seek_timer)
      return;

    if (text_len >= 46) {
      //lets find a space...
      uint8_t space = 0;
      uint8_t i;
      for (i = 0; i < 46; i++) {
        if (service_text[i] == ' ')
          space = i;
      }
      service_text[space] = '\0';
      displayText(20, 160, service_text, COLOR_RADIO_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
      if (text_len - space >= 46) {
        uint8_t space2 = 0;
        for (i = space; i < space + 46; i++) {
          if (service_text[i] == ' ')
            space2 = i;
        }
        service_text[space2] = '\0';
        displayText(20, 180, &service_text[space + 1], COLOR_RADIO_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
        if (text_len - space2 >= 46) {
          service_text[space2 + 43] = '.';
          service_text[space2 + 44] = '.';
          service_text[space2 + 45] = '.';
          service_text[space2 + 46] = '\0';
        }
        displayText(20, 200, &service_text[space2 + 1], COLOR_RADIO_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
      } else {
        displayText(20, 180, &service_text[space + 1], COLOR_RADIO_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
      }
    } else
      displayText(20, 160, service_text, COLOR_RADIO_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
    //Copy again so that we can tell if it changes...
    strcpy(service_text, Dab.ServiceData);
  }
}

void timer1ms(void) {
  if (display_screen == DISPLAY_SCREEN_DAB) {
    if (TimeTimer > 0) {
      TimeTimer--;
      if (TimeTimer == 0) {
        DABTime dabtime;
        char timestring[16];

        Dab.time(&dabtime);

        if (dabtime.Minutes != lastMinutes) {
          lastMinutes = dabtime.Minutes;

          tft.fillRect(355, 20, 125, 40, COLOR_BACKGROUND);

          //Date
          sprintf(timestring, "%02d/%02d/%02d", dabtime.Days, dabtime.Months, dabtime.Year);
          displayText(480 - 20, 20, timestring, COLOR_DATE_TIME, COLOR_BACKGROUND, 1, JUSTIFIED_RIGHT);
          //Time
          sprintf(timestring, "%02d:%02d", dabtime.Hours, dabtime.Minutes);
          displayText(480 - 20, 40, timestring, COLOR_DATE_TIME, COLOR_BACKGROUND, 1, JUSTIFIED_RIGHT);
        }
        TimeTimer = 1000;
      }
    }
    if (seek_timer > 0) {
      seek_timer--;
      if (seek_timer == 0) {
        Dab.tuneservice(Ensemble[ensemble].freq_index, Ensemble[ensemble].service[service].ServiceID, Ensemble[ensemble].service[service].CompID);
        save_settings_timer = 10000;

        GetLogoServiceID = Ensemble[ensemble].service[service].ServiceID;
        GetLogoEnsembleID = Ensemble[ensemble].EnsembleID;
        GetLogoFreq = 0;
        GetLogoGCC = Ensemble[ensemble].ECC + ((GetLogoServiceID >> 4) & 0xF00);
        UpdateLogo = GetLogoServiceID;
      }
    }
  }

  if(touch_timer)
    touch_timer--;

  if (RepeatButtonTimer > 0) {
    RepeatButtonTimer--;
    if (RepeatButtonTimer == 0) {
      ButtonDown(ActiveButton);
      RepeatButtonTimer = 250;
    }
  }
  if (save_settings_timer) {
    save_settings_timer--;
    if (save_settings_timer == 0) {
      if (GettingLogo == false)
        DABFileWrite();
      else
        save_settings_timer = 1000;
    }
  }

  if (StateTimer) {
    StateTimer--;
  }
}

void loop() {
  //Not useing this as it is low priority background task.
}

// The Main User Interface Task
void UIloop() {
  static uint32_t previous = 0;
  TouchPoint point = {0,0,0};

  //timer function
  uint32_t current = millis();
  uint32_t expired = current - previous;
  if (expired > 0) {
    if (expired > 10)
      expired = 10;  //limit to 10ms at a time..
    while (expired) {
      timer1ms();
      expired--;
    }
    previous = current;
  }

  if(touch_timer == 0)
  {
#if defined ROWLAND_DISPLAY
    if(ts.touched())
    {
      // Retrieve a point
      TS_Point p = ts.getPoint(); 
      point.x = 480-p.y;
      point.y = p.x;
      point.z = p.z;
    }
#elif defined DFROBOT_DISPLAY
    ts.read();
    if(ts.isTouched)
    {
      point.x = ts.points[0].x;
      point.y = ts.points[0].y;
      point.z = 1;
    }
#elif defined RESISTIVE_TOUCH
    if (ts.touched()) 
    {
      //Rough Calibration that'll do..
      #define XPT2046_XFAC_480x320 1326
      #define XPT2046_XOFFSET_480x320 -26 
      #define XPT2046_YFAC_480x320 894
      #define XPT2046_YOFFSET_480x320 -30

      TS_Point p = ts.getPoint();
      point.x=((long)XPT2046_XFAC_480x320*p.x)/10000+XPT2046_XOFFSET_480x320;
      point.y=((long)XPT2046_YFAC_480x320*p.y)/10000+XPT2046_YOFFSET_480x320;
      point.z = 1;
    }
#endif
    switch (display_screen) {
      case DISPLAY_SCREEN_BOOT:
        BootScreenloop();
        ProcessButtonPressBOOT(&point);
        break;

      case DISPLAY_SCREEN_DAB:
        DABScreenloop();
        ProcessButtonPressDAB(&point);
        break;
    }
    touch_timer = 20;
  }  
  Dab.task();
}

static uint8_t BootState = 0;
void BootScreenloop(void) {
  switch (BootState) {
    case 0:
      if (DABFileRead()) {
        displayText(20, 180, "Loading Services", COLOR_BACKGROUND, COLOR_RGB565_WHITE, 1, JUSTIFIED_LEFT);
        StateTimer = 3000;
        BootState = 2;
      } else {
        BootState = 1;
      }
      break;

    case 1:
      displayText(20, 180, "Scanning...", COLOR_BACKGROUND, COLOR_RGB565_WHITE, 1, JUSTIFIED_LEFT);
      NumOfEnsembles = DAB_scan();
      service = 0;
      ensemble = 0;
      DABFileWrite();
      StateTimer = 1000;
      BootState = 2;
      break;

    case 2:
      //Wait for any Button Presses
      if (StateTimer == 0) {
        DABInit();
        Display_DAB_Screen();
        display_screen = DISPLAY_SCREEN_DAB;
        BootState = 0;
      }
      break;
    default:
      BootState = 0;
  }
}

void DABRescan(void) {
  tft.fillRect(20, 180, 460, 40, COLOR_RGB565_WHITE);
  BootState = 1;
}

void DABScreenloop(void) {
  if (UpdateLogo) {
    char pngfilename[16];
    char jpgfilename[16];

    sprintf(pngfilename, "/%04x.png", UpdateLogo);
    sprintf(jpgfilename, "/%04x.jpg", UpdateLogo);
    UpdateLogo = 0;
    if (SPIFFS.exists(pngfilename)) {
      int rc = png.open((const char *)pngfilename, myOpen, myClose, myRead, mySeek, PNGDraw);
      if (rc == PNG_SUCCESS) {
        Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
        Serial.printf("PNG bufferSize %d\n", png.getBufferSize());
        rc = png.decode(NULL, 0);
        png.close();
      }
    } else if (SPIFFS.exists(jpgfilename)) {
      //.jpg might not be a JPEG file - try openning it in pngdec
      int rc = png.open((const char *)jpgfilename, myOpen, myClose, myRead, mySeek, PNGDraw);
      if (rc == PNG_SUCCESS) {
        Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
        Serial.printf("PNG bufferSize %d\n", png.getBufferSize());
        rc = png.decode(NULL, 0);
        png.close();
      } else {
        //Couldn't Find the logo, lets go to RadioDNS...
        tft.pushImage(/*x=*/20, /*y=*/20, /*w=*/128, /*h=*/128, /*bitmap gImage_Bitmap=*/(const uint16_t *)icon_DAB);
      }
    } else {
      //Couldn't Find the logo, lets go to RadioDNS...
      tft.pushImage(/*x=*/20, /*y=*/20, /*w=*/128, /*h=*/128, /*bitmap gImage_Bitmap=*/(const uint16_t *)icon_DAB);
      GetLogoSignal = true;
    }
  }

  if(WifiStatusUpdate)
  {
    WifiStatusUpdate = false;
    tft.fillRect(355, 60, 125, 20, COLOR_BACKGROUND);
    if (StatusWiFi == 1){
      displayText(480 - 20, 60, "WiFi Connected", COLOR_DATE_TIME, COLOR_BACKGROUND, 1, JUSTIFIED_RIGHT);
    }
    else {
      displayText(480 - 20, 60, "No WiFi", COLOR_DATE_TIME, COLOR_BACKGROUND, 1, JUSTIFIED_RIGHT); 
    }
  }

#ifdef DFROBOT
  switch (ui.getGestures()) {
    case ui.SLEFTGLIDE:
      DAB_NextService();
      break;
    case ui.SRIGHTGLIDE:
      DAB_PreviousService();
      break;
  }
#endif  
}

fs::File myfile;
void *myOpen(const char *filename, int32_t *size) {
  Serial.printf("Attempting to open %s\n", filename);
  myfile = SPIFFS.open(filename, "r");
  *size = myfile.size();
  return &myfile;
}

void myClose(void *handle) {
  if (myfile) myfile.close();
}

int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}

int32_t mySeek(PNGFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}

// Function to draw pixels to the display
void PNGDraw(PNGDRAW *pDraw) {
  uint16_t usPixels[128];
  
  png.getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  tft.pushImage(/*x=*/20, /*y=*/20 + pDraw->y, /*w=*/128, /*h=*/1, /*bitmap gImage_Bitmap=*/(uint16_t *)usPixels);
  sched_yield();
}

void ButtonDown(uint8_t button) {
  switch (button) {
    case BUTTON_BOOT_RESCAN:
      tft.drawRect(396, 246, 64, 64, COLOR_BACKGROUND);
      break;

    case BUTTON_DAB_PREVIOUS:
      tft.drawRect(20, 246, 64, 64, COLOR_BACKGROUND);
      DAB_PreviousService();
      break;
    case BUTTON_DAB_NEXT:
      tft.drawRect(100, 246, 64, 64, COLOR_BACKGROUND);
      DAB_NextService();
      break;
    case BUTTON_DAB_VOL_DOWN:
      tft.drawRect(316, 246, 64, 64, COLOR_BACKGROUND);
      DAB_VolDown();
      break;
    case BUTTON_DAB_VOL_UP:
      tft.drawRect(396, 246, 64, 64, COLOR_BACKGROUND);
      DAB_VolUp();
      break;
    case BUTTON_DAB_LOGO:
      if(logo_update < 10)
        logo_update++;
  }
}

void ButtonUp(uint8_t button) {
  switch (button) {
    case BUTTON_BOOT_RESCAN:
      tft.drawRect(396, 246, 64, 64, COLOR_RGB565_WHITE);
      DABRescan();
      break;

    case BUTTON_DAB_PREVIOUS:
      tft.drawRect(20, 246, 64, 64, COLOR_RGB565_WHITE);
      break;
    case BUTTON_DAB_NEXT:
      tft.drawRect(100, 246, 64, 64, COLOR_RGB565_WHITE);
      break;
    case BUTTON_DAB_VOL_DOWN:
      tft.drawRect(316, 246, 64, 64, COLOR_RGB565_WHITE);
      break;
    case BUTTON_DAB_VOL_UP:
      tft.drawRect(396, 246, 64, 64, COLOR_RGB565_WHITE);
      break;
    case BUTTON_DAB_LOGO:
      if(logo_update >= 10)
      {
        GetLogoServiceID = Ensemble[ensemble].service[service].ServiceID;
        GetLogoEnsembleID = Ensemble[ensemble].EnsembleID;
        GetLogoFreq = 0;
        GetLogoGCC = Ensemble[ensemble].ECC + ((GetLogoServiceID >> 4) & 0xF00);

        UpdateLogo = GetLogoServiceID; 
        char pngfilename[16];
        sprintf(pngfilename, "/%04x.png", UpdateLogo);
        SPIFFS.remove(pngfilename);
      }   
      break;
  }
}
#ifdef DFRobot
uint8_t stringToPoint(String str, DFRobot_UI::sPoint_t *point) {
  char pin[4];
  uint8_t nowi = 0;
  uint8_t n = 0;
  uint8_t b = 0;
  //Serial.println(str.length());
  memset(pin, '\0', 4);
  for (uint8_t i = 0; i < str.length(); i++) {
    if (str[i] == ',' || str[i] == ' ') {
      n = 0;
      if (nowi == 0) point[b].id = atoi(pin);
      if (nowi == 1) point[b].x = atoi(pin);
      if (nowi == 2) point[b].y = atoi(pin);
      if (nowi == 3) point[b].wSize = atoi(pin);
      if (nowi == 4) {
        point[b].hSize = atoi(pin);
        b++;
      }
      nowi++;
      if (nowi == 5) nowi = 0;
      memset(pin, '\0', 4);
      continue;
    }
    pin[n] = str[i];
    n++;
  }
  return 1;
}
#endif 

int8_t buttondown = -1;

void ProcessButtonPressBOOT(TouchPoint *point) {
  char buttontext[32];
  if (point->y) {
    if (buttondown == -1) {
      buttondown = -2;
      if ((point[0].y >= 246) && (point[0].y <= (246 + 64))) {
        if ((point[0].x >= 396) && (point[0].x <= (396 + 64)))
          buttondown = BUTTON_BOOT_RESCAN;
      }
    }
  } else {
    buttondown = -1;
  }

  if (buttondown != ActiveButton) {
    if (buttondown == -1) {
      ButtonUp(ActiveButton);
      RepeatButtonTimer = 0;
    } else {
      ButtonDown(buttondown);
      RepeatButtonTimer = 500;
    }
    ActiveButton = buttondown;
  }
}

void ProcessButtonPressDAB(TouchPoint *point) {
  char buttontext[32];

  if (point->y) {
    if (buttondown == -1) {
      buttondown = -2;
      if ((point[0].y >= 246) && (point[0].y <= (246 + 64))) {
        if ((point[0].x >= 20) && (point[0].x <= (20 + 64)))
          buttondown = BUTTON_DAB_PREVIOUS;
        else if ((point[0].x >= 100) && (point[0].x <= (100 + 64)))
          buttondown = BUTTON_DAB_NEXT;
        else if ((point[0].x >= 316) && (point[0].x <= (316 + 64)))
          buttondown = BUTTON_DAB_VOL_DOWN;
        else if ((point[0].x >= 396) && (point[0].x <= (396 + 64)))
          buttondown = BUTTON_DAB_VOL_UP;
      }
      else if (((point[0].y >= 2) && (point[0].y <= (20 + 128))) && ((point[0].x >= 20) && (point[0].x <= (20 + 128))))
      {
        logo_update = 0;
        buttondown = BUTTON_DAB_LOGO;
      }

    }
  } else {
    buttondown = -1;
  }

  if (buttondown != ActiveButton) {
    if (buttondown == -1) {
      ButtonUp(ActiveButton);
      RepeatButtonTimer = 0;
    } else {
      ButtonDown(buttondown);
      RepeatButtonTimer = 500;
    }
    ActiveButton = buttondown;
  }
}

void DAB_NextService(void) {
  if (Ensemble[ensemble].numberofservices > 0) {
    if (service < (Ensemble[ensemble].numberofservices - 1)) {
      service++;
    } else {
      ensemble++;
      if (ensemble > (NumOfEnsembles - 1)) {
        ensemble = 0;
      }
      service = 0;
    }
    seek_timer = 1500;
    service_text[0] = '\0';

    tft.fillRect(160, 20, 195, 40, COLOR_BACKGROUND);
    //Ensemble
    displayText(160, 20, Ensemble[ensemble].Ensemble, COLOR_ENSEMBLE_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
    //Service
    displayText(160, 40, Ensemble[ensemble].service[service].Label, COLOR_SERVICE_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
    tft.fillRect(0, 160, 480, 60, COLOR_BACKGROUND);
  }
}

void DAB_PreviousService(void) {
  if (Ensemble[ensemble].numberofservices > 0) {
    if (service > 0) {
      service--;
    } else {
      if (ensemble > 0) {
        ensemble--;
      } else {
        ensemble = NumOfEnsembles - 1;
      }
      service = Ensemble[ensemble].numberofservices - 1;
    }
    seek_timer = 1500;
    service_text[0] = '\0';

    tft.fillRect(160, 20, 195, 40, COLOR_BACKGROUND);
    //Ensemble
    displayText(160, 20, Ensemble[ensemble].Ensemble, COLOR_ENSEMBLE_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
    //Service
    displayText(160, 40, Ensemble[ensemble].service[service].Label, COLOR_SERVICE_TEXT, COLOR_BACKGROUND, 1, JUSTIFIED_LEFT);
    tft.fillRect(0, 160, 480, 60, COLOR_BACKGROUND);
  }
}

void DAB_VolUp(void) {
  if (vol < 63) {
    vol++;
    Dab.vol(vol);
  }
}

void DAB_VolDown(void) {
  if (vol > 0) {
    vol--;
    Dab.vol(vol);
  }
}

void DABSpiMsg(unsigned char *data, uint32_t len) {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));  //2MHz for starters...
  digitalWrite(DAB_CS, LOW);
  SPI.transfer(data, len);
  digitalWrite(DAB_CS, HIGH);
  SPI.endTransaction();
}