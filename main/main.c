#include "main.h"

#define transceiverFrequency 433123456
#define transceiverDataRate 1000
#define transceiverBandwidth 58000
#define transceiverSyncWord 0xD391
#define transceiverPreambleLength CC1101_PREAMBLE_16_BYTES
#define transceiverSyncWordQualifier CC1101_SYNC_16_16
#define transceiverPacketLength 0x01
#define transceiverPacketType CC1101_PACKET_TYPE_FIXED
#define transceiverModulationFormat CC1101_MOD_FORMAT_2FSK

#define TRANSMITTER_CSN 22  // GPIO 22

#define TRANSCEIVER_SCK 18  // GPIO 18
#define TRANSCEIVER_MOSI 19  // GPIO 19
#define TRANSCEIVER_MISO 21  // GPIO 21

#define GDO2 34

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
    gpio_set_direction(TRANSMITTER_CSN, GPIO_MODE_OUTPUT);
    gpio_set_direction(RECEIVER_CSN, GPIO_MODE_OUTPUT);
    gpio_set_direction(TRANSCEIVER_SCK, GPIO_MODE_OUTPUT);
    gpio_set_direction(TRANSCEIVER_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction(TRANSCEIVER_MISO, GPIO_MODE_INPUT);

    gpio_set_level(TRANSMITTER_CSN, 1);
    gpio_set_level(RECEIVER_CSN, 1);
    gpio_set_level(TRANSCEIVER_SCK, 0);
    gpio_set_level(TRANSCEIVER_MOSI, 0);

    vTaskDelay(pdMS_TO_TICKS(10));

    CC1101Reset(&transmitter);
    CC1101Reset(&receiver);

    DataLoop();
}


void DataLoop(void)
{
    Setup();
    CC1101SendStrobe(&receiver, STROBE_SRX);
    CC1101RXOff(&receiver, CC1101_PACKET_RECEIVED_STAYRX);


    uint16_t count = 1;
    while (1)
    {
        uint8_t txData[] = "I dont like tomatoes!";

        uint8_t packet[65] = { 0 };
        uint8_t size = 0;

        CC1101Transmitbyte(&transmitter, txData, sizeof(txData));

        if (CC1101ReceiveByte(&receiver, packet, &size))
        {
            printf("Receive packet: %s\n", packet);
        }
        
        printf("Count: %u\n", count);

        count++;

    }
}


void Setup(void)
{
    CC1101CommonSetup(&receiver);
    CC1101CommonSetup(&transmitter);

    CC1101TXOff(&transmitter, CC1101_PACKET_SENT_IDLE);
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