#include "c1101.h"


#define delay(x) esp_rom_delay_us(x)

void BeginTransfer(Transceiver *transceiver);
void EndTransfer(Transceiver *transceiver);
void WaitForSO(Transceiver *transceiver);

// Write a value to a CC1101 register
CC1101_STATUS_BYTE CC1101WriteRegister(Transceiver *transceiver, uint8_t address, uint8_t value)
{
    if (transceiver == NULL || address > 0x3D)
    {
        printf("(CC1101WriteRegister) Invalid address or transceiver\n");
        return 0;
    }
    CC1101_STATUS_BYTE status = 0;

    // Create the header byte
    uint8_t header = CC1101_HEADER(0, 0, address);

    
    BeginTransfer(transceiver);
    // Send the header to the C1101 and read the status byte
    status = CC1101TransferByte(transceiver, header);

    // Send the value to the C1101
    CC1101TransferByte(transceiver, value);
    EndTransfer(transceiver);
    
    return status;
}
// Read a value from a CC1101 register
CC1101_STATUS_BYTE CC1101ReadRegister(Transceiver *transceiver, uint8_t address, uint8_t *value)
{
    if (transceiver == NULL || address > 0x3D)
        return 0;
    CC1101_STATUS_BYTE status = 0;

    // Create the header byte
    uint8_t burst = (address >= 0x30) ? 1 : 0;
    uint8_t header = CC1101_HEADER(1, burst, address);

    BeginTransfer(transceiver);
    // Send the header to the C1101 and read the status byte
    status = CC1101TransferByte(transceiver, header);

    // Read the value from the C1101
    (*value) = CC1101TransferByte(transceiver, 0x00);
    EndTransfer(transceiver);

    return status;
}

// Reset the C1101 transceiver
CC1101_STATUS_BYTE CC1101Reset(Transceiver *transceiver)
{
    if (transceiver == NULL)
        return 0;

    // The dumb rest pattern you need to do before sending the SRES strobe
    gpio_set_level(transceiver->csn_pin, 1);
    delay(1);
    gpio_set_level(transceiver->csn_pin, 0);
    delay(1);
    gpio_set_level(transceiver->csn_pin, 1);
    delay(1);

    BeginTransfer(transceiver);
    CC1101_STATUS_BYTE status = CC1101TransferByte(transceiver, STROBE_SRES);
    EndTransfer(transceiver);

    CC1101FlushFIFO(transceiver); // Flush the FIFOs after reset

    return status;
}
// Set the C1101 transceiver to a specific mode
CC1101_STATUS_BYTE CC1101SetMode(Transceiver *transceiver, C1101_SET_MODE mode)
{
    if (transceiver == NULL)
        return 0;
    
    BeginTransfer(transceiver);
    CC1101_STATUS_BYTE status = CC1101TransferByte(transceiver, mode);
    EndTransfer(transceiver);

    return status;
}
// Send a specific strobe (command) to the CC1101 transceiver
CC1101_STATUS_BYTE CC1101SendStrobe(Transceiver *transceiver, uint8_t strobe)
{
    if (transceiver == NULL)
        return 0;

    BeginTransfer(transceiver);
    CC1101_STATUS_BYTE status = CC1101TransferByte(transceiver, strobe);
    EndTransfer(transceiver);

    return status;
}

// Begin a transfer to the CC1101 transceiver *THIS HAS TO BE CALLED BEFORE ANY TRANSFER TO THE CC1101*
void BeginTransfer(Transceiver *transceiver)
{
    if (transceiver == NULL)
        return;

    // Set the GPIO pins to the correct state for writing
    gpio_set_level(transceiver->sck_pin, 0);
    gpio_set_level(transceiver->csn_pin, 0);
    gpio_set_level(transceiver->mosi_pin, 0);

    // Wait for the SO pin to go low
    WaitForSO(transceiver);
}
// Transfer a single byte to the CC1101 transceiver and read the status byte
uint8_t CC1101TransferByte(Transceiver *transceiver, uint8_t byte)
{
    uint8_t received = 0;
    // Pass in the byte to the C1101 and read the status byte
    for (int bit = 0; bit < 8; bit++)
    {
        gpio_set_level(transceiver->mosi_pin, (byte >> (7 - bit)) & 0x1);
        delay(1);
        gpio_set_level(transceiver->sck_pin, 1);
        delay(1);
        received = (received << 1) | gpio_get_level(transceiver->miso_pin);
        gpio_set_level(transceiver->sck_pin, 0);
        delay(1);
    }

    return received;
}
// End a transfer to the CC1101 transceiver *THIS HAS TO BE CALLED AFTER ANY TRANSFER TO THE CC1101*
void EndTransfer(Transceiver *transceiver)
{
    if (transceiver == NULL)
        return;

    // Reset the GPIO pins to their default state
    gpio_set_level(transceiver->csn_pin, 1);
    gpio_set_level(transceiver->sck_pin, 0);
}

// Transmit a byte using the CC1101 transceiver
uint8_t CC1101Transmitbyte(Transceiver *transceiver, uint32_t data)
{
    if (transceiver == NULL)
        return 0;

    CC1101SendStrobe(transceiver, STROBE_SIDLE);
    CC1101SendStrobe(transceiver, STROBE_SFTX);

    BeginTransfer(transceiver);
    CC1101TransferByte(transceiver, 0x7F); // Burst TX FIFO
    CC1101TransferByte(transceiver, (data >> 24) & 0xFF);
    CC1101TransferByte(transceiver, (data >> 16) & 0xFF);
    CC1101TransferByte(transceiver, (data >> 8) & 0xFF);
    CC1101TransferByte(transceiver, data & 0xFF);
    EndTransfer(transceiver);

    uint8_t txBytes = 0;
    CC1101ReadRegister(transceiver, TXBYTES, &txBytes);
    printf("TXBYTES before STX = 0x%02X\n", txBytes);

    CC1101SendStrobe(transceiver, STROBE_STX);

    uint8_t marcstate = 0;

    for (int i = 0; i < 200; i++)
    {
        CC1101ReadRegister(transceiver, 0x35, &marcstate);
        CC1101ReadRegister(transceiver, TXBYTES, &txBytes);

        printf("TX loop %d: MARCSTATE=0x%02X TXBYTES=0x%02X\n",
               i,
               marcstate & 0x1F,
               txBytes);

        if ((marcstate & 0x1F) == 0x01) // IDLE
            break;

        vTaskDelay(1 / portTICK_PERIOD_MS);
    }

    CC1101ReadRegister(transceiver, 0x35, &marcstate);

    if ((marcstate & 0x1F) != 0x01)
    {
        printf("TX did not return to IDLE\n");
        CC1101SendStrobe(transceiver, STROBE_SIDLE);
        CC1101SendStrobe(transceiver, STROBE_SFTX);
        return 0;
    }

    return 1;
}
// Receive a byte using the CC1101 transceiver
uint32_t CC1101ReceiveByte(Transceiver *transceiver)
{
    if (transceiver == NULL)
        return 0;

    uint8_t rxBytes = 0;

    for (int i = 0; i < 200; i++) // 200 ms timeout
    {
        CC1101ReadRegister(transceiver, RXBYTES, &rxBytes);

        if (rxBytes & 0x80)
        {
            printf("RX FIFO overflow\n");
            CC1101SendStrobe(transceiver, STROBE_SIDLE);
            CC1101SendStrobe(transceiver, STROBE_SFRX);
            return 0;
        }

        if ((rxBytes & 0x7F) >= 4)
            break;

        vTaskDelay(1 / portTICK_PERIOD_MS);
    }

    CC1101ReadRegister(transceiver, RXBYTES, &rxBytes);

    if ((rxBytes & 0x7F) < 4)
    {
        printf("Timed out waiting for packet. RXBYTES = 0x%02X\n", rxBytes);
        return 0;
    }

    BeginTransfer(transceiver);

    CC1101TransferByte(transceiver, 0xFF); // Burst RX FIFO

    uint32_t data = 0;
    data |= ((uint32_t)CC1101TransferByte(transceiver, 0x00)) << 24;
    data |= ((uint32_t)CC1101TransferByte(transceiver, 0x00)) << 16;
    data |= ((uint32_t)CC1101TransferByte(transceiver, 0x00)) << 8;
    data |= ((uint32_t)CC1101TransferByte(transceiver, 0x00));

    EndTransfer(transceiver);

    CC1101SendStrobe(transceiver, STROBE_SIDLE);
    CC1101SendStrobe(transceiver, STROBE_SFRX);

    return data;
}


// Wait for a CC1101 SO pin to go low
void WaitForSO(Transceiver *transceiver)
{
    if (transceiver == NULL)
        return;

    // Wait for the SO pin to go low
    int timeout = 1000; // Set a timeout value (in microseconds)
    while (gpio_get_level(transceiver->miso_pin) && timeout > 0)
    {
        delay(1); // Wait for 1 microsecond
        timeout--;
    }
    if (timeout == 0)
        printf("Timed out waiting for SO pin to go low\n");
}

// Set the CC1101 Frequency registers
CC1101_STATUS_BYTE CC1101SetFrequency(Transceiver *transceiver, uint32_t frequency)
{
    if (transceiver == NULL || frequency < 240000000 || frequency > 960000000)
        return 0;
    CC1101_STATUS_BYTE status = 0;

    uint32_t finalFreq = (uint32_t)((uint64_t)frequency * (1ULL << 16) / CC1101_CRYSTAL_FREQUENCY);
        
    CC1101WriteRegister(transceiver, FREQ2, (finalFreq >> 16) & 0xFF); 
    CC1101WriteRegister(transceiver, FREQ1, (finalFreq >> 8) & 0xFF); 
    status = CC1101WriteRegister(transceiver, FREQ0, finalFreq & 0xFF);

    return status;
}
// Set the CC1101 Data Rate registers (in bps)
CC1101_STATUS_BYTE CC1101SetDataRate(Transceiver *transceiver, uint32_t dataRate)
{
    if (transceiver == NULL || dataRate <= 600 || dataRate > 406300)
    {
        printf("(CC1101SetDataRate) Data rate out of range, or invalid transceiver\n");
        return 0;
    }

    uint8_t DRATE_M = 0;
    uint8_t DRATE_E = 0;
    for (DRATE_E = 0; DRATE_E < 16; DRATE_E++)
    {
        double temp = ((double)dataRate * (1ULL << 28)) /
                      ((double)CC1101_CRYSTAL_FREQUENCY * (1ULL << DRATE_E));
        temp -= 256;

        if (temp >= 0 && temp <= 255)
        {
            DRATE_M = (uint8_t)(temp+0.5); // Round to the nearest integer
            break;
        }
    }

    if (DRATE_E == 16)
    {
        printf("Data rate out of range\n");
        return 0;
    }

    // Get the correct value to set MDMCFG4 register
    uint8_t mdmcfg4 = 0;
    CC1101ReadRegister(transceiver, MDMCFG4, &mdmcfg4);

    // Keep the 4 MSB, and set the 4 LSB
    mdmcfg4 &= 0xF0;
    mdmcfg4 |= (DRATE_E & 0x0F); 

    CC1101WriteRegister(transceiver, MDMCFG4, mdmcfg4);
    return CC1101WriteRegister(transceiver, MDMCFG3, DRATE_M);
}

// Set the CC1101 Receiver Channel Filter Bandwidth register (in Hz) Returns the selected bandwidth
uint32_t CC1101SetChannelFilterbandwidth(Transceiver *transceiver, uint32_t bandwidth)
{
    if (transceiver == NULL || bandwidth < 58000 || bandwidth > 812000)
    {
        printf("(CC1101SetChannelFilterbandwidth) Bandwidth out of range, or invalid transceiver\n");
        return 0;
    }

    for (int i = 0; i < CC1101_CHANNEL_FILTER_LENGTH; i++)
    {
        if (bandwidth <= channelFilterBandwidths[i].bandwidth)
        {
            uint8_t mdmcfg4 = 0;
            CC1101ReadRegister(transceiver, MDMCFG4, &mdmcfg4);
            uint8_t bits = (channelFilterBandwidths[i].mdmcfg4_CHANBW_E << 2) | 
                            channelFilterBandwidths[i].mdmcfg4_CHANBW_M;
            // Update the channel filter bandwidth settings in mdmcfg4
            mdmcfg4 &= 0x0F; // Clear the CHANBW bits
            mdmcfg4 |= (bits << 4); // Set the 4 MSB
            CC1101WriteRegister(transceiver, MDMCFG4, mdmcfg4);
            return channelFilterBandwidths[i].bandwidth;
        }
    }

    printf("Bandwidth not found in predefined list\n");
    return 0; // Should never reach here if the bandwidth is valid
}
// Set the CC1101 Sync Word Qualifier register
CC1101_STATUS_BYTE CC1101SetSyncWordQualifier(Transceiver *transceiver, CC1101_SYNC_WORD_QUALIFIER qualifier)
{
    if (transceiver == NULL || qualifier > CC1101_SYNC_CS_30_32)
    {
        printf("(CC1101SetSyncWordQualifier) Invalid qualifier or transceiver\n");
        return 0;
    }

    uint8_t currentSyncWordQualifier = 0;
    CC1101ReadRegister(transceiver, MDMCFG2, &currentSyncWordQualifier);

    return CC1101WriteRegister(transceiver, MDMCFG2, 
           (currentSyncWordQualifier & ~(CC1101_SYNC_WORD_QUALIFIER_Msk)) | qualifier);
}
// Set the CC1101 Sync Word registers
CC1101_STATUS_BYTE CC1101SetSyncWord(Transceiver *transceiver, uint16_t syncWord)
{
    if (transceiver == NULL)
        return 0;

    // Seperate the 16 bit sync word into two 8 bit values for the registers
    CC1101_STATUS_BYTE status = 0;
    uint8_t syncWordHigh = (uint8_t)(syncWord >> 8);
    uint8_t syncWordLow = (uint8_t)(syncWord);

    // Write these values to the regsiters
    CC1101WriteRegister(transceiver, SYNC1, syncWordHigh);
    status = CC1101WriteRegister(transceiver, SYNC0, syncWordLow);

    // Read back the values to verify they were set correctly
    uint8_t readBackHigh = 0;
    uint8_t readBackLow = 0;
    CC1101ReadRegister(transceiver, SYNC0, &readBackLow);
    CC1101ReadRegister(transceiver, SYNC1, &readBackHigh);

    // Confirm that these value were set successfully
    if (readBackHigh != syncWordHigh ||
        readBackLow != syncWordLow)
    {
        printf("(CC1101SetSyncWord) Failed to read back sync word registers. Sync word is 0x%d\n", syncWord);
        return 0;
    }

    return status;
}
// Set the CC1101 Minimum Preamble Byte Length
CC1101_STATUS_BYTE CC1101SetMinimumPreamble(Transceiver *transceiver, CC1101_PREAMBLE_LENGTH preambleLength)
{
    if (transceiver == NULL || preambleLength > CC1101_PREAMBLE_24_BYTES)
    {
        printf("(CC1101SetMinimumPreamble) Invalid preamble length or transceiver\n");
        return 0;
    }

    // Just read the current value of MDMCFG1 to preserve the other settings
    uint8_t currentMDMCFG1 = 0;
    CC1101ReadRegister(transceiver, MDMCFG1, &currentMDMCFG1); 

    // Set the value we want to currentMDMCFG1
    currentMDMCFG1 &= ~(MDMCFG1_PREAMBLE_LENGTH_Msk);
    currentMDMCFG1 |= (preambleLength << CC1101_PREAMBLE_LENGTH_POSITION);

    // Write the new value to the register
    CC1101_STATUS_BYTE status = CC1101WriteRegister(transceiver, MDMCFG1, currentMDMCFG1);

    // Read back the value to verify it was set correctly
    uint8_t readBack = 0;
    CC1101ReadRegister(transceiver, MDMCFG1, &readBack);

    // Confirm that the value was set successfully
    if (readBack != currentMDMCFG1)
    {
        printf("(CC1101SetMinimumPreamble) Failed to read back MDMCFG1 register. Value is 0x%02X\n", readBack);
        return 0;
    }

    return status;
}
// Set the CC1101 Packet Length
CC1101_STATUS_BYTE CC1101SetPacketLength(Transceiver *transceiver, CC1101_PACKET_LENGTH packetLength)
{
    if (transceiver == NULL || packetLength == 0)
    {
        printf("(CC1101SetPacketLength) Invalid packet length or transceiver\n");
        return 0;
    }

    CC1101_STATUS_BYTE status = CC1101WriteRegister(transceiver, PKTLEN, packetLength);

    uint8_t readBack = 0;
    CC1101ReadRegister(transceiver, PKTLEN, &readBack);

    if (readBack != packetLength)
    {
        printf("(CC1101SetPacketLength) Failed to read back PKTLEN register. Value is 0x%02X\n", readBack);
        return 0;
    }

    (*transceiver).packet_length = packetLength;
    return status;
}
// Set the CC1101 Packet Type
CC1101_STATUS_BYTE CC1101SetPacketType(Transceiver *transceiver, CC1101_PACKET_TYPE packetType)
{
    if (transceiver == NULL || packetType > CC1101_PACKET_TYPE_INFINITE)
    {
        printf("(CC1101SetPacketType) Invalid packet type or transceiver\n");
        return 0;
    }

    uint8_t currentPKTCTRL0 = 0;
    CC1101ReadRegister(transceiver, PKTCTRL0, &currentPKTCTRL0);

    currentPKTCTRL0 &= ~(0x03); // Clear the packet type bits
    currentPKTCTRL0 |= packetType; // Set the packet type bits
    

    CC1101_STATUS_BYTE status = CC1101WriteRegister(transceiver, PKTCTRL0, currentPKTCTRL0);

    uint8_t readBack = 0;
    CC1101ReadRegister(transceiver, PKTCTRL0, &readBack);


    (*transceiver).packetType = packetType;
    return status;
}
// Flush the CC1101 RX and TX FIFOs
CC1101_STATUS_BYTE CC1101FlushFIFO(Transceiver *transceiver)
{
    if (transceiver == NULL)
    {
        printf("(CC1101FlushFIFO) Invalid transceiver\n");
        return 0;
    }

    CC1101SendStrobe(transceiver, STROBE_SFTX); // Flush the TX FIFO
    return CC1101SendStrobe(transceiver, STROBE_SFRX); // Flush the RX FIFO
}





void CC1101SetPATable(Transceiver *transceiver, uint8_t power)
{
    if (transceiver == NULL)
        return;

    BeginTransfer(transceiver);
    CC1101TransferByte(transceiver, 0x3E); // PATABLE single write
    CC1101TransferByte(transceiver, power);
    EndTransfer(transceiver);
}


