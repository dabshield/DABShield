////////////////////////////////////////////////////////////
// DAB/LCD Shield Example App
// AVIT Research Ltd
//
// Library Dependencies:
//    * DABShiled
//    * Adafruit_RGBLCDShiled
//    * FlashStorage (SAMD devices e.g. M0, Zero etc)
//    * DueFlashStorage (Ardino Due)
//
// Version Info:
// v0.1 22/02/2018 - initial release
// v0.2 05/07/2018 - added faster tune
// v0.3 19/09/2018 - array safeguards
// v0.4 18/10/2018 - non volatile memory storeage
// v0.5 29/03/2019 - add FM functionality and Menu
// v0.6 05/09/2019 - enhanced FM functionality (seek)
// v0.7 20/12/2019 - Ralf's no dab station additions
// v0.8 06/02/2020 - Fix for no dab stations EEPROM to allow FM.
// v0.9 05/06/2020 - non volatile memory storeage for Due
// v1.0 16/07/2020 - Display mapping for EU chars, Interface speed up for next/previous
////////////////////////////////////////////////////////////
#include "Arduino.h"

#include <SPI.h>
#include <DABShield.h>

#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>

#ifdef ARDUINO_ARCH_SAMD
#define Serial SerialUSB
//Comment out the following line if using the ICSP connector (and modified DABShield).
#define DAB_SPI_BITBANG

// Include API for FlashStorage only valid for SAMD21 (M0/Zero etc).
#define USE_EEPROM
#include <FlashStorage.h>
#endif

#ifdef ARDUINO_ARCH_SAM
#define Serial SerialUSB
//Comment out the following line if using the ICSP connector (and modified DABShield).
#define DAB_SPI_BITBANG
#endif

#ifdef ARDUINO_SAM_DUE
#define USE_EEPROM
#define DUE_FLASH
#include <DueFlashStorage.h>
DueFlashStorage dueFlashStorage;
#endif

#ifdef USE_EEPROM
uint16_t writeCurrentSettingsToFlashDelayTimer;
uint8_t writeCurrrentSettingsToFlash = 0;
#define WAIT_SAVECURRENTSETTINGS  10000   //wait 10 sec. after channel/vol change before save, prevent writing while selecting
#endif

DAB Dab;

Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();



//SPI Ports for BIT
const byte slaveSelectPin = 8;

#ifdef DAB_SPI_BITBANG
const byte SCKPin = 13;
const byte MISOPin = 12;
const byte MOSIPin = 11;
#endif

//App Data
//DAB

//Maxium number of services per ensembles
#define MAX_SERVICES_PER_ENSEMBLE 24
typedef struct _DABEnsembles
{
  uint8_t freq_index;
  char Ensemble[17];
  uint8_t numberofservices;
  DABService service[MAX_SERVICES_PER_ENSEMBLE];
} DABEnsembles;

//Maximum number of ensembles to be stored.
#define MAX_ENSEMBLES 16
DABEnsembles Ensemble[MAX_ENSEMBLES];
uint8_t NumOfEnsembles;

uint8_t vol = 63;
uint8_t service = 0;
uint8_t ensemble = 0;
uint8_t freq = 0;

char service_text[DAB_MAX_SERVICEDATA_LEN];
uint8_t text_index;
uint8_t lastMinutes = 99;

enum
{
  TEXTMODE_SERVICEDATA,
  TEXTMODE_TIME,
  TEXTMODE_ENSEMBLE,
};

uint8_t dab_mode = true;
uint8_t text_mode = TEXTMODE_SERVICEDATA;

byte rxindex = 0;
char rxdata[32];

//Display
enum
{
  DISPLAY_INIT,
  DISPLAY_DELAY,
  DISPLAY_RUN,
  DISPLAY_SERVICE,
  DISPLAY_VOL,
  DISPLAY_MENU,
};

uint16_t  displayTimer;
uint8_t   display_state;

//Buttons
uint8_t buttons;
uint8_t lastbuttons;
uint16_t readbuttonsTimer;
uint16_t debouncebuttonsTimer;

//Timer
uint32_t previous;

//FM
uint16_t fm_freq = 8750;
char fm_ps[9] = "";

//Function Prototypes
void timer1ms(void);
void DAB_NextService(void);
void DAB_PreviousService(void);
void DAB_VolUp(void);
void DAB_VolDown(void);

#ifdef USE_EEPROM
#define MAGIC 8500
int16_t magic_check;

#ifdef DUE_FLASH 
  //define the storeage locations in flash.
  #define FLASH_ensemble        0 //byte
  #define FLASH_service         1 //byte
  #define FLASH_vol             2 //byte
  #define FLASH_dab_mode        3 //byte
  #define FLASH_NumOfEnsembles  4 //byte  
  #define FLASH_magic_check     8 //&9 word
  #define FLASH_fm_freq         12 //&13 word
  #define FLASH_Ensembles       16 //and lots more
#else
  typedef struct _FlashStorage_Ensembles
  {
    DABEnsembles Ensemble[16];
  } FlashStorage_Ensembles;
  FlashStorage_Ensembles* pEnsemples = (FlashStorage_Ensembles*)&Ensemble[0];
  FlashStorage(magic_check_storage, int16_t);
  FlashStorage(Ensemble_storage, FlashStorage_Ensembles);
  FlashStorage(NumOfEnsembles_storage, uint8_t);
  FlashStorage(LastPlayedEnsemble_storage, uint8_t);
  FlashStorage(LastPlayedService_storage, uint8_t);
  FlashStorage(LastVolume_storage, uint8_t);
  FlashStorage(DabMode_storage, uint8_t);
  FlashStorage(FMFreq_storage, uint16_t);
#endif
#endif

void ServiceData(void);
uint8_t DAB_scan(void);
void process_buttons(uint8_t buttons);
void process_display(void);

//LCD custom chars for accented vowels.
const uint8_t abar[] = {0x0E,0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00};  //0x08
const uint8_t ebar[] = {0x0E,0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00};  //0x09
const uint8_t ibar[] = {0x0E,0x00,0x0C,0x04,0x04,0x04,0x0E,0x00};  //0x0A
const uint8_t obar[] = {0x0E,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00};  //0x0B
const uint8_t ubar[] = {0x0E,0x00,0x11,0x11,0x11,0x13,0x0D,0x00};  //0x0C

//Conversion from extended EBU Latin (DAB/RDS) to LCD charset  (0x80 to 0xFF)
const char EBULatintoLCD[0x80] = {0x08,0x08,0x09,0x09,0x0A,0x0A,0x0B,0x0B,0x0C,0x0C,'N', 'C', 'S', 0xE2,'i', 'Y',
                                  0x08,0x08,0x09,0x09,0x0A,0x0A,0x0B,0x0B,0x0C,0x0C,'n', 'c', 's', 'g', 'i', 'y', 
                                  'a', 'a', 'c', '/', 'G', 'e', 'n', 0x0B,0xF7,'$', '$', '$', 0x7F,'^', 0x7E,'v', 
                                  '0', '1', '2', '3', '+', 'I', 'n', 0x0C,0xE4,'?', 0xFD,0xDF,'_', '_', '_', '_', 
                                  'A', 'A', 'E', 'E', 'I', 'I', 'O', 'O', 'U', 'U', 'R', 'C', 'S', 'Z', 'D', 'L', 
                                  'A', 'A', 'E', 'E', 'I', 'I', 'O', 'O', 'U', 'U', 'r', 'c', 's', 'z', 'd', 'l', 
                                  'A', 'A', 'E', 'E', 'y', 'Y', 'O', 'O', 'p', 'n', 'R', 'C', 'S', 'Z', 't', 'd', 
                                  0x08,0x08,'e', 'e', 'w', 'y', 'O', 'o', 'p', 'n', 'r', 'c', 's', 'z', 't', ' '};

void replaceSpecialCharsInplace(char * p, int len)
{
  for (int j = 0; j < (len); j++)
  {
    //correct wrong chars for our LCD display
    unsigned char * cc = (unsigned char*) &p[j];
    if(*cc > 0x7F)
      *cc = EBULatintoLCD[*cc - 0x80];
  }
}

void setup() {

  //Intitialise the terminal (not used in this app).
  Serial.begin(115200);

  //Enable SPI
#ifdef DAB_SPI_BITBANG
  pinMode(slaveSelectPin, OUTPUT);
  pinMode(SCKPin, OUTPUT);
  pinMode(MOSIPin, OUTPUT);
  pinMode(MISOPin, INPUT_PULLUP);
  digitalWrite(slaveSelectPin, HIGH);
#else
  pinMode(slaveSelectPin, OUTPUT);
  digitalWrite(slaveSelectPin, HIGH);
  SPI.begin();
#endif

  //Display Startup Message
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AVIT DAB/FM v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Initialising....");
  lcd.setBacklight(1);
  
  lcd.createChar(0, (uint8_t *)abar);
  lcd.createChar(1, (uint8_t *)ebar);
  lcd.createChar(2, (uint8_t *)ibar);
  lcd.createChar(3, (uint8_t *)obar);
  lcd.createChar(4, (uint8_t *)ubar);
  
  display_state = DISPLAY_INIT;

  //DAB Setup
  Dab.setCallback(ServiceData);
  Dab.begin();

  if (Dab.error != 0)
  {
    lcd.setCursor(0, 1);
    lcd.print("ERROR");
  }
  else
  {

#ifdef USE_EEPROM
    
    #ifdef DUE_FLASH
      NumOfEnsembles = dueFlashStorage.read(FLASH_NumOfEnsembles);
      dab_mode = dueFlashStorage.read(FLASH_dab_mode);
      memcpy(&magic_check, dueFlashStorage.readAddress(FLASH_magic_check), sizeof(magic_check));     
    #else
      NumOfEnsembles = NumOfEnsembles_storage.read();
      magic_check = magic_check_storage.read();
      dab_mode = DabMode_storage.read();
    #endif
    
    buttons = lcd.readButtons();

    if ((magic_check != MAGIC) || 
        (buttons & BUTTON_SELECT) ||
        ((dab_mode == true) && (NumOfEnsembles == 0)) )
    {
      dab_mode = true;
      vol = 25;
      Dab.vol(vol);
      ScanforServices();
    }
    else
    {
      lcd.setCursor(0, 1);
      lcd.print("Read EEPROM..");

      #ifdef DUE_FLASH
        NumOfEnsembles = dueFlashStorage.read(FLASH_NumOfEnsembles);
        service = dueFlashStorage.read(FLASH_service);
        ensemble = dueFlashStorage.read(FLASH_ensemble);
        vol = dueFlashStorage.read(FLASH_vol);
        dab_mode = dueFlashStorage.read(FLASH_dab_mode);
        memcpy(&fm_freq , dueFlashStorage.readAddress(FLASH_fm_freq), sizeof(fm_freq));  
        memcpy(&Ensemble[0], dueFlashStorage.readAddress(FLASH_Ensembles), sizeof(Ensemble));        
      #else      
        NumOfEnsembles = NumOfEnsembles_storage.read();
        Ensemble_storage.read(pEnsemples);
        service = LastPlayedService_storage.read();
        ensemble = LastPlayedEnsemble_storage.read();
        vol = LastVolume_storage.read();
        dab_mode = DabMode_storage.read();
        fm_freq = FMFreq_storage.read();
      #endif
      
      Dab.vol(vol);

      if (dab_mode == true)
      {
        if (NumOfEnsembles > 0)
        {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Tuning ensemble:");
          lcd.setCursor(0, 1);
          lcd.print(Ensemble[ensemble].Ensemble);
          Dab.tuneservice(Ensemble[ensemble].freq_index, Ensemble[ensemble].service[service].ServiceID, Ensemble[ensemble].service[service].CompID);
        }
      }
      else
      {
        Dab.begin(1);
        Dab.tune(fm_freq);
        Dab.vol(vol);
      }
    }
#else
    ScanforServices();
#endif
  }
  previous = millis();
}

void ScanforServices(void)
{
  //SCAN for Services...
  lcd.setCursor(0, 1);
  lcd.print("Scanning...     ");

  ensemble = 0;
  service = 0;
  NumOfEnsembles = DAB_scan();

#ifdef USE_EEPROM
  //Write to EEPROM even if nothing found as EEPROM will be need if we swtich to FM.
  lcd.setCursor(0, 1);
  lcd.print("save to EEPROM..");

  magic_check = MAGIC; 
  #ifdef DUE_FLASH
    dueFlashStorage.write(FLASH_NumOfEnsembles, NumOfEnsembles);
    dueFlashStorage.write(FLASH_ensemble, ensemble);
    dueFlashStorage.write(FLASH_service, service);
    dueFlashStorage.write(FLASH_dab_mode, dab_mode);
    dueFlashStorage.write(FLASH_vol, vol);
    dueFlashStorage.write(FLASH_magic_check, (uint8_t *)&magic_check, sizeof(magic_check));
    dueFlashStorage.write(FLASH_fm_freq, (uint8_t *)&fm_freq, sizeof(fm_freq));
    dueFlashStorage.write(FLASH_Ensembles, (uint8_t *)&Ensemble[0], sizeof(Ensemble));        
  #else
    magic_check_storage.write(magic_check);
    NumOfEnsembles_storage.write(NumOfEnsembles);
    Ensemble_storage.write(*pEnsemples);
    LastPlayedEnsemble_storage.write(ensemble);
    LastPlayedService_storage.write(service);
    DabMode_storage.write(dab_mode);
    LastVolume_storage.write(vol);
    FMFreq_storage.write(fm_freq);
  #endif
#endif

  if (NumOfEnsembles > 0)
  {
    //Something has been found so tune to First service    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Tuning ensemble:");
    lcd.setCursor(0, 1);
    lcd.print(Ensemble[ensemble].Ensemble);

    Dab.tune(Ensemble[ensemble].freq_index);
    Dab.set_service(service);
  }
}

//Callback from DAB Library when new service data (radio text) is recieved.
void ServiceData(void)
{
  //remove spaces from end of string.
  for (uint8_t i = strlen(Dab.ServiceData) - 1; i > 0; i--)
  {
    if (Dab.ServiceData[i] == ' ')
    {
      Dab.ServiceData[i] = '\0';
    }
    else
    {
      break;
    }
  }
  //convert charset for our LCD display
  replaceSpecialCharsInplace(Dab.ServiceData, strlen(Dab.ServiceData));         
    
  //new text...but we compare to the previous text, as an update isn't always different.
  if (strcmp(service_text, Dab.ServiceData) != 0)
  {   
    //Text has changed, copy to current text and reset scrolling to beginning
    strcpy(service_text, Dab.ServiceData);
    text_index = 0;
  }
  replaceSpecialCharsInplace(Dab.ps, strlen(Dab.ps));
  if (strcmp(fm_ps, (char*)Dab.ps) != 0)
  {
    strcpy(fm_ps, (char*)Dab.ps);
    display_state = DISPLAY_SERVICE;
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  Dab.task();

#ifdef USE_EEPROM
  //delayed save of selected Ensemble/Service/vol to flash
  if (writeCurrentSettingsToFlashDelayTimer == 0 && writeCurrrentSettingsToFlash)
  {
    lcd.setCursor(0, 1);
    lcd.print("Save Setting... ");
    
    #ifdef DUE_FLASH
      dueFlashStorage.write(FLASH_ensemble, ensemble);
      dueFlashStorage.write(FLASH_service, service);
      dueFlashStorage.write(FLASH_dab_mode, dab_mode);
      dueFlashStorage.write(FLASH_vol, vol);
      dueFlashStorage.write(FLASH_fm_freq, (uint8_t *)&fm_freq, sizeof(fm_freq));        
    #else
      LastPlayedEnsemble_storage.write(ensemble);
      LastPlayedService_storage.write(service);
      LastVolume_storage.write(vol);
      DabMode_storage.write(dab_mode);
      FMFreq_storage.write(fm_freq);
    #endif
    
    writeCurrrentSettingsToFlash = 0;
    lcd.setCursor(13, 1);
    lcd.print("ok");
    displayTimer = 1000;
  }
#endif

  //Read Buttons...
  if (readbuttonsTimer == 0)
  {
    buttons = lcd.readButtons();
    readbuttonsTimer = 50; //ms

    if (debouncebuttonsTimer == 0)
    {
      if (buttons > 0)
      {
        process_buttons(buttons);
        process_display();
        if (buttons != lastbuttons)
        {
          debouncebuttonsTimer = 500; //ms
        }
        else
        {
          debouncebuttonsTimer = 200; //ms
        }
      }
    }
    if (buttons == 0)
    {
      debouncebuttonsTimer = 0;
      process_buttons(buttons);
    }
    lastbuttons = buttons;
    process_display();
  }

  //timer function
  uint32_t current = millis();
  uint32_t expired = current - previous;
  if (expired > 0)
  {
    while (expired)
    {
      timer1ms();
      expired--;
    }
    previous = current;
  }
}

uint8_t button_select_timer = 0;
uint8_t button_select = true;
uint8_t seek_timer = 0;
uint8_t seek_dir = 0;
uint8_t menu_mode = 0;
uint16_t menu_timer = 0;

void process_buttons(uint8_t buttons)
{
  if (menu_mode == 0)
  {
    if (buttons & BUTTON_UP)
    {
      DAB_VolUp();
      display_state = DISPLAY_VOL;
    }
    if (buttons & BUTTON_DOWN)
    {
      DAB_VolDown();
      display_state = DISPLAY_VOL;
    }
    if (buttons & BUTTON_LEFT)
    {
      if (dab_mode == true)
      {
        seek_timer = 5;
        DAB_PreviousService();
        display_state = DISPLAY_SERVICE;
      }
      else
      {
        if (seek_timer < 5)
        {
          seek_dir = 0;
          seek_timer++;
        }
        if (seek_timer == 5)
        {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("FM Seek: down   ");
          Dab.seek(seek_dir, 1);
          fm_freq = Dab.freq;
#ifdef USE_EEPROM
          writeCurrrentSettingsToFlash = true;
          writeCurrentSettingsToFlashDelayTimer = WAIT_SAVECURRENTSETTINGS; //prevent writing while selecting
#endif
          service_text[0] = '\0';
          fm_ps[0] = '\0';
          display_state = DISPLAY_SERVICE;
          seek_timer++;
        }
      }
    }
    if (buttons & BUTTON_RIGHT) {
      if (dab_mode == true)
      {
        seek_timer = 5;
        DAB_NextService();
        display_state = DISPLAY_SERVICE;
      }
      else
      {
        if (seek_timer < 5)
        {
          seek_dir = 1;
          seek_timer++;
        }
        if (seek_timer == 5)
        {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("FM Seek: up     ");
          Dab.seek(seek_dir, 1);
          fm_freq = Dab.freq;
#ifdef USE_EEPROM
          writeCurrrentSettingsToFlash = true;
          writeCurrentSettingsToFlashDelayTimer = WAIT_SAVECURRENTSETTINGS; //prevent writing while selecting
#endif
          service_text[0] = '\0';
          fm_ps[0] = '\0';
          display_state = DISPLAY_SERVICE;
          seek_timer++;
        }
      }
    }
    if (buttons & BUTTON_SELECT)
    {
      if (button_select_timer < 10)
      {
        button_select_timer++;
      }
      else if (button_select_timer == 10)
      {
        display_state = DISPLAY_MENU;
        process_display();
        button_select_timer++;
      }
      else
      {
        //do nothing...
      }
    }
    if (buttons == 0)
    {
      //Seek Release
      if (seek_timer > 0)
      {
        if (dab_mode == true)
        {
            if(seek_timer)
            {
              seek_timer--;
              if(seek_timer == 0)
              {
                lcd.setCursor(0, 1);
                lcd.print("tuning...       ");
                Dab.tuneservice(Ensemble[ensemble].freq_index, Ensemble[ensemble].service[service].ServiceID, Ensemble[ensemble].service[service].CompID);  
                display_state = DISPLAY_DELAY;
                displayTimer = 1000;     
              }
            }
        }
        else
        {        
          if (seek_timer < 5)
          {
            if (seek_dir == 1)
            {
              if (fm_freq < 10800)
              {
                fm_freq += 10;
              }
              else
              {
                fm_freq = 8750;
              }
            }
            else
            {
              if (fm_freq > 8750)
              {
                fm_freq -= 10;
              }
              else
              {
                fm_freq = 10800;
              }
            }
            Dab.tune(fm_freq);
  #ifdef USE_EEPROM
            writeCurrrentSettingsToFlash = true;
            writeCurrentSettingsToFlashDelayTimer = WAIT_SAVECURRENTSETTINGS; //prevent writing while selecting
  #endif
            service_text[0] = '\0';
            fm_ps[0] = '\0';
            display_state = DISPLAY_SERVICE;
          }
          seek_timer = 0;
        }        
      }
      //Select Release
      else if (button_select_timer > 0)
      {
        if (button_select_timer > 10)
        {
          menu_mode = 1;
          menu_timer = 5000;
        }
        else
        {
          //Toggle Display Modes
          switch (text_mode)
          {
            case TEXTMODE_SERVICEDATA:
              text_mode = TEXTMODE_TIME;
              break;
            case TEXTMODE_TIME:
              if (dab_mode == true)
              {
                text_mode = TEXTMODE_ENSEMBLE;
              }
              else
              {
                text_mode = TEXTMODE_SERVICEDATA;
              }
              break;
            case TEXTMODE_ENSEMBLE:
              text_mode = TEXTMODE_SERVICEDATA;
              break;
            default:
              text_mode = TEXTMODE_SERVICEDATA;
              break;
          }
          displayTimer = 0;
          text_index = 0; //Reset text scroll
          lastMinutes = 99; //Force a time update
        }
        button_select_timer = 0;
      }
    }
  }
  else //menu
  {
    if (dab_mode == true)
    {
      if (buttons & BUTTON_RIGHT)
      {
        menu_timer = 5000;  //stay in menu mode
        if (menu_mode < 2)
        {
          menu_mode++;
        }
      }
      if (buttons & BUTTON_LEFT)
      {
        menu_timer = 5000;  //stay in menu mode
        if (menu_mode > 1)
        {
          menu_mode--;
        }
      }      
      if (menu_mode == 1) //FM
      {
        if (buttons & BUTTON_SELECT)
        {
          Dab.begin(1);
          Dab.tune(fm_freq);
#ifdef USE_EEPROM
          writeCurrrentSettingsToFlash = true;
          writeCurrentSettingsToFlashDelayTimer = WAIT_SAVECURRENTSETTINGS; //prevent writing while selecting
#endif
          text_mode = TEXTMODE_SERVICEDATA;
          dab_mode = false;
          Dab.vol(vol);
          service_text[0] = '\0';
          fm_ps[0] = '\0';
          menu_mode = 0;
          menu_timer = 0;
          display_state = DISPLAY_DELAY;
          displayTimer = 0;
        }
      }
      else if (menu_mode == 2) //SCAN
      {
        if (buttons & BUTTON_SELECT)
        {
          lcd.setCursor(0, 0);
          lcd.print("Re-SCAN         ");          
          ScanforServices();        
          service_text[0] = '\0';
          fm_ps[0] = '\0';
          menu_mode = 0;
          menu_timer = 0;
          display_state = DISPLAY_DELAY;
          displayTimer = 0;   
        }     
      }
    }
    else
    {
      if (buttons & BUTTON_RIGHT)
      {
        menu_timer = 5000;  //stay in menu mode
        if (menu_mode < 1)
        {
          menu_mode++;
        }
      }
      if (buttons & BUTTON_LEFT)
      {
        menu_timer = 5000;  //stay in menu mode
        if (menu_mode > 1)
        {
          menu_mode--;
        }
      }
      if (menu_mode == 1) //DAB
      {
        if (buttons & BUTTON_SELECT)
        {
          Dab.begin(0);
          if (NumOfEnsembles > 0)
          {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Tuning ensemble:");
            lcd.setCursor(0, 1);
            lcd.print(Ensemble[ensemble].Ensemble);
            Dab.tuneservice(Ensemble[ensemble].freq_index, Ensemble[ensemble].service[service].ServiceID, Ensemble[ensemble].service[service].CompID);
          }
          else
          {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("No DAB stations ");
          }
          dab_mode = true;
          Dab.vol(vol);        
          service_text[0] = '\0';
          fm_ps[0] = '\0';
          menu_mode = 0;
          menu_timer = 0;
          display_state = DISPLAY_DELAY;
          displayTimer = 0;
        }
      }
    }
  }
}

void process_display(void)
{
  switch (display_state)
  {
    case DISPLAY_INIT:
      displayTimer = 1000;
      display_state = DISPLAY_DELAY;
      break;
    case DISPLAY_DELAY:
      if (displayTimer == 0)
      {
        lcd.clear();
        lcd.setCursor(0, 0);
        if (dab_mode == true)
        {
          if (NumOfEnsembles > 0)
          {
            lcd.print(Ensemble[ensemble].service[service].Label);
          }
          else
          {
            lcd.setCursor(0, 0);
            lcd.print("No DAB stations ");
          }
        }
        else
        {
          char freqstring[32];
          if (strlen(fm_ps) == 0)
          {
            sprintf(freqstring, "%-8s   %3d.%01d", "FM      ", fm_freq / 100, (fm_freq % 100) / 10);
          }
          else
          {
            sprintf(freqstring, "%-8s   %3d.%01d", fm_ps, fm_freq / 100, (fm_freq % 100) / 10);
          }
          lcd.print(freqstring);
        }
        text_index = 0; //Reset text scroll
        lastMinutes = 99; //Force a time update
        display_state = DISPLAY_RUN;
      }
      break;
    case DISPLAY_RUN:
      if (displayTimer == 0)
      {
        char text[17];
        uint8_t text_len;

        if (text_mode == TEXTMODE_TIME)
        {
          //time..
          DABTime dabtime;
          char timestring[16];

          if (dab_mode == true)
          {
            if (NumOfEnsembles > 0)
            {
              Dab.time(&dabtime);
            }
            else
            {
              lcd.setCursor(0, 0);
              lcd.print("No DAB stations ");
            }
          }
          else
          {
            dabtime.Days = Dab.Days;
            dabtime.Months = Dab.Months;
            dabtime.Year = Dab.Year;
            dabtime.Hours = Dab.Hours;
            dabtime.Minutes = Dab.Minutes;
            dabtime.Seconds = 0;
          }
          
          if (dabtime.Minutes != lastMinutes)
          {
            lastMinutes = dabtime.Minutes;
            lcd.setCursor(0, 1);
            sprintf(timestring, "%02d/%02d/%02d ", dabtime.Days, dabtime.Months, dabtime.Year);
            lcd.print(timestring);
            sprintf(timestring, "%02d:%02d  ", dabtime.Hours, dabtime.Minutes);
            lcd.print(timestring);
          }
          displayTimer = 1000;
        }
        else if (text_mode == TEXTMODE_SERVICEDATA)
        {
          if ((NumOfEnsembles == 0) && (dab_mode == true))
          {
            lcd.setCursor(0, 0);
            lcd.print("No DAB stations ");
          }
          else
          {
            lcd.setCursor(0, 1);
            text_len = strlen(service_text);
            if (text_len > 0)
            {
              strncpy(text, &service_text[text_index], 16);
              for (uint8_t i = text_len; i < 16; i++)
              {
                text[i] = ' ';
              }
              text[16] = '\0';

              lcd.print(text);

              if (text_len > 16)
              {
                text_index++;
              }

              if (text_index == 1)
              {
                displayTimer = 1000;
              }
              else if ((text_index + 16) > text_len)
              {
                text_index = 0;
                displayTimer = 1000;
              }
              else
              {
                displayTimer = 333;
              }
            }
            else
            {
              lcd.print("                ");
              displayTimer = 1000;
            }
          }
        }
        else //TEXTMODE_ENSEMBLE
        {
          if ((NumOfEnsembles == 0) && (dab_mode == true))
          {
            lcd.setCursor(0, 0);
            lcd.print("No DAB stations ");
          }
          else
          {
            lcd.setCursor(0, 1);
            text_len = strlen(Ensemble[ensemble].Ensemble);
            if (text_len > 0)
            {
              strncpy(text, Ensemble[ensemble].Ensemble, 16);
              for (uint8_t i = text_len; i < 16; i++)
              {
                text[i] = ' ';
              }
              text[16] = '\0';

              lcd.print(text);
            }
            displayTimer = 1000;
          }
        }
      }
      break;
    case DISPLAY_SERVICE:
      lcd.clear();
      lcd.setCursor(0, 0);
      if (dab_mode == true)
      {
        if (NumOfEnsembles > 0)
        {
          lcd.print(Ensemble[ensemble].service[service].Label);
        }
        else
        {
          lcd.setCursor(0, 0);
          lcd.print("No DAB stations ");
        }
      }
      else
      {
        char freqstring[32];
        if (strlen(fm_ps) == 0)
        {
          sprintf(freqstring, "%-8s   %3d.%01d", "FM      ", fm_freq / 100, (fm_freq % 100) / 10);
        }
        else
        {
          sprintf(freqstring, "%-8s   %3d.%01d", fm_ps, fm_freq / 100, (fm_freq % 100) / 10);
        }
        lcd.print(freqstring);
      }
      text_index = 0; //Reset text scroll
      lastMinutes = 99; //Force a time update
      display_state = DISPLAY_RUN;
      break;
    case DISPLAY_VOL:
      lcd.setCursor(0, 1);
      lcd.print("Vol:            ");
      if (vol > 9)
        lcd.setCursor(5, 1);
      else
        lcd.setCursor(6, 1);
      lcd.print(vol);
      display_state = DISPLAY_DELAY;
      displayTimer = 1000;
      break;
    case DISPLAY_MENU:
      lcd.setCursor(0, 0);
      lcd.print("Menu            ");
      lcd.setCursor(0, 1);
      if (dab_mode == true)
      {     
        if (menu_mode <= 1)
        {
          lcd.print("FM Mode         ");
        }
        else if (menu_mode == 2)
        {
          lcd.print("Re-SCAN         ");
        }
      }
      else
      {
        if (menu_mode <= 1)
        {
          lcd.print("DAB Mode        ");
        }
      }
      break;
  }
}


void timer1ms(void)
{
  if (readbuttonsTimer > 0)
  {
    readbuttonsTimer--;
  }
  if (debouncebuttonsTimer > 0)
  {
    debouncebuttonsTimer--;
  }
  if (displayTimer > 0)
  {
    displayTimer--;
  }
  if (menu_timer > 0)
  {
    menu_timer--;
    if (menu_timer == 0)
    {
      menu_mode = 0;
      display_state = DISPLAY_DELAY;
      displayTimer = 0;
    }
  }
#ifdef USE_EEPROM
  if (writeCurrentSettingsToFlashDelayTimer > 0)
  {
    writeCurrentSettingsToFlashDelayTimer--;
  }
#endif
}

uint8_t DAB_scan(void)
{
  uint8_t freq_index;
  uint8_t ensemble_index;
  uint8_t numofservices;
  uint8_t i;

  ensemble_index = 0;

  for (freq_index = 0; freq_index < DAB_FREQS; freq_index++)
  {
    if (freq_index > 9)
      lcd.setCursor(11, 1);
    else
      lcd.setCursor(12, 1);
    lcd.print(freq_index);
    lcd.setCursor(13, 1);
    lcd.print("/37");

    Dab.tune(freq_index);
    if (Dab.servicevalid() == true)
    {
      //Copy Services into Array...
      Ensemble[ensemble_index].freq_index = freq_index;
      //Convert charset for our LCD display
      replaceSpecialCharsInplace(Dab.Ensemble, strlen(Dab.Ensemble));         
      strcpy(Ensemble[ensemble_index].Ensemble, Dab.Ensemble);
      //ensure we don't overflow our array.
      numofservices = Dab.numberofservices;
      if (numofservices > MAX_SERVICES_PER_ENSEMBLE)
      {
        numofservices = MAX_SERVICES_PER_ENSEMBLE;
      }
      Ensemble[ensemble_index].numberofservices = numofservices;
      for (i = 0; i < numofservices; i++)
      {
        Ensemble[ensemble_index].service[i].ServiceID = Dab.service[i].ServiceID;
        Ensemble[ensemble_index].service[i].CompID = Dab.service[i].CompID;
        //Convert charset for our LCD display
        replaceSpecialCharsInplace(Dab.service[i].Label, strlen(Dab.service[i].Label));         
        strcpy(Ensemble[ensemble_index].service[i].Label, Dab.service[i].Label);
      }
      ensemble_index++;
      //Limit to our array size
      if (ensemble_index == MAX_ENSEMBLES)
      {
        break;
      }
    }
  }
  return ensemble_index;
}

void DAB_NextService(void)
{
  if (Ensemble[ensemble].numberofservices > 0)
  {
    if (service < (Ensemble[ensemble].numberofservices - 1))
    {
      service++;
    }
    else
    {
      ensemble++;
      if (ensemble > (NumOfEnsembles - 1))
      {
        ensemble = 0;
      }
      service = 0;
    }
    service_text[0] = '\0';
#ifdef USE_EEPROM
    writeCurrrentSettingsToFlash = true;
    writeCurrentSettingsToFlashDelayTimer = WAIT_SAVECURRENTSETTINGS; //prevent writing while selecting
#endif
  }
}

void DAB_PreviousService(void)
{
  if (Ensemble[ensemble].numberofservices > 0)
  {
    if (service > 0)
    {
      service--;
    }
    else
    {
      if (ensemble > 0)
      {
        ensemble--;
      }
      else
      {
        ensemble = NumOfEnsembles - 1;
      }
      service = Ensemble[ensemble].numberofservices - 1;
    }
    service_text[0] = '\0';
#ifdef USE_EEPROM
    writeCurrrentSettingsToFlash = true;
    writeCurrentSettingsToFlashDelayTimer = WAIT_SAVECURRENTSETTINGS; //prevent writing while selecting
#endif
  }
}

void DAB_VolUp(void)
{
  if (vol < 63)
  {
    vol++;
    Dab.vol(vol);
#ifdef USE_EEPROM
    writeCurrrentSettingsToFlash = true;
    writeCurrentSettingsToFlashDelayTimer = WAIT_SAVECURRENTSETTINGS; //prevent writing while selecting
#endif
  }
}

void DAB_VolDown(void)
{
  if (vol > 0)
  {
    vol--;
    Dab.vol(vol);
#ifdef USE_EEPROM
    writeCurrrentSettingsToFlash = true;
    writeCurrentSettingsToFlashDelayTimer = WAIT_SAVECURRENTSETTINGS; //prevent writing while selecting
#endif
  }
}


#ifdef DAB_SPI_BITBANG
void DABSpiMsg(unsigned char *data, uint32_t len)
{
  digitalWrite(SCKPin, LOW);
  digitalWrite (slaveSelectPin, LOW);
  for (uint32_t l = 0; l < len; l++)
  {
    unsigned char spiByte = data[l];
    for (uint32_t i = 0; i < 8; i++)
    {
      digitalWrite(MOSIPin, (spiByte & 0x80) ? HIGH : LOW);
      delayMicroseconds(1);
      digitalWrite(SCKPin, HIGH);
      spiByte = (spiByte << 1) | digitalRead(MISOPin);
      digitalWrite(SCKPin, LOW);
      delayMicroseconds(1);
    }
    data[l] = spiByte;
  }
  digitalWrite (slaveSelectPin, HIGH);
}
#else
void DABSpiMsg(unsigned char *data, uint32_t len)
{
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));    //2MHz for starters...
  digitalWrite (slaveSelectPin, LOW);
  SPI.transfer(data, len);
  digitalWrite (slaveSelectPin, HIGH);
  SPI.endTransaction();
}
#endif
