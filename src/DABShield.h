////////////////////////////////////////////////////////////
// DAB Shield Library
// AVIT Research Ltd
// v0.1 11/05/2017 - initial release
// v0.2 07/10/2017 - added support for M0
// v0.3 17/10/2017 - Added Max Services to prevent overflow
// v0.4 20/11/2017 - Added Version Info
// v0.7 27/11/2017 - Added Get DAB Time
// v0.8 06/12/2017 - Added FM/RDS
// v0.9 05/07/2018 - Added Faster DAB Tune
// v0.10 19/09/2018 - Removed Serial.print
// v0.11 06/09/2019 - Added FM seek and valid
// v0.12 17/12/2019 - Corrected DAB Freqs 
// v1.1.3 01/07/2020 - Updated Version Format for Arduino IDE Managed Libraries
///////////////////////////////////////////////////////////
#ifndef DABShield_h
#define DABShield_h

#include "Arduino.h"

const PROGMEM uint32_t dab_freq[] = {174928, 176640, 178352, 180064, 181936, 183648, 185360, 187072, 188928, 190640, 192352, 194064, 195936, 197648, 199360, 201072,
                                     202928, 204640, 206352, 208064, 209936, 211648, 213360, 215072, 216928, 218640, 220352, 222064, 223936, 225648, 227360, 229072,
                                     230784, 232496, 234208, 235776, 237488, 239200
                                    };

#define DAB_FREQS (sizeof(dab_freq) / sizeof(dab_freq[0]))

#define DAB_MAX_SERVICES		16
#define DAB_MAX_SERVICEDATA_LEN	128

typedef struct _Services
{
  uint8_t   Freq;
  uint32_t  ServiceID;
  uint32_t  CompID;
  char      Label[17];
} DABService;

typedef struct _DABTime
{
  uint16_t  Year;
  uint8_t   Months;
  uint8_t	Days;
  uint8_t	Hours;
  uint8_t	Minutes;
  uint8_t	Seconds;
} DABTime;

class DAB {
    friend void irq(void);
  public:
    DAB();

    void task(void);
    void setCallback(void (*ServiceData)(void));
    void begin(void);
	void begin(uint8_t band);
    void tune(uint8_t freq_index);
	void tuneservice(uint8_t freq, uint32_t serviceID, uint32_t CompID);
	void tune(uint16_t freq_kHz);
	bool seek(uint8_t dir, uint8_t wrap);
	bool status(void);
	bool time(DABTime *time);
    void set_service(uint8_t index);
	bool servicevalid(void);
    void vol(uint8_t vol);
    void servicedata(void);
    uint32_t freq_khz(uint8_t index);
    char Ensemble[17];
    char ServiceData[DAB_MAX_SERVICEDATA_LEN];
	
	uint8_t error;

	uint8_t		freq_index;
    DABService	service[DAB_MAX_SERVICES];
    uint8_t		numberofservices = 0;
	uint8_t		ChipRevision;
	uint8_t		RomID;
	uint16_t	PartNo;
	uint8_t		VerMajor;
	uint8_t		VerMinor;
	uint8_t		VerBuild;
	uint8_t		LibMajor;
	uint8_t		LibMinor;

	uint16_t	freq;
	int8_t		signalstrength;
	int8_t		snr;
	bool		valid;

	uint16_t	pi;
	uint8_t		pty;
	char		ps[9];
	
	uint16_t	Year;
	uint8_t		Months;
	uint8_t		Days;
	uint8_t		Hours;
	uint8_t		Minutes;

  private:
    void (*_Callback)(void);
    void DataService(void);
    void get_ensemble_info(void);
    void parse_service_list(void);
    void parse_service_data(void);
	void si468x_get_part_info(void);
	void si468x_get_func_info(void);    
	void si468x_fm_rsq_status(void);
	void si468x_get_fm_rds_status(void);
	uint16_t decode_rds_group(uint16_t blockA, uint16_t blockB, uint16_t blockC, uint16_t blockD);

	uint8_t		last_text_ab_state;
	uint16_t	rdsdata;
};

//Function Must be supplied by the Application for SPI
extern void DABSpiMsg(unsigned char *data, uint32_t len);

#endif

