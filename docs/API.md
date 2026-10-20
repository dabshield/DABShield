# DABShield Library

Control the DABShield receiver from an Arduino-compatible board. The library supports DAB, DAB+ and FM reception, including service information, DAB slideshow data, and FM RDS data.

> **Hardware note:** This library is for the DABShield hardware. Check the board's pinout and connection guide before wiring it to your Arduino. Supported SPI pins, chip-select wiring, voltage levels, and audio outputs depend on the board and shield revision.

## Contents

- [Installation](#installation)
- [Getting started](#getting-started)
- [SPI connection](#spi-connection)
- [DAB operation](#dab-operation)
- [FM operation](#fm-operation)
- [Audio controls](#audio-controls)
- [Callbacks and slideshow images](#callbacks-and-slideshow-images)
- [API reference](#api-reference)
- [Data types and public fields](#data-types-and-public-fields)
- [Troubleshooting](#troubleshooting)

## Installation

Install **DABShield** from Arduino IDE Library Manager, or copy the `DABShield` library folder into your Arduino `libraries` folder. Include the library in your sketch:

```cpp
#include <SPI.h>
#include <DABShield.h>
```

Examples are provided in the library's `examples` folder:

- `DABApp` — serial-console DAB/FM example
- `DABLCD` — character LCD radio
- `DABSlideShow` — colour-display DAB slideshow example for ESP32
- `HybridRadio` — colour-display and RadioDNS example for ESP32

The display examples have additional board and library requirements described in their sketch headers.

## Getting started

The library uses the application's `DABSpiMsg()` function to transfer bytes over SPI. Define that function once in the sketch; see [SPI connection](#spi-connection).

```cpp
#include <SPI.h>
#include <DABShield.h>

constexpr uint8_t DAB_CS = 8; // Use the chip-select pin wired to your shield.
constexpr uint8_t BAND_DAB = 0;

DAB Dab;

void RadioInfoUpdated();

void setup()
{
  Serial.begin(115200);

  pinMode(DAB_CS, OUTPUT);
  digitalWrite(DAB_CS, HIGH);
  SPI.begin();

  Dab.setCallback(RadioInfoUpdated);
  Dab.begin(BAND_DAB);

  if (Dab.error != 0)
  {
    Serial.print(F("DABShield initialisation error: "));
    Serial.println(Dab.error);
  }
}

void loop()
{
  Dab.task();
}

void RadioInfoUpdated()
{
  Serial.println(Dab.ServiceData);
}

void DABSpiMsg(unsigned char *data, uint32_t len)
{
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(DAB_CS, LOW);
  SPI.transfer(data, len);
  digitalWrite(DAB_CS, HIGH);
  SPI.endTransaction();
}
```

`Dab.task()` must be called repeatedly from `loop()` (or an equivalent regularly scheduled task). It polls the receiver and dispatches service-data callbacks. Do not call `DABSpiMsg()` from application code; it is the SPI transport hook used by the library.

## SPI connection

The sketch must provide this function:

```cpp
void DABSpiMsg(unsigned char *data, uint32_t len);
```

The library passes a command or response buffer to this function. The implementation must transfer exactly `len` bytes in place: transmit the bytes in `data` and replace them with the received bytes. The bundled examples use SPI mode 0, MSB first, and start at 2 MHz.

The chip-select pin is selected by the sketch and must match the wiring. The examples commonly use pin 8 on non-ESP32 boards and pin 12 on ESP32 boards; these are example conventions, not a substitute for checking the shield and board pinout.

Some SAMD and SAM examples use software/bit-banged SPI, enabled by defining `DAB_SPI_BITBANG` in the sketch. Follow the pin setup in `DABApp` or `DABLCD` if using that configuration.

The optional custom-pin overload configures the receiver's interrupt, reset and power-enable pins:

```cpp
Dab.begin(band, interruptPin, resetPin, powerEnablePin);
```

It does not configure SPI chip select or SPI data/clock pins; those remain the application's responsibility.

## DAB operation

### Select DAB mode

```cpp
Dab.begin();       // Same as begin(0): DAB
Dab.begin(0);      // DAB
```

`begin()` initializes the receiver and detects the DABShield Pro codec when present. Call it after setting up SPI. The `band` argument is `0` for DAB or `1` for FM. Reinitializing with `begin()` switches modes.

### Tune an ensemble

DAB tuning uses a **frequency-table index**, not a frequency in kHz. The library provides 37 Band III entries:

```cpp
for (uint8_t index = 0; index < DAB_FREQS; ++index)
{
  Dab.tune(index);
  if (Dab.servicevalid())
  {
    Serial.println(Dab.Ensemble);
    Serial.print(F("Frequency (kHz): "));
    Serial.println(Dab.freq_khz(index));
    break;
  }
}
```

`Dab.freq_khz(index)` returns the frequency for a valid index, or `0` if the index is out of range. `DAB_FREQS` is the number of supported table entries.

An invalid index passed to `tune(uint8_t)` or `tuneservice()` is ignored and sets `Dab.error` to `0x80`; no DAB tune command is sent.

### Find and select services

After a successful ensemble tune, `Dab.numberofservices` and `Dab.service[]` contain the discovered services:

```cpp
Dab.tune(0);

if (Dab.servicevalid())
{
  for (uint8_t i = 0; i < Dab.numberofservices; ++i)
  {
    Serial.print(i);
    Serial.print(F(": "));
    Serial.println(Dab.service[i].Label);
  }

  if (Dab.numberofservices > 0)
  {
    Dab.set_service(0);
  }
}
```

`set_service(index)` selects an entry in the current `service[]` list. Check that the index is less than `numberofservices` before calling it.

Use `tuneservice()` when a service's frequency index, service ID and component ID have already been saved:

```cpp
Dab.tuneservice(freqIndex, serviceID, componentID);
```

Service order can change after rescanning or retuning. Persist the service identifiers and frequency rather than assuming a service will always have the same array index.

### Service validity

```cpp
bool found = Dab.servicevalid();
```

Checks the receiver's current DAB signal/service status. It is commonly used after `tune()` while scanning frequencies.

## FM operation

### Select FM and tune

```cpp
constexpr uint8_t BAND_FM = 1;

Dab.begin(BAND_FM);
Dab.tune((uint16_t)8750); // 87.50 MHz
```

FM tuning uses units of **10 kHz**: `8750` means 87.50 MHz and `10790` means 107.90 MHz. Use a `uint16_t` value or cast when calling the FM overload; the DAB overload takes a `uint8_t` frequency-table index.

### Seek

```cpp
bool stationFound = Dab.seek(1, 1); // Seek up, wrap at band edge
```

- `dir`: `1` seeks up; `0` seeks down.
- `wrap`: nonzero wraps at the band edge; `0` stops at the edge.
- Returns the receiver's FM station-valid result after seeking.

### FM status and RDS

```cpp
bool stationValid = Dab.status();
```

In FM mode, `status()` refreshes reception information and returns the FM `valid` flag. FM RDS data is made available through the public fields `pi`, `ps` and `pty`; service text is in `ServiceData`.

## Audio controls

```cpp
Dab.vol(32);                   // Volume: 0 (minimum) through 63 (maximum)
Dab.mute(true, true);           // Mute both channels
Dab.mute(false, false);         // Unmute both channels
Dab.mute(true, false);          // Mute left only
Dab.mono(true);                 // Mono
Dab.mono(false);                // Stereo
Dab.speaker(SPEAKER_STEREO);    // Pro speaker output configuration
```

`speaker()` selects `SPEAKER_NONE`, `SPEAKER_DIFF` or `SPEAKER_STEREO`. The built-in speaker codec controls (`speaker`, `bass`, `mid` and `treble`) apply to DABShield Pro. Bass, mid and treble accept levels from `-12` to `12`.

For non-Pro shields, `digitalouput(true)` enables the receiver's digital audio output in I2S master mode; `digitalouput(false)` disables it. The API spelling is `digitalouput` (with the `u` before `t`), matching the existing library.

## Callbacks and slideshow images

### Service-data callback

Register a callback with the signature `void callback(void)`:

```cpp
void ServiceDataUpdated()
{
  Serial.println(Dab.ServiceData);
}

void setup()
{
  // Set up SPI first.
  Dab.setCallback(ServiceDataUpdated);
  Dab.begin();
}
```

Call `Dab.task()` regularly to process receiver events and invoke the callback. The callback may be used for DAB service text and FM RDS updates. Keep callback work short; copy any data needed later rather than keeping pointers into receiver/library buffers.

### DAB slideshow callback

Register a caller-owned buffer and callback before tuning to a DAB service:

```cpp
constexpr size_t IMAGE_BUFFER_SIZE = 48 * 1024;
uint8_t imageBuffer[IMAGE_BUFFER_SIZE];

void ShowImage(uint8_t *image, uint32_t size, ImageType type)
{
  if (type == DAB_JPEG)
  {
    // Decode or display image[0..size) here.
  }
}

void setup()
{
  // Set up SPI first.
  Dab.slideshow(imageBuffer, sizeof(imageBuffer), ShowImage);
  Dab.begin();
}
```

The buffer must remain valid for as long as the library uses it. The image data is assembled in this buffer and reused; consume or copy it during the callback. The current library invokes the callback for complete JPEG and PNG slideshow objects that fit the supplied buffer. JPEG, PNG, GIF and BMP enum values exist, but the current parser dispatches JPEG and PNG images.

## API reference

| Method | Description |
|---|---|
| `DAB()` | Creates a receiver object. |
| `begin()` | Initializes in DAB mode. |
| `begin(band)` | Initializes in DAB (`0`) or FM (`1`) mode. |
| `begin(band, interruptPin, resetPin, powerEnablePin)` | Initializes using custom receiver control pins. |
| `end()` | Turns off the receiver power-enable output. |
| `task()` | Polls for receiver events and dispatches callbacks. Call regularly. |
| `setCallback(callback)` | Registers the service-data callback. |
| `tune(uint8_t freqIndex)` | Tunes to a DAB frequency-table index. |
| `tune(uint16_t frequency10kHz)` | Tunes to an FM frequency in 10 kHz units. |
| `tuneservice(freqIndex, serviceID, compID)` | Tunes directly to a known DAB service. |
| `set_service(index)` | Selects a service from the current DAB service list. |
| `servicevalid()` | Returns whether the current DAB reception has a valid service. |
| `seek(direction, wrap)` | Seeks to an FM station; returns station validity. |
| `status()` | Refreshes current reception information; returns FM validity in FM mode and `true` in DAB mode. |
| `status(serviceID, compID)` | Refreshes DAB information for the specified service; returns `false` in FM mode. |
| `time(DABTime *time)` | Reads the receiver's DAB time into a `DABTime`. See the return-value note below. |
| `freq_khz(index)` | Returns a DAB table frequency in kHz, or `0` for an invalid index. |
| `vol(level)` | Sets volume from 0 to 63. |
| `mute(left, right)` | Independently mutes the left and right channels. |
| `mono(enable)` | Enables or disables mono output. |
| `speaker(value)` | Selects Pro speaker output configuration. |
| `bass(level)`, `mid(level)`, `treble(level)` | Set Pro codec equalizer levels from -12 to 12. |
| `digitalouput(enable)` | Enables/disables non-Pro digital output (I2S master mode when enabled). |
| `slideshow(buffer, size, callback)` | Registers the image assembly buffer and slideshow callback. |

**`time()` return-value compatibility note:** The current implementation returns `false` after reading time successfully and `true` if passed a null pointer. Check the pointer before calling and use a non-null `DABTime` object. This legacy return convention may be corrected in a future API version.

## Data types and public fields

### Enums

```cpp
enum DABSpeaker { SPEAKER_NONE, SPEAKER_DIFF, SPEAKER_STEREO };
enum ServiceType { SERVICE_NONE, SERVICE_AUDIO, SERVICE_DATA };
enum AudioMode { DUAL, MONO, STEREO, JOINT_STEREO };
enum ImageType { DAB_GIF, DAB_JPEG, DAB_BMP, DAB_PNG };
```

### Service and time structures

```cpp
typedef struct {
  uint8_t Freq;
  uint32_t ServiceID;
  uint32_t CompID;
  char Label[17];
} DABService;

typedef struct {
  uint16_t Year;
  uint8_t Months, Days, Hours, Minutes, Seconds;
} DABTime;
```

### Receiver fields

Fields are public and updated as the receiver is queried or events are processed.

| Field(s) | Meaning |
|---|---|
| `Ensemble`, `EnsembleID`, `ECC` | Current DAB ensemble label, identifier and extended country code. |
| `service[]`, `numberofservices` | Current ensemble service list and its length. `DAB_MAX_SERVICES` is 24 on AVR Uno and 32 on other targets. |
| `ServiceData` | Current DAB service text or FM radio text. Buffer capacity is `DAB_MAX_SERVICEDATA_LEN` (128 bytes). |
| `freq_index` | Current DAB frequency-table index. |
| `freq` | Current FM frequency in 10 kHz units. |
| `valid`, `signalstrength`, `snr`, `quality` | Current reception validity and receiver status values. |
| `type`, `dabplus`, `bitrate`, `samplerate`, `mode`, `pty` | DAB service/audio information. |
| `pi`, `ps`, `pty` | FM RDS program identifier, program service name and program type. |
| `Year`, `Months`, `Days`, `Hours`, `Minutes` | Time values last read from the receiver. `time()` also fills a `DABTime`. |
| `PartNo`, `ChipRevision`, `RomID`, `VerMajor`, `VerMinor`, `VerBuild` | Receiver part and firmware information. |
| `LibMajor`, `LibMinor` | Library version numbers. |
| `Pro`, `speakeroutput` | Whether a Pro codec was detected and its selected speaker output. |
| `error` | Last reported command error. `0` indicates no reported error; `0x80` is used for timeout/invalid DAB frequency index. |
| `imagebuffer`, `imagebuffersize` | Slideshow buffer pointer and capacity registered by `slideshow()`. |

The `DABService` and `DABTime` fields and all receiver status fields are ordinary public C++ members; they are not separate getter functions.

## Troubleshooting

- **`DABSpiMsg` not declared/defined:** provide exactly one `DABSpiMsg(unsigned char *, uint32_t)` implementation in the sketch.
- **`Dab.error` is nonzero after initialization:** check SPI wiring, chip select, reset/power pins, shield connection and board voltage compatibility.
- **No DAB services:** verify the antenna and reception, then scan valid indices from `0` through `DAB_FREQS - 1`. `servicevalid()` reports whether an ensemble was found.
- **No callback data:** call `setCallback()` before tuning and call `task()` regularly.
- **No slideshow callback:** register a sufficiently large image buffer and callback before tuning; the image must be a supported JPEG/PNG object that fits the buffer.
- **Confusing DAB/FM overload:** use a `uint8_t` index for DAB and a `uint16_t` frequency in 10 kHz units for FM.

For wiring diagrams and board-specific information, see the [DABShield circuit guide](https://www.dabshield.com/circuit). For working sketches, use the examples installed with the library.
