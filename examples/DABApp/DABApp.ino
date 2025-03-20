////////////////////////////////////////////////////////////
// DAB Shield Example App
// AVIT Research Ltd
// v0.1 11/05/2017 - initial release
// v0.2 09/10/2017 - support for M0 etc.
// v0.3 17/10/2017 - Added Max Services to prevent overflow (Library only)
// v0.4 20/11/2017 - Added Version Info
// v0.5 23/11/2017 - Added More 'Getting Started' Debugging
// v0.6 23/11/2017 - support for Due etc.
// v0.7 27/11/2017 - Added Get Time support
// v0.8 06/12/2017 - Added FM/RDS Functionality
// v0.9 05/09/2019 - Enhanced FM functionality (seek/scan)
// v1.0 23/09/2020 - Added ESP32 D1 R32 Support
// v1.1 11/12/2020 - Added DAB Status, Mono and Mute
// v1.2 01/11/2021 - Added DAB Service Type, Dab/Dab+
// v2.0 27/02/2025 - Added Support for DAB Shield Pro
///////////////////////////////////////////////////////////

/*********  DAB SHIELD V3.0 **********/
// Setup up Speaker output.
#define SPEAKER_OUTPUT  SPEAKER_DIFF   //SPEAKER_NONE, SPEAKER_DIFF, SPEAKER_STEREO

#include <SPI.h>
#include <DABShield.h>

//In order to compile for the UNO, all strings are put into flash (PROGRAM MEMORY)
const char pty_0[] PROGMEM =  "None";
const char pty_1[] PROGMEM =  "News";
const char pty_2[] PROGMEM =  "Current affairs";
const char pty_3[] PROGMEM =  "Information";
const char pty_4[] PROGMEM =  "Sport";
const char pty_5[] PROGMEM =  "Education";
const char pty_6[] PROGMEM =  "Drama";
const char pty_7[] PROGMEM =  "Culture";
const char pty_8[] PROGMEM =  "Science";
const char pty_9[] PROGMEM =  "Varied";
const char pty_10[] PROGMEM =  "Pop music";
const char pty_11[] PROGMEM =  "Rock music";
const char pty_12[] PROGMEM =  "Easy listening music";
const char pty_13[] PROGMEM =  "Light classical";
const char pty_14[] PROGMEM =  "Serious classical";
const char pty_15[] PROGMEM =  "Other music";
const char pty_16[] PROGMEM =  "Weather";
const char pty_17[] PROGMEM =  "Finance";
const char pty_18[] PROGMEM =  "Children’s programmes";
const char pty_19[] PROGMEM =  "Social Affairs";
const char pty_20[] PROGMEM =  "Religion";
const char pty_21[] PROGMEM =  "Phone In";
const char pty_22[] PROGMEM =  "Travel";
const char pty_23[] PROGMEM =  "Leisure";
const char pty_24[] PROGMEM =  "Jazz music";
const char pty_25[] PROGMEM =  "Country music";
const char pty_26[] PROGMEM =  "National music";
const char pty_27[] PROGMEM =  "Oldies music";
const char pty_28[] PROGMEM =  "Folk music";
const char pty_29[] PROGMEM =  "Documentary";
const char pty_30[] PROGMEM =  "Alarm test";
const char pty_31[] PROGMEM =  "Alarm";
const char *const pty[] PROGMEM = {pty_0,pty_1,pty_2,pty_3,pty_4,pty_5,pty_6,pty_7,pty_8,pty_9,pty_10,pty_11,pty_12,pty_13,pty_14,pty_15,pty_16,pty_17,pty_18,pty_19,pty_20,pty_21,pty_22,pty_23,pty_24,pty_25,pty_26,pty_27,pty_28,pty_29,pty_30,pty_31};

const char mode_0[] PROGMEM = "Dual";
const char mode_1[] PROGMEM = "Mono";
const char mode_2[] PROGMEM = "Stereo";
const char mode_3[] PROGMEM = "Joint Stereo";
const char *const audiomode[] PROGMEM = {mode_0,mode_1,mode_2,mode_3};

//#define DAB_SPI_BITBANG
//#define ANALOG_VOLUME

#ifdef ARDUINO_ARCH_SAMD
#define Serial SerialUSB
//Comment out the following line if using the ICSP connector (and modified DABShield).
#define DAB_SPI_BITBANG
#endif

#ifdef ARDUINO_ARCH_SAM
#define Serial SerialUSB
//Comment out the following line if using the ICSP connector (and modified DABShield).
#define DAB_SPI_BITBANG
#endif

DAB Dab;
//SPI Ports for BIT
#if defined(ARDUINO_ARCH_ESP32)
const byte slaveSelectPin = 12;
#else
const byte slaveSelectPin = 8;
#endif

#ifdef DAB_SPI_BITBANG
#if defined(ARDUINO_ARCH_ESP32)
const byte SCKPin = 18;
const byte MISOPin = 19;
const byte MOSIPin = 23;
#else
const byte SCKPin = 13;
const byte MISOPin = 12;
const byte MOSIPin = 11;
#endif
#endif

bool dabmode = true;
DABTime dabtime;
uint8_t vol = 63;
uint8_t service = 0;
uint8_t freq = 0;

byte rxindex = 0;
char rxdata[32];

void setup() {

  //Intitialise the terminal
  Serial.begin(115200);
  while(!Serial);

  Serial.print(F("                    AVIT DAB 2017-20\n\n")); 
  
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

  Serial.print(F("Initialising.....")); 
  
  //DAB Setup
  Dab.setCallback(ServiceData);
  Dab.speaker(SPEAKER_OUTPUT);
  Dab.begin(0);

  if(Dab.error != 0)
  {
    Serial.print(F("ERROR: "));
    Serial.print(Dab.error);
    Serial.print(F("\nCheck DABShield is Connected and SPI Communications\n"));
  }
  else  
  {
    Serial.print(F("done\n\n")); 
    Help_Menu();
    Serial.print(F("DAB>"));
  }
}

void ServiceData(void)
{
  if (dabmode == true)
  {
    Serial.print(Dab.ServiceData);
    Serial.print(F("\n"));
  }
  else
  {
    char statusstring[72];

	sprintf_P(statusstring, PSTR("%02d/%02d/%04d "), Dab.Days, Dab.Months, Dab.Year);
	Serial.print(statusstring);
	sprintf_P(statusstring, PSTR("%02d:%02d "),Dab.Hours, Dab.Minutes);
	Serial.print(statusstring);
	sprintf_P(statusstring, PSTR("%s "),Dab.ps);
	Serial.print(statusstring);
	sprintf_P(statusstring, PSTR("%s\n"),Dab.ServiceData);
	Serial.print(statusstring);
  }
}

void loop() {
  // put your main code here, to run repeatedly:

#ifdef ANALOG_VOLUME
  static int volumeADCFilter = -1;
  //Analogue Volume control...
  int volumeADC = analogRead(A1);  
  if (volumeADCFilter == -1)
    volumeADCFilter =  volumeADC;
  else
    volumeADCFilter += (volumeADC - volumeADCFilter) / 64;    
  
  byte vollimit = map(volumeADCFilter, 64, 1023, 0, 63);
  if(vollimit != vol)
  {
    vol = vollimit;
    Dab.vol(vol);
  }
#endif

  Dab.task();
  if (Serial.available() > 0)
  {
    rxdata[rxindex] = Serial.read();
    if (rxdata[rxindex] == '\r' || rxdata[rxindex] == '\n')  //return or linefeed
    {
      Serial.print(F("\n"));
      rxdata[rxindex] = '\0';

      process_command(rxdata);
      rxindex = 0;
    }
    else if (rxdata[rxindex] == '\b')  //backspace
    {
      if (rxindex > 0)
      {
        Serial.print(F("\b \b"));
        rxindex--;
      }
    }    
    else  //other char
    {
      Serial.print(rxdata[rxindex]);
      rxindex++;
      if (rxindex >= 32)
      {
        rxindex = 0;
      }
    }
  }
}

void Help_Menu(void)
{
  Serial.print(F("                        HELP\n")); 
  Serial.print(F("PartNo = Si"));
  Serial.print(Dab.PartNo);
  Serial.print(F("\n"));
  
  Serial.print(F("Firmware Version = "));
  Serial.print(Dab.VerMajor);
  Serial.print(F("."));
  Serial.print(Dab.VerMinor);
  Serial.print(F("."));
  Serial.print(Dab.VerBuild);
  Serial.print(F("\n"));
  
  Serial.print(F("________________________________________________________\n\n"));
  if (dabmode == true)
  {
    Serial.print(F("scan                     - scans for valid DAB ensembles\n"));
    Serial.print(F("tune <n>                 - tunes to frequency index\n"));
    Serial.print(F("service <ID>             - tunes to service ID\n"));
    Serial.print(F("info                     - displays the ensemble info\n"));
    Serial.print(F("time                     - displays the current DAB time\n"));
    Serial.print(F("fm                       - FM Mode\n"));
  }
  else
  {
    Serial.print(F("dab                      - DAB Mode\n"));
    Serial.print(F("tune <n>                 - tunes to frequency in kHz (87500 == 87.5MHz)\n"));
    Serial.print(F("seek <up/down>           - seeks to next station\n"));
    Serial.print(F("scan                     - lists available stations\n"));
  }
  Serial.print(F("volume <n>               - set volume 0 - 63\n"));
  Serial.print(F("mono                     - set audio mode to mono\n"));
  Serial.print(F("stereo                   - set audio mode to stereo\n"));
  Serial.print(F("mute <on/off/left/right> - mutes/unmutes audio\n"));
  Serial.print(F("status                   - displays audio/reception info\n"));
  Serial.print(F("help                     - displays this menu\n"));
  if(Dab.Pro == true)
  {
    Serial.print(F("spekaer <on/off/stereo>  - enable/disable/dual speaker config\n"));
  Serial.print(F("bass <n>                 - set bass -12..0..12\n"));
  Serial.print(F("mid <n>                  - set mid -12..0..12\n"));
  Serial.print(F("treble <n>               - set treble -12..0..12\n"));
  }
  Serial.print(F("________________________________________________________\n\n"));
}

void process_command(char *command)
{
  char *cmd;
  cmd = strtok(command, " \r");

  if (strcmp_P(cmd, PSTR("tune")) == 0)
  {
    cmd = strtok(NULL, " \r");
    if(dabmode == true)
    {
      freq = (int)strtol(cmd, NULL, 10);
      service = 0;
      Dab.tune(freq);
      Ensemble_Info();
    }
    else
    {
      uint32_t freqkhz = strtol(cmd, NULL, 10);
      if((freqkhz >= 87500) && (freqkhz <= 107900))
      {
        Dab.tune((uint16_t)(freqkhz/10));
        FM_status();      
      }
      else if((freqkhz >= 8750) && (freqkhz <= 10790))
      {
        Dab.tune((uint16_t)freqkhz);      
        FM_status();
      }
      else
      {
        Serial.print(F("Freq not in range\n"));
      }
    }
  }
  else if (strcmp_P(cmd, PSTR("service")) == 0)
  {
    cmd = strtok(NULL, " \r");
    service = (uint8_t)strtol(cmd, NULL, 10);
    Dab.set_service(service);
    Serial.print(Dab.service[service].Label);
    Serial.print(F("\n"));
  }
  else if (strcmp_P(cmd, PSTR("volume")) == 0)
  {
    cmd = strtok(NULL, " \r");
    vol = (uint8_t)strtol(cmd, NULL, 10);
    Dab.vol(vol);
  }
  else if (strcmp_P(cmd, PSTR("bass")) == 0)
  {
    cmd = strtok(NULL, " \r");
    int8_t level = (int8_t)strtol(cmd, NULL, 10);
    Dab.bass(level);
  }
  else if (strcmp_P(cmd, PSTR("mid")) == 0)
  {
    cmd = strtok(NULL, " \r");
    int8_t level = (int8_t)strtol(cmd, NULL, 10);
    Dab.mid(level);
  }
  else if (strcmp_P(cmd, PSTR("treble")) == 0)
  {
    cmd = strtok(NULL, " \r");
    int8_t level = (int8_t)strtol(cmd, NULL, 10);
    Dab.treble(level);
  }
  else if (strcmp_P(cmd, PSTR("info")) == 0)
  {
    Ensemble_Info();
  }
  else if (strcmp_P(cmd, PSTR("scan")) == 0)
  {
    if(dabmode == true)
    {
      DAB_scan();
    }
    else
    {
      FM_scan();
    }
  }
  else if (strcmp(cmd, "fm") == 0)
  {
    Dab.begin(1);
    dabmode = false;
    Dab.tune((uint16_t)8750);
    Help_Menu();
  }  
  else if (strcmp(cmd, "dab") == 0)
  {
    Dab.begin(0);
    dabmode = true;
    Help_Menu();
  }  
  else if (strcmp(cmd, "mono") == 0)
  {
    Dab.mono(true);
  }
  else if (strcmp(cmd, "stereo") == 0)
  {
    Dab.mono(false);
  }
  else if (strcmp(cmd, "speaker") == 0)
  {
    cmd = strtok(NULL, " \r");
    if(strcmp(cmd, "off") == 0)
    {
      Dab.speaker(SPEAKER_NONE);  
    }
    else if(strcmp(cmd, "on") == 0)
    {
      Dab.speaker(SPEAKER_DIFF);  
    }
    if(strcmp(cmd, "stereo") == 0)
    {
      Dab.speaker(SPEAKER_STEREO);  
    }
  }
  else if (strcmp(cmd, "mute") == 0)
  {
    cmd = strtok(NULL, " \r");
    if(strcmp(cmd, "on") == 0)
    {
      Dab.mute(true, true);  
    }
    else if(strcmp(cmd, "off") == 0)
    {
      Dab.mute(false, false);  
    }    
    else if(strcmp(cmd, "left") == 0)
    {
      Dab.mute(true, false);  
    }    
    else if(strcmp(cmd, "right") == 0)
    {
      Dab.mute(false, true);  
    }    
  }  
  else if (strcmp(cmd, "seek") == 0)
  {
    bool valid = false;
    cmd = strtok(NULL, " \r");
    if(strcmp(cmd, "up") == 0)
    {
      valid = Dab.seek(1, 1);
    }
    else if(strcmp(cmd, "down") == 0)
    {
      valid = Dab.seek(0, 1);
    }
    if(valid == true)
    {
      FM_status();      
    }
  }
  else if (strcmp(cmd, "status") == 0)
  {
    if(dabmode == true)
    { 
       DAB_status(); 
    }
    else
    {
    	FM_status(); 
  	}
  }
  else if (strcmp(cmd, "time") == 0)
  {
    char timestring[16];
    Dab.time(&dabtime);
    sprintf(timestring,"%02d/%02d/%02d ", dabtime.Days,dabtime.Months,dabtime.Year);
    Serial.print(timestring);
    sprintf(timestring,"%02d:%02d\n", dabtime.Hours,dabtime.Minutes);
    Serial.print(timestring);
  } 
  else if (strcmp(cmd, "help") == 0)
  {
    Help_Menu();
  }
  else if (strlen(command) == 0)
  {
    //no command
  }
  else
  {
    Serial.print(F("Unknown command\n"));
  }
  if(dabmode == true)
  {
    Serial.print(F("DAB>"));
  }
  else
  {
    Serial.print(F("FM>"));
  }
}

void Ensemble_Info(void)
{
  char freqstring[32];
  uint8_t i;

  Serial.print(F("\n\nEnsemble Freq "));
  sprintf(freqstring, "%02d\t %03d.", Dab.freq_index, (uint16_t)(Dab.freq_khz(Dab.freq_index) / 1000));
  Serial.print(freqstring);
  sprintf(freqstring, "%03d MHz", (uint16_t)(Dab.freq_khz(Dab.freq_index) % 1000));
  Serial.print(freqstring);

  Serial.print(F("\n"));
  Serial.print(Dab.Ensemble);
  Serial.print(F("\n"));

  Serial.print(F("\nServices: \n"));
  Serial.print(F("ID\tName\n\n"));

  for (i = 0; i < Dab.numberofservices; i++)
  {
    Serial.print(i);
    Serial.print(F(":\t"));
    Serial.print(Dab.service[i].Label);
    Dab.status(Dab.service[i].ServiceID, Dab.service[i].CompID);
    if(Dab.type == SERVICE_AUDIO)
    {
      Serial.print(F("\t dab"));    
      if(Dab.dabplus == true) 
      {
        Serial.print(F("+"));    
      }
    }
    else if(Dab.type == SERVICE_DATA)
    {
      Serial.print(F("\t data"));    
    }
    Serial.print(F("\n"));
  }
  Serial.print(F("\n"));
}

void DAB_scan(void)
{
  uint8_t freq_index;
  char freqstring[32];

  for (freq_index = 0; freq_index < DAB_FREQS; freq_index++)
  {
    Serial.print(F("\nScanning Freq "));
    sprintf(freqstring, "%02d\t %03d.", freq_index, (uint16_t)(Dab.freq_khz(freq_index) / 1000));
    Serial.print(freqstring);
    sprintf(freqstring, "%03d MHz", (uint16_t)(Dab.freq_khz(freq_index) % 1000));
    Serial.print(freqstring);
    Dab.tune(freq_index);
    if(Dab.servicevalid() == true)
    {
      Ensemble_Info();
    }
  }
  Serial.print(F("\n\n"));
}

void DAB_status(void)
{
  char dabstring[64];
  Dab.status();
  Serial.print(Dab.service[service].Label);
  Serial.print(F("\n"));
  //sprintf_P(dabstring,PSTR("PTY = %S (%d)\n"), pgm_read_word(&pty[Dab.pty]), Dab.pty);
  //Serial.print(dabstring); 

  sprintf(dabstring,"Bit Rate = %d kHz, ", Dab.bitrate);
  Serial.print(dabstring);
  sprintf(dabstring,"Sample Rate = %d Hz, ", Dab.samplerate);
  Serial.print(dabstring); 
  //sprintf_P(dabstring,PSTR("Audio Mode = %S (%d)\n"), pgm_read_word(&audiomode[Dab.mode]), Dab.mode);
  //Serial.print(dabstring); 

  sprintf_P(dabstring, PSTR("Serivce Mode = %s\n"), Dab.dabplus == true ? PSTR("dab+") : PSTR("dab"));
  Serial.print(dabstring);
  
  sprintf(dabstring,"RSSI = %d, SNR = %d, Quality = %d%, ", Dab.signalstrength, Dab.snr, Dab.quality);
  Serial.print(dabstring); 
  Serial.print(F("\n"));  
}

void FM_status(void)
{
  char freqstring[32];
  Dab.status();
  sprintf(freqstring, "Freq = %3d.", (uint16_t)Dab.freq / 100);
  Serial.print(freqstring);
  sprintf(freqstring, "%1d MHz : ", (uint16_t)(Dab.freq % 100)/10);
  Serial.print(freqstring);   
  sprintf(freqstring,"RSSI = %d, ",Dab.signalstrength);
  Serial.print(freqstring);
  sprintf(freqstring,"SNR = %d\n",Dab.snr);
  Serial.print(freqstring);
  sprintf(freqstring, "ECC: %x\n", Dab.ECC);
  Serial.print(freqstring);
}

void FM_scan(void)
{
  uint16_t startfreq = Dab.freq;
   
  Dab.vol(0);
  Dab.tune((uint16_t)8750);
  while(Dab.seek(1, 0) == true)
  {
    FM_status();
    if(Dab.freq == 10790)
      break;
  }
  Dab.tune(startfreq);
  Dab.vol(vol);
}

#ifdef DAB_SPI_BITBANG
void DABSpiMsg(unsigned char *data, uint32_t len)
{
  digitalWrite(SCKPin, LOW);
  digitalWrite (slaveSelectPin, LOW);
  for(uint32_t l=0; l<len; l++)
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
