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
////////////////////////////////////////////////////////////
#include <SPI.h>
#include <DABShield.h>

//#define DAB_SPI_BITBANG

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
const byte slaveSelectPin = 8;

#ifdef DAB_SPI_BITBANG
const byte SCKPin = 13;
const byte MISOPin = 12;
const byte MOSIPin = 11;
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

  Serial.print(F("                    AVIT DAB 2017-19\n\n")); 
  
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
  Dab.begin();

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

    sprintf(statusstring,"%02d/%02d/%04d ", Dab.Days, Dab.Months, Dab.Year);
    Serial.print(statusstring);
    sprintf(statusstring,"%02d:%02d ",Dab.Hours, Dab.Minutes);
    Serial.print(statusstring);
    sprintf(statusstring,"%s ",Dab.ps);
    Serial.print(statusstring);
    sprintf(statusstring,"%s\n",Dab.ServiceData);
    Serial.print(statusstring);
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  Dab.task();
  if (Serial.available() > 0)
  {
    rxdata[rxindex] = Serial.read();
    if (rxdata[rxindex] == '\r')  //return
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
    else if (rxdata[rxindex] == '+')
    {
      if (vol < 63)
      {
        vol++;
        Dab.vol(vol);
      }
    }
    else if (rxdata[rxindex] == '-')
    {
      if (vol > 0)
      {
        vol--;
        Dab.vol(vol);
      }
    }
    else if (rxdata[rxindex] == '<')
    {
      if (Dab.numberofservices > 0)
      {
        if (service > 0)
        {
          service--;
        }
        else
        {
          service = Dab.numberofservices - 1;
        }
        Dab.set_service(service);
        Serial.print(Dab.service[service].Label);
        Serial.print(F("\n"));
      }
    }
    else if (rxdata[rxindex] == '>')
    {
      if (Dab.numberofservices > 0)
      {
        if (service < (Dab.numberofservices - 1))
        {
          service++;
        }
        else
        {
          service = 0;
        }
        Dab.set_service(service);
        Serial.print(Dab.service[service].Label);
        Serial.print(F("\n"));
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
  Serial.print(F("help                     - displays this menu\n"));
  Serial.print(F("________________________________________________________\n\n"));
}

void process_command(char *command)
{
  char *cmd;
  cmd = strtok(command, " \r");
  if (strcmp(cmd, "tune") == 0)
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
  else if (strcmp(cmd, "service") == 0)
  {
    cmd = strtok(NULL, " \r");
    service = (uint8_t)strtol(cmd, NULL, 10);
    Dab.set_service(service);
    Serial.print(Dab.service[service].Label);
    Serial.print(F("\n"));
  }
  else if (strcmp(cmd, "volume") == 0)
  {
    cmd = strtok(NULL, " \r");
    vol = (uint8_t)strtol(cmd, NULL, 10);
    Dab.vol(vol);
  }
  else if (strcmp(cmd, "info") == 0)
  {
    Ensemble_Info();
  }
  else if (strcmp(cmd, "scan") == 0)
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
    FM_status(); 
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
}

void FM_scan(void)
{
  uint16_t startfreq = Dab.freq;
   
  Dab.vol(0);
  Dab.tune((uint16_t)8750);
  while(Dab.seek(1, 0) == true)
  {
    FM_status();
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
