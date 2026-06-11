#include "main.h"

#define TRANSMITTER_CSN 22  // GPIO 22
#define RECEIVER_CSN 23     // GPIO 23

#define TRANSCEIVER_SCK 18  // GPIO 18
#define TRANSCEIVER_MOSI 19  // GPIO 19
#define TRANSCEIVER_MISO 21  // GPIO 21




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
    SetupTransceivers();

    DataLoop();
}

void SetupTransceivers(void)
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

    CC1101Reset(&transmitter);
    CC1101Reset(&receiver);

    CC1101Init(&receiver);
    CC1101Init(&transmitter);
}

void DataLoop(void)
{
    CC1101SendStrobe(&receiver, STROBE_SRX);
    CC1101SetAfterPacketReceivedMode(&receiver, CC1101_PACKET_RECEIVED_STAYRX);


    uint16_t count = 1;
    while (1)
    {
        uint8_t txData[] = "I don't like tomatoes!";

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
