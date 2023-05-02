/*
 * Hybrid Radio Project with RadioDNS functionality
 * DAB Radio with Colour LCD, and Hybrid Radio Functionality
 * AVIT Research Ltd
 *
 * RadioDNS.h (Radio DNS header file)
 * Provides RadioDNS implmentation for logo download
 *
 *
 *
 * v0.4 03/05/2023 - Updated for TFT_eSPI library
 *
 */
void RadioDNSsetup(const char *ssid, const char *password);
uint16_t GetDABLogo(uint16_t ServiceID, uint16_t EnsembleID, uint16_t ECC);
uint16_t GetFMLogo(uint16_t Freq, uint16_t ServiceID, uint16_t ECC);