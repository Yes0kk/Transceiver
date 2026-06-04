#include "main.h"

#define transceiverFrequency 433123456
#define transceiverDataRate 1000
#define transceiverBandwidth 58000
#define transceiverSyncWord 0xD391
#define transceiverPreambleLength CC1101_PREAMBLE_16_BYTES
#define transceiverSyncWordQualifier CC1101_SYNC_16_16
#define transceiverPacketLength 0x04
#define transceiverPacketType CC1101_PACKET_TYPE_FIXED
#define transceiverModulationFormat CC1101_MOD_FORMAT_2FSK

#define TRANSMITTER_CSN 22  // GPIO 22

#define TRANSCEIVER_SCK 18  // GPIO 18
#define TRANSCEIVER_MOSI 19  // GPIO 19
#define TRANSCEIVER_MISO 21  // GPIO 21

#define RECEIVER_CSN 23     // GPIO 23

Transceiver transmitter = {
    .csn_pin = TRANSMITTER_CSN,

    .sck_pin = TRANSCEIVER_SCK,
    .mosi_pin = TRANSCEIVER_MOSI,
    .miso_pin = TRANSCEIVER_MISO
};
Transceiver receiver = {
    .csn_pin = RECEIVER_CSN,

    .sck_pin = TRANSCEIVER_SCK,
    .mosi_pin = TRANSCEIVER_MOSI,
    .miso_pin = TRANSCEIVER_MISO
};


void app_main(void)
{
    Setup();
    Test();
    DataLoop();
}

void Setup(void)
{
    gpio_set_direction(TRANSMITTER_CSN, GPIO_MODE_OUTPUT);
    gpio_set_direction(RECEIVER_CSN, GPIO_MODE_OUTPUT);
    gpio_set_direction(TRANSCEIVER_SCK, GPIO_MODE_OUTPUT);
    gpio_set_direction(TRANSCEIVER_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction(TRANSCEIVER_MISO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TRANSCEIVER_MISO, GPIO_PULLUP_ONLY);

    gpio_set_level(TRANSMITTER_CSN, 1);
    gpio_set_level(RECEIVER_CSN, 1);
    gpio_set_level(TRANSCEIVER_SCK, 0);
    gpio_set_level(TRANSCEIVER_MOSI, 0);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    CC1101Reset(&transmitter);
    CC1101Reset(&receiver);

    CC1101SetFrequency(&transmitter, transceiverFrequency);
    CC1101SetFrequency(&receiver, transceiverFrequency);
    CC1101SetBitRate(&transmitter, transceiverDataRate);
    CC1101SetBitRate(&receiver, transceiverDataRate);
    CC1101SetChannelFilterbandwidth(&transmitter, transceiverBandwidth);
    CC1101SetChannelFilterbandwidth(&receiver, transceiverBandwidth);
    CC1101SetSyncWordQualifier(&transmitter, transceiverSyncWordQualifier);
    CC1101SetSyncWordQualifier(&receiver, transceiverSyncWordQualifier);
    CC1101SetSyncWord(&transmitter, transceiverSyncWord);
    CC1101SetSyncWord(&receiver, transceiverSyncWord);
    CC1101SetMinimumPreamble(&transmitter, transceiverPreambleLength);
    CC1101SetMinimumPreamble(&receiver, transceiverPreambleLength);
    CC1101SetPacketLength(&transmitter, transceiverPacketLength);
    CC1101SetPacketLength(&receiver, transceiverPacketLength);
    CC1101SetPacketType(&transmitter, transceiverPacketType);
    CC1101SetPacketType(&receiver, transceiverPacketType);
    CC1101SetModulationFormat(&transmitter, transceiverModulationFormat);
    CC1101SetModulationFormat(&receiver, transceiverModulationFormat);
    CC1101SetFrequencyDeviation(&transmitter, 5000);
    CC1101SetFrequencyDeviation(&receiver, 5000);

    CheckValues();


    vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait for 1 second
}

void CheckValues(void)
{

}

void PrintRSSI(const char *label)
{
    uint8_t rssi = 0;
    CC1101ReadRegister(&receiver, 0x34, &rssi); // RSSI status register
    printf("%s RSSI raw = 0x%02X\n", label, rssi);
}

void Test(void)
{
    uint32_t txData = 0x12345678;

    CC1101SendStrobe(&transmitter, STROBE_SIDLE);
    CC1101SendStrobe(&receiver, STROBE_SIDLE);

    CC1101FlushFIFO(&transmitter);
    CC1101FlushFIFO(&receiver);

    CC1101SendStrobe(&receiver, STROBE_SRX);
    vTaskDelay(20 / portTICK_PERIOD_MS);

    uint8_t marcstate = 0;
    CC1101ReadRegister(&receiver, 0x35, &marcstate);
    printf("RX MARCSTATE before TX = 0x%02X\n", marcstate & 0x1F);

    PrintRSSI("Before TX");

    printf("Transmitting: 0x%08lX\n", (unsigned long)txData);

    CC1101Transmitbyte(&transmitter, txData);

    PrintRSSI("After TX");

    vTaskDelay(20 / portTICK_PERIOD_MS);

    uint8_t rxBytes = 0;
    CC1101ReadRegister(&receiver, RXBYTES, &rxBytes);
    printf("RXBYTES after TX = 0x%02X\n", rxBytes);

    uint32_t rxData = CC1101ReceiveByte(&receiver);
    printf("Received: 0x%08lX\n", (unsigned long)rxData);
}

void DataLoop(void)
{
    while (1)
    {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        printf("\nRestarting...\n\n");

        Setup();
        Test();
    }
}

void PrintStatusByte(CC1101_STATUS_BYTE status)
{
    printf("C1101 Status Byte: 0x%02X\n", status);
    printf("CHIP_RDY: %d\n", (status >> 7) & 0x1);
    printf("STATE: %d\n", (status >> 4) & 0x7);
    printf("FIFO_BYTES_AVAILABLE: %d\n", status & 0xF);
}


char *ByteToBinary(uint8_t value)
{
    static char binary[9];

    for (int i = 7; i >= 0; i--)
    {
        binary[7 - i] = ((value >> i) & 1) + '0';
    }

    binary[8] = '\0';

    return binary;
}