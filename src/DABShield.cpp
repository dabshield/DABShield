////////////////////////////////////////////////////////////
// DAB Shield Library
// AVIT Research Ltd
// v0.1 11/05/2017 - initial release
// v0.2 07/10/2017 - added support for M0
// v0.3 17/10/2017 - Added Max Services to prevent overflow
// v0.4 20/11/2017 - Added Version Info
// v0.5 23/11/2017 - Added Timeouts to waiting functions
// v0.7 27/11/2017 - Added Get DAB Time
// v0.8 06/12/2017 - Added FM/RDS
// v0.9 05/07/2018 - Added Faster DAB Tune
// v0.10 19/09/2018 - Removed Serial.print
// v0.11 06/09/2019 - Added FM seek and valid
// v0.12 17/12/2019 - Corrected DAB Freqs
// v1.1.3 01/07/2020 - Updated Version Format for Arduino IDE Managed Libraries
// v1.2.0 15/07/2020 - Prevent DLS Tag Command being sent as DLS Message
// v1.3.0 15/07/2020 - Added ESP32 D1 R32 Support
// v1.4.0 10/12/2020 - Added Audio Status
// v1.5.0 01/11/2021 - Added DAB Service Type, Dab/Dab+
// v1.5.1 19/03/2022 - Fix ServiceID for AVR (UNO) compiler
// v1.5.2 18/10/2022 - Added EnsembleID and Extended Country Code 
///////////////////////////////////////////////////////////
#include "DABShield.h"
#include "Si468xROM.h"

#define LIBMAJOR	1
#define LIBMINOR	4

#define SI46XX_RD_REPLY 				0x00
#define SI46XX_POWER_UP 				0x01
#define SI46XX_HOST_LOAD				0x04
#define SI46XX_FLASH_LOAD 				0x05
#define SI46XX_LOAD_INIT 				0x06
#define SI46XX_BOOT 					0x07
#define SI46XX_GET_PART_INFO 				0x08
#define SI46XX_GET_SYS_STATE 				0x09
#define SI46XX_READ_OFFSET					0x10

#define SI46XX_GET_FUNC_INFO 				0x12

#define SI46XX_SET_PROPERTY 				0x13
#define SI46XX_GET_PROPERTY 				0x14

#define SI46XX_FM_TUNE_FREQ 				0x30
#define SI46XX_FM_SEEK_START 				0x31
#define SI46XX_FM_RSQ_STATUS 				0x32
#define SI46XX_FM_ACF_STATUS 				0x33
#define SI46XX_FM_RDS_STATUS 				0x34
#define SI46XX_FM_RDS_BLOCKCOUNT 			0x35

#define SI46XX_DAB_GET_DIGITAL_SERVICE_LIST 	        0x80
#define SI46XX_DAB_START_DIGITAL_SERVICE 		0x81
#define SI46XX_GET_DIGITAL_SERVICE_DATA			0x84

#define SI46XX_DAB_TUNE_FREQ 				0xB0
#define SI46XX_DAB_DIGRAD_STATUS 			0xB2
#define SI46XX_DAB_GET_EVENT_STATUS			0xB3
#define SI46XX_DAB_GET_ENSEMBLE_INFO                    0xB4
#define SI46XX_DAB_GET_SERVICE_LINKING_INFO 	        0xB7
#define SI46XX_DAB_SET_FREQ_LIST 			0xB8
#define SI46XX_DAB_GET_ENSEMBLE_INFO 			0xB4
#define SI46XX_GET_TIME						0xBC
#define SI46XX_DAB_GET_AUDIO_INFO 			0xBD
#define SI46XX_DAB_GET_SUBCHAN_INFO 			0xBE
#define SI46XX_DAB_GET_SERVICE_INFO			0xC0

#if defined(ARDUINO_ARCH_ESP32)
const byte interruptPin = 26;
const byte DABResetPin = 14;
const byte PwrEn = 27;
#else
const byte interruptPin = 2;
const byte DABResetPin = 7;
const byte PwrEn = 6;
#endif

#if defined (ARDUINO_AVR_UNO)
#define SPI_BUFF_SIZE	(256)
#else
#define SPI_BUFF_SIZE	(512)
#endif

unsigned char spiBuf[SPI_BUFF_SIZE + 8];
uint8_t command_error;

static void si468x_reset(void);
static void si468x_init_dab(void);
static void si468x_cts(void);
static void si468x_response(void);
static void si468x_responseN(int len);

static void si468x_init_fm(void);
static void si468x_fm_tune_freq(uint16_t freq);
static void si468x_fm_seek(uint8_t dir, uint8_t wrap);

static void si468x_flash_load(uint32_t flash_addr);
static void si468x_boot(void);
static void si468x_power_up(void);
static void si468x_power_down(void);
static void si468x_load_init(void);
static void si468x_host_load(void);
static void si468x_readoffset(uint16_t offset);

static void si468x_flash_set_property(uint16_t property, uint16_t value);
static void si468x_set_property(uint16_t property, uint16_t value);
static void si468x_set_freq_list(void);

static void si468x_dab_tune_freq(uint8_t freq_index);
static void si468x_start_digital_service(uint32_t serviceID, uint32_t compID);
static void si468x_dab_get_ensemble_info(void);
static void si468x_dab_digrad_status(void);
static void si468x_get_digital_service_data(void);
static void si468x_dab_get_event_status(void);
static void si468x_get_digital_service_list(void);
static void si468x_get_digital_service_data(void);
static void si468x_get_audio_info(void);
static void si468x_get_service_info(uint32_t serviceID);
static void si468x_get_subchan_info(uint32_t serviceID, uint32_t compID);
static bool DAB_service_valid(void);
static void DAB_wait_service_list(void);
static void WriteSpiMssg(unsigned char *data, uint32_t len);

DAB::DAB()
{
	LibMajor = LIBMAJOR;
	LibMinor = LIBMINOR;
	bitrate = 0;
	samplerate = 0;
	pinMode(DABResetPin, OUTPUT);
	pinMode(PwrEn,OUTPUT);
	pinMode(interruptPin, INPUT_PULLUP);
}

void DAB::task(void)
{
	if(digitalRead(interruptPin) == LOW)
	{
		DataService();
	}
}

void DAB::setCallback(void (*ServiceData)(void))
{
	_Callback = ServiceData;
}

void DAB::DataService(void)
{
	si468x_response();
	uint8_t status0 = spiBuf[1];
	uint8_t status1 = spiBuf[2];
	//DSRVINT
	if ((status0 & 0x10) == 0x10)
	{
		si468x_get_digital_service_data();
		si468x_responseN(20);
		uint16_t len = spiBuf[19] + (spiBuf[20] << 8);
		if (len < (SPI_BUFF_SIZE - 24))
		{
			si468x_responseN(len + 24);
		}
		else
		{
			si468x_responseN(SPI_BUFF_SIZE - 1);
		}
		parse_service_data();
		_Callback();
	}
	//RDSINT
	if ((status0 & 0x04) == 0x04)
	{
		si468x_get_fm_rds_status();
		if(rdsdata != 0)
			_Callback();
	}
}

void DAB::begin(void)
{
	freq_index = -1;

	si468x_reset();
	si468x_init_dab();
	si468x_get_part_info();
	si468x_get_func_info();
	dab = true;
	error = command_error;
}

void DAB::begin(uint8_t band)
{
	freq_index = -1;
	
	si468x_reset();
	if(band == 0)
	{
		si468x_init_dab();
		si468x_get_part_info();
		si468x_get_func_info();
		dab = true;
	}
	else if(band == 1)
	{
		si468x_init_fm();
		si468x_get_part_info();
		si468x_get_func_info();
		dab = false;
	}
	error = command_error;
}

void DAB::tune(uint8_t freq)
{
	numberofservices = 0;
	freq_index = freq;
	ServiceData[0] = '\0';
	Ensemble[0] = '\0';
	si468x_dab_tune_freq(freq_index);
	if(command_error == 0)
	{
		get_ensemble_info();
	}
	else
	{
		//failed to tune
		freq_index = -1;
	}
	error = command_error;
}

void DAB::tuneservice(uint8_t freq, uint32_t serviceID, uint32_t CompID)
{
	uint32_t timeout;
	numberofservices = 0;

	ServiceData[0] = '\0';
	Ensemble[0] = '\0';

	if(freq_index != freq)
	{
		freq_index = freq;
		si468x_dab_tune_freq(freq_index);
	}

	DAB::serviceID = serviceID;
	DAB::compID = CompID;

	timeout = 1000;
	do
	{
		delay(4);
		si468x_start_digital_service(serviceID, CompID);
		timeout--;
		if(timeout == 0)
		{
			break;
		}
	}
	while (command_error != 0);
}

void DAB::tune(uint16_t freq_kHz)
{
	si468x_fm_tune_freq(freq_kHz);
	si468x_fm_rsq_status();

	uint8_t i;
	for(i=0; i<9; i++) {ps[i] = 0;}
	for(i=0; i<DAB_MAX_SERVICEDATA_LEN; i++) {ServiceData[i] = 0;}
	error = command_error;
}

bool DAB::seek(uint8_t dir, uint8_t wrap)
{
	si468x_fm_seek(dir, wrap);
	si468x_fm_rsq_status();
	uint8_t i;
	for(i=0; i<9; i++) {ps[i] = 0;}
	for(i=0; i<DAB_MAX_SERVICEDATA_LEN; i++) {ServiceData[i] = 0;}
	error = command_error;
	return valid;
}

bool DAB::status(void)
{
	if(dab == true)
	{
		get_digrad_status();
		get_audio_info();
		get_service_info(serviceID);
		get_subchan_info(serviceID, compID);
		return true;
	}
	else
	{
		si468x_fm_rsq_status();
		error = command_error;
		return valid;
	}
}

bool DAB::status(uint32_t ServiceID, uint32_t CompID)
{
	if(dab == true)
	{
		get_digrad_status();
		get_audio_info();
		get_service_info(ServiceID);
		get_subchan_info(ServiceID, CompID);
		return true;
	}
}

void DAB::set_service(uint8_t index)
{
	si468x_start_digital_service(service[index].ServiceID, service[index].CompID);
	serviceID = service[index].ServiceID;
	compID = service[index].CompID;
	error = command_error;
}

bool DAB::servicevalid(void)
{
	return DAB_service_valid();
}

void DAB::vol(uint8_t vol)
{
	si468x_set_property(0x0300, (vol & 0x3F));
	error = command_error;
}

uint32_t DAB::freq_khz(uint8_t index)
{
	return pgm_read_dword_near(dab_freq + index);
}

static bool DAB_service_valid(void)
{
	si468x_dab_digrad_status();
	si468x_responseN(23);

	if (((spiBuf[6] & 0x01) == 0x01) && (spiBuf[7] > 0x20) && (spiBuf[9] > 25))
	{
		return true;
	}
	else
	{
		return false;
	}
}

void DAB_wait_service_list(void)
{
	uint32_t	timeout;
	timeout = 1000;
	do
	{
		delay(4);
		si468x_dab_get_event_status();
		si468x_responseN(8);
		timeout--;
		if(timeout == 0)
		{
			command_error |= 0x80;
			break;
		}
	}
	while ((spiBuf[6] & 0x01) == 0x00); //Service List Ready ?
}

void DAB::get_ensemble_info(void)
{
	si468x_dab_digrad_status();
	si468x_responseN(23);
	if (DAB_service_valid() == true)
	{
		DAB_wait_service_list();

		si468x_dab_get_ensemble_info();
		si468x_responseN(29);

		EnsembleID = spiBuf[5] + (spiBuf[6] << 8);

		uint8_t i;
		for (i = 0; i < 16; i++)
		{
			Ensemble[i] = ((char)spiBuf[7 + i]);
		}
		Ensemble[16] = '\0';

		ECC = spiBuf[23];

		si468x_get_digital_service_list();
		si468x_responseN(6);

		int16_t len = spiBuf[5] + (spiBuf[6] << 8) + 2;

		if(len < SPI_BUFF_SIZE)
		{
			si468x_responseN(len + 4);
			parse_service_list();
		}
		else
		{
			uint16_t offset = 0;
			bool first = true;
			while(len >= SPI_BUFF_SIZE)
			{
				si468x_readoffset(offset);
				si468x_responseN(SPI_BUFF_SIZE + 4);
				if(parse_service_list(first, &(spiBuf[5]), SPI_BUFF_SIZE) == true)
				{
					len = 0;
				}
				else
				{
					len -= SPI_BUFF_SIZE;
				}
				first = false;
				offset += SPI_BUFF_SIZE;
			}
			if(len)
			{
				si468x_readoffset(offset);
				si468x_responseN(len + 4);
				parse_service_list(first, &(spiBuf[5]), len);
			}
		}
	}
	else
	{
		//No services
	}
}

void DAB::get_digrad_status(void)
{
	si468x_dab_digrad_status();
	si468x_responseN(22);
	
	signalstrength = spiBuf[7];
	snr = spiBuf[8];
	quality = spiBuf[9];
}

void DAB::get_audio_info(void)
{
	si468x_get_audio_info();
	si468x_responseN(19);

	bitrate = spiBuf[5] + (spiBuf[6] << 8);
	samplerate = spiBuf[7] + (spiBuf[8] << 8);
	mode = (AudioMode)(spiBuf[9] & 0x3);
}

void DAB::get_service_info(uint32_t serviceID)
{
	si468x_get_service_info(serviceID);
	si468x_responseN(19);

	pty = (spiBuf[5] >> 1) & 0x1F;
}

void DAB::get_subchan_info(uint32_t serviceID, uint32_t compID)
{
	si468x_get_subchan_info(serviceID, compID);
	si468x_responseN(12);

	dabplus = false;
	type = SERVICE_NONE;
	switch(spiBuf[5])
	{
	case 0:
		type = SERVICE_AUDIO;
		break;
	case 1:
	case 2:
	case 3:
		type = SERVICE_DATA;
		break;
	case 4:
		dabplus = true;
		type = SERVICE_AUDIO;
		break;
	case 5:
		type = SERVICE_AUDIO;
		break;
	case 6:
	case 7:
	case 8:
		type = SERVICE_DATA;
		break;
	default:
		break;
	}
}


static void si468x_reset(void)
{
	digitalWrite(PwrEn, HIGH);
	delay(100);	

	digitalWrite(DABResetPin, LOW);  //Reset LOW
	delay(100);
	digitalWrite(DABResetPin, HIGH); //Reset HIGH
	delay(100);

	si468x_power_up();
	si468x_load_init();
	si468x_host_load();
	si468x_load_init();

	//Set SPI Clock to 10 MHz
	si468x_flash_set_property(0x0001, 10000);
}

static void si468x_init_dab()
{
	si468x_flash_load(0x6000);
	si468x_boot();
	si468x_set_freq_list();

	//Set up INTB
	si468x_set_property(0x0000, 0x0010);

	si468x_set_property(0x1710, 0xF83E);
	si468x_set_property(0x1711, 0x01A4);
	si468x_set_property(0x1712, 0x0001);

	si468x_set_property(0x8100, 0x0001);	//enable DSRVPCKTINT
	si468x_set_property(0xb400, 0x0007);	//enable XPAD data	
}

static void si468x_init_fm()
{
	si468x_flash_load(0x86000);
	si468x_boot();

	//FM Seek Settings:
	//FM_VALID_SNR_THRESHOLD
	si468x_set_property(0x3204, 10);
	//FM_VALID_RSSI_THRESHOLD,
	si468x_set_property(0x3202, 17);
	//FM_VALID_MAX_TUNE_ERROR
	si468x_set_property(0x3200, 114);

	//Set up INTB
	si468x_set_property(0x0000, 0x0004);

	si468x_set_property(0x1710, 0xF83E);
	si468x_set_property(0x1711, 0x01A4);	
	si468x_set_property(0x1712, 0x0001);

	si468x_set_property(0x3900, 0x0001);
	si468x_set_property(0x3C00, 0x0001);
	si468x_set_property(0x3C01, 0x0010);
	si468x_set_property(0x3C02, 0x0001);
}

void DAB::si468x_get_part_info(void)
{
	spiBuf[0] = SI46XX_GET_PART_INFO;
	spiBuf[1] = 0x00;
	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
	si468x_responseN(10);
	ChipRevision = spiBuf[5];
	RomID = spiBuf[6];
	PartNo = (spiBuf[10] << 8) + spiBuf[9];
}

void DAB::si468x_get_func_info(void)
{
	spiBuf[0] = SI46XX_GET_FUNC_INFO;
	spiBuf[1] = 0x00;
	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
	si468x_responseN(8);
	VerMajor = spiBuf[5];
	VerMinor = spiBuf[6];
	VerBuild = spiBuf[7];
}

void DAB::si468x_fm_rsq_status(void)
{
	spiBuf[0] = SI46XX_FM_RSQ_STATUS;
	spiBuf[1] = 0x00;
	WriteSpiMssg(spiBuf, 2);

	si468x_cts();
	si468x_responseN(12);

	valid = (spiBuf[6] & 0x01) == 0x01 ? true : false;  
	freq = spiBuf[7] + ((uint16_t)spiBuf[8] << 8);;
	signalstrength = spiBuf[10];
	snr = spiBuf[11];
}

void DAB::si468x_get_fm_rds_status(void)
{
	uint16_t	blockA;
	uint16_t	blockB;
	uint16_t	blockC;
	uint16_t	blockD;

	spiBuf[0] = SI46XX_FM_RDS_STATUS;
	spiBuf[1] = 0x01;
	WriteSpiMssg(spiBuf, 2);

	si468x_cts();
	si468x_responseN(20);

	if((spiBuf[6] & 0x08) == 0x08)
		pi = spiBuf[9] + ((uint16_t)spiBuf[10] << 8);
	if((spiBuf[6] & 0x10) == 0x10)
		pty = spiBuf[7] & 0x1f;

	blockA = spiBuf[13] + ((uint16_t)spiBuf[14] << 8);
	blockB = spiBuf[15] + ((uint16_t)spiBuf[16] << 8);
	blockC = spiBuf[17] + ((uint16_t)spiBuf[18] << 8);
	blockD = spiBuf[19] + ((uint16_t)spiBuf[20] << 8);
	rdsdata = decode_rds_group(blockA, blockB, blockC, blockD);
}

bool DAB::time(DABTime *time)
{
	bool ret = 1;

	if(time != NULL)
	{
		spiBuf[0] = SI46XX_GET_TIME;
		spiBuf[1] = 0x00;
		WriteSpiMssg(spiBuf, 2);

		si468x_cts();
		si468x_responseN(12);

		time->Year = spiBuf[5] + ((uint16_t)spiBuf[6] << 8);
		time->Months = spiBuf[7];
		time->Days = spiBuf[8];
		time->Hours = spiBuf[9];
		time->Minutes = spiBuf[10];
		time->Seconds = spiBuf[11];
		ret = 0;
	}
	return ret;
}

void DAB::mono(bool enable)
{
	si468x_set_property(0x0302, enable ? 0x01 : 0x00);
}

void DAB::mute(bool left, bool right)
{
	uint8_t mute = 0;
	if(left == true)
		mute = 0x01;
	if(right == true)
		mute |= 0x02;

	si468x_set_property(0x0301, mute);
}


static void si468x_cts(void)
{
	uint32_t timeout;
	command_error = 0;
	timeout = 1000;
	do
	{
		delay(4);
		si468x_response();
		timeout--;
		if(timeout == 0)
		{
			command_error = 0x80;
			break;
		}
	}
	while ((spiBuf[1] & 0x80) == 0x00);

	if ((spiBuf[1] & 0x40) == 0x40)
	{
		si468x_responseN(5);
		command_error = 0x80 | spiBuf[5];
	}
}

static void si468x_response(void)
{
	si468x_responseN(4);
}


static void si468x_responseN(int len)
{
	int i;

	for (i = 0; i < len + 1; i++)
	{
		spiBuf[i] = 0;
	}

	DABSpiMsg(spiBuf, len + 1);
}


static void si468x_readoffset(uint16_t offset)
{
	spiBuf[0] = SI46XX_READ_OFFSET;
	spiBuf[1] = 0x00;
	spiBuf[2] = offset & 0xff;
	spiBuf[3] = (offset >> 8) & 0xff;
	WriteSpiMssg(spiBuf, 4);
	si468x_cts();
}

static void si468x_power_up(void)
{
	spiBuf[0] = SI46XX_POWER_UP;
	spiBuf[1] = 0x00;
	spiBuf[2] = 0x17;
	spiBuf[3] = 0x48;
	spiBuf[4] = 0x00;
	spiBuf[5] = 0xf8;
	spiBuf[6] = 0x24;
	spiBuf[7] = 0x01;
	spiBuf[8] = 0x1F;
	spiBuf[9] = 0x10;
	spiBuf[10] = 0x00;
	spiBuf[11] = 0x00;
	spiBuf[12] = 0x00;
	spiBuf[13] = 0x18;
	spiBuf[14] = 0x00;
	spiBuf[15] = 0x00;

	WriteSpiMssg(spiBuf, 16);

	si468x_cts();
}

static void si468x_load_init(void)
{
	spiBuf[0] = SI46XX_LOAD_INIT;
	spiBuf[1] = 0x00;

	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
}

static void si468x_host_load(void)
{
	uint16_t index;
	uint16_t patchsize;
	uint16_t i;

	patchsize = sizeof(rom_patch_016);
	index = 0;

	while (index < patchsize)
	{
		spiBuf[0] = SI46XX_HOST_LOAD;
		spiBuf[1] = 0x00;
		spiBuf[2] = 0x00;
		spiBuf[3] = 0x00;
		for (i = 4; (i < SPI_BUFF_SIZE) && (index < patchsize); i++)
		{
			spiBuf[i] = pgm_read_byte_near(rom_patch_016 + index);
			index++;
		}
		WriteSpiMssg(spiBuf, i);
		si468x_cts();
	}
}

static void si468x_flash_load(uint32_t flash_addr)
{
	spiBuf[0] = SI46XX_FLASH_LOAD;
	spiBuf[1] = 0x00;
	spiBuf[2] = 0x00;
	spiBuf[3] = 0x00;

	spiBuf[4] = (flash_addr  & 0xff);
	spiBuf[5] = ((flash_addr >> 8)  & 0xff);
	spiBuf[6] = ((flash_addr >> 16)  & 0xff);
	spiBuf[7] = ((flash_addr >> 24) & 0xff);

	spiBuf[8] = 0x00;
	spiBuf[9] = 0x00;
	spiBuf[10] = 0x00;
	spiBuf[11] = 0x00;

	WriteSpiMssg(spiBuf, 12);
	si468x_cts();
}

static void si468x_boot(void)
{
	spiBuf[0] = SI46XX_BOOT;
	spiBuf[1] = 0x00;

	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
}

static void si468x_flash_set_property(uint16_t property, uint16_t value)
{
	spiBuf[0] = SI46XX_FLASH_LOAD;
	spiBuf[1] = 0x10;
	spiBuf[2] = 0x0;
	spiBuf[3] = 0x0;

	spiBuf[4] = property & 0xFF;		//SPI CLock
	spiBuf[5] = (property >> 8) & 0xFF;
	spiBuf[6] = value & 0xFF;
	spiBuf[7] = (value >> 8) & 0xFF;

	WriteSpiMssg(spiBuf, 8);
	si468x_cts();
}

static void si468x_set_property(uint16_t property, uint16_t value)
{
	spiBuf[0] = SI46XX_SET_PROPERTY;
	spiBuf[1] = 0x00;

	spiBuf[2] = property & 0xFF;
	spiBuf[3] = (property >> 8) & 0xFF;
	spiBuf[4] = value & 0xFF;
	spiBuf[5] = (value >> 8) & 0xFF;

	WriteSpiMssg(spiBuf, 6);
	si468x_cts();
}

static void si468x_fm_tune_freq(uint16_t freq)
{
	uint32_t timeout;
	
	spiBuf[0] = SI46XX_FM_TUNE_FREQ;
	spiBuf[1] = 0x00;

	spiBuf[2] = freq & 0xFF;
	spiBuf[3] = (freq >> 8) & 0xFF;
	spiBuf[4] = 0x00;
	spiBuf[5] = 0x00;
	spiBuf[6] = 0x00;

	WriteSpiMssg(spiBuf, 7);
	si468x_cts();

	timeout = 1000;
	do
	{
		delay(4);
		si468x_response();
		timeout--;
		if(timeout == 0)
		{
			command_error |= 0x80;
			break;
		}
	}
	while ((spiBuf[1] & 0x01) == 0); //STCINT
}

static void si468x_fm_seek(uint8_t dir, uint8_t wrap)
{
	uint32_t timeout;

	spiBuf[0] = SI46XX_FM_SEEK_START;
	spiBuf[1] = 0x00;
	spiBuf[2] = ((dir == 0) ? 0 : 2) | ((wrap == 0) ? 0 : 1);
	spiBuf[3] = 0x00;
	spiBuf[4] = 0x00;
	spiBuf[5] = 0x00;

	WriteSpiMssg(spiBuf, 6);
	si468x_cts();

	timeout = 10000;
	do
	{
		delay(4);
		si468x_response();
		timeout--;
		if(timeout == 0)
		{
			command_error |= 0x80;
			break;
		}
	}
	while ((spiBuf[1] & 0x01) == 0); //STCINT
}


static void si468x_set_freq_list(void)
{
	uint8_t i;
	spiBuf[0] = SI46XX_DAB_SET_FREQ_LIST;
	spiBuf[1] = DAB_FREQS;
	spiBuf[2] = 0x00;
	spiBuf[3] = 0x00;

	for (i = 0; i < DAB_FREQS; i++)
	{
		uint32_t  freq = pgm_read_dword_near(dab_freq + i);
		spiBuf[4 + (i * 4)] = (freq  & 0xff);
		spiBuf[5 + (i * 4)] = ((freq >> 8)  & 0xff);
		spiBuf[6 + (i * 4)] = ((freq >> 16)  & 0xff);
		spiBuf[7 + (i * 4)] = ((freq >> 24) & 0xff);
	}
	WriteSpiMssg(spiBuf, 4 + (i * 4));
	si468x_cts();
}

static void si468x_dab_tune_freq(uint8_t freq_index)
{
	uint32_t timeout;
	spiBuf[0] = SI46XX_DAB_TUNE_FREQ;
	spiBuf[1] = 0x00;
	spiBuf[2] = freq_index;
	spiBuf[3] = 0x00;
	spiBuf[4] = 0x00;
	spiBuf[5] = 0x00;
	WriteSpiMssg(spiBuf, 6);
	si468x_cts();

	timeout = 1000;
	do
	{
		delay(4);
		si468x_response();
		timeout--;
		if(timeout == 0)
		{
			command_error |= 0x80;
			break;
		}
	}
	while ((spiBuf[1] & 0x01) == 0); //STCINT
}


static void si468x_start_digital_service(uint32_t serviceID, uint32_t compID)
{
	spiBuf[0] = SI46XX_DAB_START_DIGITAL_SERVICE;
	spiBuf[1] = 0x00;
	spiBuf[2] = 0x00;
	spiBuf[3] = 0x00;
	spiBuf[4] = serviceID & 0xff;
	spiBuf[5] = (serviceID >> 8) & 0xff;
	spiBuf[6] = (serviceID >> 16) & 0xff;
	spiBuf[7] = (serviceID >> 24) & 0xff;
	spiBuf[8] = compID & 0xff;
	spiBuf[9] = (compID >> 8) & 0xff;
	spiBuf[10] = (compID >> 16) & 0xff;
	spiBuf[11] = (compID >> 24) & 0xff;
	WriteSpiMssg(spiBuf, 12);
	si468x_cts();
}

static void si468x_dab_get_ensemble_info(void)
{
	spiBuf[0] = SI46XX_DAB_GET_ENSEMBLE_INFO;
	spiBuf[1] = 0x00;
	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
}

static void si468x_dab_digrad_status(void)
{
	spiBuf[0] = SI46XX_DAB_DIGRAD_STATUS;
	spiBuf[1] = 0x09; //Clear Interrupts: DIGRAD_ACK | STC_ACK
	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
}

static void si468x_get_digital_service_data(void)
{
	spiBuf[0] = SI46XX_GET_DIGITAL_SERVICE_DATA;
	spiBuf[1] = 0x01;
	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
}

static void si468x_dab_get_event_status(void)
{
	spiBuf[0] = SI46XX_DAB_GET_EVENT_STATUS;
	spiBuf[1] = 0x00;
	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
}

static void si468x_get_digital_service_list(void)
{
	spiBuf[0] = SI46XX_DAB_GET_DIGITAL_SERVICE_LIST;
	spiBuf[1] = 0x00;
	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
}

static void si468x_get_audio_info(void)
{
	spiBuf[0] = SI46XX_DAB_GET_AUDIO_INFO;
	spiBuf[1] = 0x00;
	WriteSpiMssg(spiBuf, 2);
	si468x_cts();
}

static void si468x_get_service_info(uint32_t serviceID)
{
	spiBuf[0] = SI46XX_DAB_GET_SERVICE_INFO;
	spiBuf[1] = 0x00;
	spiBuf[2] = 0x00;
	spiBuf[3] = 0x00;
	spiBuf[4] = serviceID & 0xFF;
	spiBuf[5] = (serviceID >> 8) & 0xFF;
	spiBuf[6] = (serviceID >> 16) & 0xFF;
	spiBuf[7] = (serviceID >> 24) & 0xFF;
	WriteSpiMssg(spiBuf, 8);
	si468x_cts();
}

static void si468x_get_subchan_info(uint32_t serviceID, uint32_t compID)
{
	spiBuf[0] = SI46XX_DAB_GET_SUBCHAN_INFO;
	spiBuf[1] = 0x00;
	spiBuf[2] = 0x00;
	spiBuf[3] = 0x00;
	spiBuf[4] = serviceID & 0xFF;
	spiBuf[5] = (serviceID >> 8) & 0xFF;
	spiBuf[6] = (serviceID >> 16) & 0xFF;
	spiBuf[7] = (serviceID >> 24) & 0xFF;
	spiBuf[8] = compID & 0xFF;
	spiBuf[9] = (compID >> 8) & 0xFF;
	spiBuf[10] = (compID >> 16) & 0xFF;
	spiBuf[11] = (compID >> 24) & 0xFF;

	WriteSpiMssg(spiBuf, 12);
	si468x_cts();
}

static void WriteSpiMssg(unsigned char *data, uint32_t len)
{
	DABSpiMsg(data, len);
}

void DAB::parse_service_list(void)
{
	uint16_t i, j, offset;

	uint32_t serviceID;
	uint32_t componentID;
	uint16_t numberofcomponents;

	uint16_t listsize;
	uint16_t version;

	listsize = spiBuf[5] + (spiBuf[6] << 8);
	version = spiBuf[7] + (spiBuf[8] << 8);
	(void)listsize; //not used
	(void)version;	//not used
	
	numberofservices = spiBuf[9];
	if(numberofservices > DAB_MAX_SERVICES)
	{
		numberofservices = DAB_MAX_SERVICES;
	}

	offset = 13;

	for (i = 0; i < numberofservices; i++)
	{
		serviceID = spiBuf[offset + 3];
		serviceID <<= 8;
		serviceID += spiBuf[offset + 2];
		serviceID <<= 8;
		serviceID += spiBuf[offset + 1];
		serviceID <<= 8;
		serviceID += spiBuf[offset];
		componentID = 0;

		numberofcomponents = spiBuf[offset + 5] & 0x0F;

		for (j = 0; j < 16; j++)
		{
			service[i].Label[j] = spiBuf[offset + 8 + j];
		}
		service[i].Label[16] = '\0';

		offset += 24;

		for (j = 0; j < numberofcomponents; j++)
		{
			if (j == 0)
			{
				componentID = spiBuf[offset + 3];
				componentID <<= 8;
				componentID += spiBuf[offset + 2];
				componentID <<= 8;
				componentID += spiBuf[offset + 1];
				componentID <<= 8;
				componentID += spiBuf[offset];
			}
			offset += 4;
		}

		service[i].ServiceID = serviceID;
		service[i].CompID = componentID;
	}
}

//Parse by sections...
bool DAB::parse_service_list(bool first, uint8_t* data, uint16_t len)
{
	//Parse the data by byte in blocks
	static uint16_t listsize;
	static uint8_t parse_service_state = 0;
	static uint8_t numberofservices;
	static uint8_t serviceindex = 0;
	static uint8_t labelindex = 0;
	static uint8_t numberofcomponents;
	static uint8_t compindex = 0;
	static uint32_t serviceID;
	static uint32_t componentID;	
	static uint16_t version;

	uint8_t byte;
	uint16_t i;
	bool done = false;

	if(first == true)
	{
		parse_service_state = 0;
	}
	
	i = 0;
	while((i < len) && (done == false))
	{	
		byte = data[i];
		i++;
		
		switch(parse_service_state)
		{
		case 0: //size LSB
			listsize = byte;
			parse_service_state = 1;
			break;
		case 1: //size MSB
			listsize += (byte << 8);
			parse_service_state = 2;
			break;
		case 2: //version LSB
			version = byte;
			parse_service_state = 3;
			break;
		case 3: //version MSB
			version += (byte << 8);
			parse_service_state = 4;
			break;
		case 4: //Num of Services
			serviceindex = 0;
			numberofservices = byte;
			if(numberofservices > DAB_MAX_SERVICES)
				DAB::numberofservices = DAB_MAX_SERVICES;
			else
				DAB::numberofservices = numberofservices;						
			parse_service_state = 5;
			break;
		case 5:
		case 6:
		case 7:
			parse_service_state++;
			serviceindex = 0;
			break;

		case 8: //ServiceID
			serviceID = byte;
			parse_service_state = 9;
			break;
		case 9: //ServiceID
			serviceID += ((uint32_t)byte << 8);
			parse_service_state = 10;
			break;
		case 10: //ServiceID
			serviceID += ((uint32_t)byte << 16);
			parse_service_state = 11;
			break;
		case 11: //ServiceID
			serviceID += ((uint32_t)byte << 24);		
			if(serviceindex < DAB_MAX_SERVICES)
			{
				service[serviceindex].ServiceID = serviceID;
			}					
			parse_service_state = 12;
			break;
		
		case 12: // RFU, SrvLinging, Pty, P/D Flag
			parse_service_state = 13;
			break;
		case 13: //Local, DAid, NUM_COMP
			numberofcomponents = byte & 0x0F;
			parse_service_state = 14;
			break;
		case 14: // RFU, SICharset
			parse_service_state = 15;
			break;
		case 15: // Align
			parse_service_state = 16;
			labelindex = 0;
			break;
		case 16: //Label...
			if(serviceindex < DAB_MAX_SERVICES)
			{
				service[serviceindex].Label[labelindex] = byte;
			}
			labelindex++;
			if(labelindex >= 16)
			{
				if(serviceindex < DAB_MAX_SERVICES)
				{
					service[serviceindex].Label[labelindex] = '\0';
				}
				if(numberofcomponents > 0)
				{
					compindex = 0;
					parse_service_state = 17;
				}
				else
				{
					serviceindex++;
					if((serviceindex >= numberofservices) || (serviceindex >= DAB_MAX_SERVICES))
					{
						//Done...
						done = true;
						parse_service_state = 0;
					}
					else
					{						
						parse_service_state = 8;
					}
				}
			}
			break;
		case 17: //componentID
			componentID = byte;
			parse_service_state = 18;
			break;
		case 18: //componentID
			componentID += ((uint32_t)byte << 8);
			parse_service_state = 19;
			break;
		case 19: //componentID
			componentID += ((uint32_t)byte << 16);
			parse_service_state = 20;
			break;
		case 20: //componentID
			componentID += ((uint32_t)byte << 24);

			//currently only support first component...
			if(compindex == 0)
			{
				if(serviceindex < DAB_MAX_SERVICES)
				{
					service[serviceindex].CompID = componentID;	
				}					
			}
								
			compindex++;
			if(compindex >= numberofcomponents)
			{
				serviceindex++;
				if((serviceindex >= numberofservices) || (serviceindex >= DAB_MAX_SERVICES))					
				{
					//Done...	
					done = true;									
					parse_service_state = 0;
				}
				else
				{
					parse_service_state = 8;
				}
			}
			else
			{
				parse_service_state = 17;
			}
			break;
		}
	}
	return done;
}

void DAB::parse_service_data(void)
{
	uint8_t	buff_count;
	uint8_t	srv_state;
	uint8_t	data_src;
	uint8_t	DSCty;
	uint32_t	service_id;
	uint32_t	comp_id;
	uint16_t	rfu;
	uint16_t	byte_count;
	uint16_t	seg_num;
	uint16_t	num_segs;

	uint8_t j;

	buff_count = spiBuf[6];
	srv_state = spiBuf[7];
	data_src = (spiBuf[8] >> 6) & 0x03;
	DSCty = spiBuf[8] & 0x3F;
	service_id = spiBuf[9] + ((uint32_t)spiBuf[10] << 8) + ((uint32_t)spiBuf[11] << 16) + ((uint32_t)spiBuf[12] << 24) ;
	comp_id = spiBuf[13] + ((uint32_t)spiBuf[14] << 8) + ((uint32_t)spiBuf[15] << 16) + ((uint32_t)spiBuf[16] << 24) ;
	rfu = spiBuf[17] + (spiBuf[18] << 8);
	byte_count = spiBuf[19] + (spiBuf[20] << 8);
	seg_num = spiBuf[21] + (spiBuf[22] << 8);
	num_segs = spiBuf[23] + (spiBuf[24] << 8);

	(void)buff_count;
	(void)srv_state;
	(void)DSCty;
	(void)service_id;
	(void)comp_id;
	(void)rfu;
	(void)seg_num;
	(void)num_segs;

	if (data_src == 0x02) //DLS/DL+ over PAD for DLS services
	{
		uint8_t header1;
		uint8_t header2;
		
		header1 = spiBuf[25];
		header2 = spiBuf[26];

		if((header1 & 0x10) == 0x10)
		{
			//DLS Tags Command
		}
		else
		{				
			//DLS Message
			if (byte_count > DAB_MAX_SERVICEDATA_LEN)
			{
				byte_count = DAB_MAX_SERVICEDATA_LEN;
			}
			for (j = 0; j < (byte_count - 2 - 1); j++)
			{
				ServiceData[j] = (char)spiBuf[27 + j];
			}
			ServiceData[j] = '\0';
		}
	}
	else
	{
		//NON RAD/DLS
	}
}

#define RDS_GROUP_0A	0
#define RDS_GROUP_0B	1
#define RDS_GROUP_1A	2
#define RDS_GROUP_2A	4
#define RDS_GROUP_2B	5
#define RDS_GROUP_4A	8

uint16_t DAB::decode_rds_group(uint16_t blockA, uint16_t blockB, uint16_t blockC, uint16_t blockD)
{
	(void)blockA;	//not used

	uint8_t ret;
	uint8_t (*rt_buffer)[64] = (uint8_t (*)[64])service;	//Reuse the DAB service memory
	uint8_t (*ps_buffer)[8] = (uint8_t (*)[8])Ensemble;		//Reuse the DAB service data memory
	uint8_t	rds_group;
	uint8_t offset;
	ret = 0;

	rds_group = (blockB >> 11);
	switch(rds_group)
	{
	case RDS_GROUP_0A:
	case RDS_GROUP_0B:
		offset = blockB & 0x03;
		//copy last data into last buffer
		ps_buffer[1][(offset * 2)+0] = ps_buffer[0][(offset * 2)+0];
		ps_buffer[1][(offset * 2)+1] = ps_buffer[0][(offset * 2)+1];
		//load new data into current buffer
		ps_buffer[0][(offset * 2)+0] = blockD >> 8;
		ps_buffer[0][(offset * 2)+1] = blockD;
		if(offset == 3)
		{
			uint8_t i;
			//look for a difference between consecative text
			for(i=0;i<8;i++)
			{
				if(ps_buffer[0][i] != ps_buffer[1][i])
					break;
			}
			//if no difference found check to see if new Radio Text
			if(i==8)
			{
				for(i=0;i<8;i++)
				{
					if(ps[i] != ps_buffer[0][i])
						break;
				}
				//new Radio Text update buffer
				if(i!=8)
				{
					for(i=0;i<8;i++)
						ps[i] = ps_buffer[0][i];
					ret |= 0x0001;
					ps[8] = '\0';
				}
			}				
		}
		ps[8] = '\0';
		break;

	case RDS_GROUP_1A:
		if ((blockC & 0x7000) == 0x0000)
			ECC = blockC & 0xFF;
		break;

	case RDS_GROUP_2A:
	case RDS_GROUP_2B:
	{
		uint8_t offset;
		uint8_t i;
		uint8_t text_ab_state;
		uint8_t same;
		uint8_t text_size;
		uint8_t first_zero;

		text_ab_state = (blockB & 0x0010) ? 0x80 : 0x00;
		text_ab_state += rds_group;

		//the Group or Text A/B has changed to clear the buffers.
		if(text_ab_state != last_text_ab_state)
		{
			for(i=0;i<64;i++)
			{
				rt_buffer[0][i] = 0;
				rt_buffer[1][i] = 0;
			}
		}
		last_text_ab_state = text_ab_state;

		if(rds_group == RDS_GROUP_2A)
		{
			offset = (blockB & 0xf) * 4;

			rt_buffer[1][offset + 0] = rt_buffer[0][offset + 0];
			rt_buffer[1][offset + 1] = rt_buffer[0][offset + 1];
			rt_buffer[1][offset + 2] = rt_buffer[0][offset + 2];
			rt_buffer[1][offset + 3] = rt_buffer[0][offset + 3];

			rt_buffer[0][offset + 0] = (blockC >> 8)   ? (blockC >> 8)   : 0x20;
			rt_buffer[0][offset + 1] = (blockC & 0xff) ? (blockC & 0xff) : 0x20;
			rt_buffer[0][offset + 2] = (blockD >> 8)   ? (blockD >> 8)   : 0x20;
			rt_buffer[0][offset + 3] = (blockD & 0xff) ? (blockD & 0xff) : 0x20;

			//Are there differences, if so clear and start from scratch.
			if(((rt_buffer[1][offset + 0]) && (rt_buffer[1][offset + 0] != rt_buffer[0][offset + 0])) ||
				((rt_buffer[1][offset + 1]) && (rt_buffer[1][offset + 1] != rt_buffer[0][offset + 1])) ||
				((rt_buffer[1][offset + 2]) && (rt_buffer[1][offset + 2] != rt_buffer[0][offset + 2])) ||
				((rt_buffer[1][offset + 3]) && (rt_buffer[1][offset + 3] != rt_buffer[0][offset + 3])))
			{
				for(i=0;i<64;i++)
				{
					rt_buffer[0][i] = 0;
					rt_buffer[1][i] = 0;
				}           
				rt_buffer[0][offset + 0] = (blockC >> 8)   ? (blockC >> 8)   : 0x20;
				rt_buffer[0][offset + 1] = (blockC & 0xff) ? (blockC & 0xff) : 0x20;
				rt_buffer[0][offset + 2] = (blockD >> 8)   ? (blockD >> 8)   : 0x20;
				rt_buffer[0][offset + 3] = (blockD & 0xff) ? (blockD & 0xff) : 0x20;
			}
		}
		else
		{
			offset = (blockB & 0x0F) * 2;

			rt_buffer[1][offset + 0] = rt_buffer[0][offset + 0];
			rt_buffer[1][offset + 1] = rt_buffer[0][offset + 1];
			//load new data into current buffer
			rt_buffer[0][offset + 0] = (blockD >> 8)   ? (blockD >> 8)   : 0x20;
			rt_buffer[0][offset + 1] = (blockD & 0xff) ? (blockD & 0xff) : 0x20;
			//Are there differences, if so clear and start from scratch.
			if(((rt_buffer[1][offset + 0]) && (rt_buffer[1][offset + 0] != rt_buffer[0][offset + 0])) ||
				((rt_buffer[1][offset + 1]) && (rt_buffer[1][offset + 1] != rt_buffer[0][offset + 1])))
			{
				for(i=0;i<64;i++)
				{
					rt_buffer[0][i] = 0;
					rt_buffer[1][i] = 0;
				}           
				rt_buffer[0][offset + 0] = (blockD >> 8)   ? (blockD >> 8)   : 0x20;
				rt_buffer[0][offset + 1] = (blockD & 0xff) ? (blockD & 0xff) : 0x20;
			}
		}


		same = 1;
		offset = 0;
		text_size = 0;
		first_zero = 0xff;

		while (same && (offset < 64))
		{
			if(rt_buffer[1][offset] != rt_buffer[0][offset])
				same = 0;
			if(rt_buffer[0][offset] > 0)
			{
				text_size = offset;
			}
			else if(first_zero == 0xff)
			{
				first_zero = offset;
			}
			offset++;
		}
		if(first_zero < text_size)
		{
			same = 0;
		}

		if(same)
		{
			offset = 0;                             
			while (same && (offset < 64))
			{
				if(ServiceData[offset] != rt_buffer[0][offset])
					same = 0;
				offset++;
			}
			//
			if(same == 0)
			{
				for(i=0;i<64;i++)
					ServiceData[i] = rt_buffer[0][i];
				ServiceData[64] = '\0';
				ret |= 0x0004;	//Change of Status
			}
		}
	}
	break;

	case RDS_GROUP_4A:
		uint32_t mjd;

		mjd = (blockB & 0x03);
		mjd <<= 15;
		mjd += ((blockC >> 1) & 0x7FFF); 

	    long J, C, Y, M;

		J = mjd + 2400001 + 68569;
		C = 4 * J / 146097;
		J = J - (146097 * C + 3) / 4;
		Y = 4000 * (J + 1) / 1461001;
		J = J - 1461 * Y / 4 + 31;
		M = 80 * J / 2447;
		Days = J - 2447 * M / 80;
		J = M / 11;
		Months = M + 2 - (12 * J);
		Year = 100 * (C - 49) + Y + J;

		Hours = ((blockD >> 12) & 0x0f);
		Hours += ((blockC << 4) & 0x0010);
		Minutes = ((blockD >> 6) & 0x3f);
		//LocalOffset = (blockD & 0x3f);
		ret |= 0x0010;
	break;
	}
	return ret;
}
