#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"




// The header for the beginning of a read/write operation to the C1101
#define CC1101_HEADER(RW, Burst, Address) \
        ((RW << 7) | (Burst << 6) | (Address & 0x3F))

// Send a 2 byte packet to the C1101
#define CC1101_PACKET(CC1101_HEADER, Data) \
        ((CC1101_HEADER << 8) | (Data & 0xFF))

// The strobe command to reset the C1101
#define STROBE_SRES 0x30
// The strobe command to enter SFSTXON mode
#define STROBE_SFSTXON 0x31
// The strobe command to turn off the crystal oscillator
#define STROBE_SXOFF 0x32
// The strobe command to calibrate the Frequency Synthesizer and turn it off
#define STROBE_SCAL 0x33
// the strobe command to enter RX mode
#define STROBE_SRX 0x34
// The strobe command to enter TX mode
#define STROBE_STX 0x35
// The strobe command to enter Idle mode
#define STROBE_SIDLE 0x36
// The strobe command to flush the RX FIFO
#define STROBE_SFRX 0X3A
// The strobe command to flush the TX FIFO
#define STROBE_SFTX 0x3B

// Highest byte location of the Frequency registers 
#define FREQ0 0x0F
// Middle byte location of the Frequency registers
#define FREQ1 0x0E
// Lowest byte location of the Frequency registers
#define FREQ2 0x0D

// Sync Word 1 register. High byte of the sync word.
#define SYNC1 0x04
// Sync Word 0 register. Low byte of the sync word.
#define SYNC0 0x05
// Packet Length register
#define PKTLEN 0x06
// Packet Automation Control 1 register
#define PKTCTRL1 0x07
// Packet Automation Control 0 register
#define PKTCTRL0 0x08

// Modem Configuration 4
#define MDMCFG4 0x10
// Modem Configuration 3
#define MDMCFG3 0x11
// Modem Configuration 2
#define MDMCFG2 0x12
// Modem Configuration 1
#define MDMCFG1 0x13

// Leftover Transmit Bytes register
#define TXBYTES 0x3A
// Leftover Receive Bytes register
#define RXBYTES 0x3B

#define CC1101_CRYSTAL_FREQUENCY 26000000 // 26 MHz

typedef uint8_t CC1101_STATUS_BYTE;

typedef enum {
    CC1101_PACKET_TYPE_FIXED = 0x00,    // Fixed packet length mode
    CC1101_PACKET_TYPE_VARIABLE = 0x01, // Variable packet length mode
    CC1101_PACKET_TYPE_INFINITE = 0x02  // Infinite packet length mode
} CC1101_PACKET_TYPE;
typedef uint8_t CC1101_PACKET_LENGTH;

typedef struct {
    gpio_num_t csn_pin;  // !Chip Select pin
    gpio_num_t sck_pin;  // Serial Clock pin
    gpio_num_t mosi_pin; // Input pin 
    gpio_num_t miso_pin; // Output pin

    CC1101_PACKET_TYPE packetType; // Current packet type. Should not be touched by the user
    CC1101_PACKET_LENGTH packet_length; // Current packet length. Should not be touched by the user
} Transceiver;

typedef enum {
    C1101_MODE_IDLE = 0x00,
    C1101_MODE_RX = 0x01,
    C1101_MODE_TX = 0x02,
    C1101_MODE_FSTXON = 0x03,
    C1101_MODE_CALIBRATE = 0x04,
    C1101_MODE_SETTLING = 0x05,
    C1101_MODE_RXFIFO_OVERFLOW = 0x06,
    C1101_MODE_TXFIFO_UNDERFLOW = 0x07
} C1101_MODE;

typedef enum {
    C1101_SET_MODE_IDLE = STROBE_SIDLE,         // IDLE mode
    C1101_SET_MODE_RX = STROBE_SRX,             // RX mode
    C1101_SET_MODE_TX = STROBE_STX,             // TX mode  
    C1101_SET_MODE_FSTXON = STROBE_SFSTXON,     // FSTXON mode
} C1101_SET_MODE;


// Length of the predefined channel filter bandwidths array
#define CC1101_CHANNEL_FILTER_LENGTH (sizeof(channelFilterBandwidths) / sizeof(channelFilterBandwidths[0]))
typedef struct {
    uint32_t bandwidth;         // Bandwidth in Hz
    uint8_t mdmcfg4_CHANBW_E;   // 4 MSB of MDMCFG4
    uint8_t mdmcfg4_CHANBW_M;   // 4 LSB of
} C1101_CHANNEL_FILTER_BANDWIDTH;
static const C1101_CHANNEL_FILTER_BANDWIDTH channelFilterBandwidths[] = {
    { 58000, 0x3, 0x3 },
    { 68000, 0x3, 0x2 },
    { 81000, 0x3, 0x1 },
    { 102000, 0x3, 0x0 },
    { 116000, 0x2, 0x3 },
    { 135000, 0x2, 0x2 },
    { 162000, 0x2, 0x1 },
    { 203000, 0x2, 0x0 },
    { 232000, 0x1, 0x3 },
    { 270000, 0x1, 0x2 },
    { 325000, 0x1, 0x1 },
    { 406000, 0x1, 0x0 },
    { 464000, 0x0, 0x3 },
    { 541000, 0x0, 0x2 },
    { 650000, 0x0, 0x1 },
    { 812000, 0x0, 0x0 }
};

// Sync Word Qualifier 
#define CC1101_SYNC_WORD_QUALIFIER_Msk 0x07
typedef enum {
    CC1101_SYNC_NONE     = 0x00, // No sync word qualifier
    CC1101_SYNC_15_16    = 0x01, // 15/16 sync word qualifier
    CC1101_SYNC_16_16    = 0x02, // 16/16 sync word qualifier
    CC1101_SYNC_30_32    = 0x03, // 30/32 sync word qualifier
    CC1101_SYNC_CS_NONE  = 0x04, // No sync word qualifier, carrier sense above threshold
    CC1101_SYNC_CS_15_16 = 0x05, // 15/16 sync word qualifier, carrier sense above threshold
    CC1101_SYNC_CS_16_16 = 0x06, // 16/16 sync word qualifier, carrier sense above threshold
    CC1101_SYNC_CS_30_32 = 0x07, // 30/32 sync word qualifier, carrier sense above threshold
} CC1101_SYNC_WORD_QUALIFIER;


// Preamble Length Position
#define CC1101_PREAMBLE_LENGTH_POSITION 4
// Preamble Length
#define MDMCFG1_PREAMBLE_LENGTH_Msk (0x07 << CC1101_PREAMBLE_LENGTH_POSITION)
typedef enum {
    CC1101_PREAMBLE_2_BYTES = 0x00, // 2 bytes
    CC1101_PREAMBLE_3_BYTES = 0x01, // 3 bytes
    CC1101_PREAMBLE_4_BYTES = 0x02, // 4 bytes
    CC1101_PREAMBLE_6_BYTES = 0x03, // 6 bytes
    CC1101_PREAMBLE_8_BYTES = 0x04, // 8 bytes
    CC1101_PREAMBLE_12_BYTES = 0x05, // 12 bytes
    CC1101_PREAMBLE_16_BYTES = 0x06, // 16 bytes
    CC1101_PREAMBLE_24_BYTES = 0x07  // 24 bytes
} CC1101_PREAMBLE_LENGTH;

CC1101_STATUS_BYTE CC1101WriteRegister(Transceiver *transceiver, uint8_t address, uint8_t value);

CC1101_STATUS_BYTE CC1101ReadRegister(Transceiver *transceiver, uint8_t address, uint8_t *value);

CC1101_STATUS_BYTE CC1101SetFrequency(Transceiver *transceiver, uint32_t frequency);

CC1101_STATUS_BYTE CC1101Reset(Transceiver *transceiver);

CC1101_STATUS_BYTE CC1101SetMode(Transceiver *transceiver, C1101_SET_MODE mode);

CC1101_STATUS_BYTE CC1101SendStrobe(Transceiver *transceiver, uint8_t strobe);

CC1101_STATUS_BYTE CC1101TransferByte(Transceiver *transceiver, uint8_t byte);

uint32_t CC1101ReceiveByte(Transceiver *transceiver);

uint8_t CC1101Transmitbyte(Transceiver *transceiver, uint32_t byte);

CC1101_STATUS_BYTE CC1101SetDataRate(Transceiver *transceiver, uint32_t dataRate);

uint32_t CC1101SetChannelFilterbandwidth(Transceiver *transceiver, uint32_t bandwidth);

CC1101_STATUS_BYTE CC1101SetSyncWordQualifier(Transceiver *transceiver, CC1101_SYNC_WORD_QUALIFIER qualifier);

CC1101_STATUS_BYTE CC1101SetSyncWord(Transceiver *transceiver, uint16_t syncWord);

CC1101_STATUS_BYTE CC1101SetMinimumPreamble(Transceiver *transceiver, CC1101_PREAMBLE_LENGTH preambleLength);

CC1101_STATUS_BYTE CC1101SetPacketLength(Transceiver *transceiver, CC1101_PACKET_LENGTH packetLength);

CC1101_STATUS_BYTE CC1101SetPacketType(Transceiver *transceiver, CC1101_PACKET_TYPE packetType);

CC1101_STATUS_BYTE CC1101FlushFIFO(Transceiver *transceiver);






void CC1101SetPATable(Transceiver *transceiver, uint8_t power);