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
uint8_t CC1101Transmitbyte(Transceiver *transceiver, uint8_t *byte, uint8_t length)
{
    if (transceiver == NULL)
        return 0;
    BeginTransfer(transceiver);
    CC1101TransferByte(transceiver, 0x7F);

    CC1101TransferByte(transceiver, length+2); // Do +2 to add two blank bytes
    for (int currentByte = 0; currentByte < length; currentByte++)
    {
        CC1101TransferByte(transceiver, byte[currentByte]);
    }
    CC1101TransferByte(transceiver, 0x00);
    CC1101TransferByte(transceiver, 0x00);
    EndTransfer(transceiver);

    CC1101SendStrobe(transceiver, STROBE_STX);

    uint8_t marc = 0;
    int64_t start = esp_timer_get_time();

    do
    {
        CC1101ReadRegister(transceiver, MARCSTATE, &marc);
        marc &= 0x1F;

        if ((esp_timer_get_time() - start) > 100000)
        {
            printf("TX timeout, MARCSTATE=0x%02X\n", marc);
            CC1101SendStrobe(transceiver, STROBE_SIDLE);
            CC1101FlushTX(transceiver);
            return 0;
        }

        vTaskDelay(pdMS_TO_TICKS(1));

    } while (marc != 0x01); // IDLE

    return 1;
}
// Receive a byte using the CC1101 transceiver
bool CC1101ReceiveByte(Transceiver *transceiver, uint8_t *buffer, uint8_t *length)
{
    if (transceiver == NULL || buffer == NULL || length == NULL)
        return false;

    uint8_t rxBytes = 0;
    int64_t start = esp_timer_get_time();

    // 1. Wait until at least the variable-length byte is available
    while ((esp_timer_get_time() - start) < 10000)
    {
        CC1101ReadRegister(transceiver, RXBYTES, &rxBytes);

        if (rxBytes & 0x80)
        {
            printf("RX FIFO overflow\n");
            CC1101SendStrobe(transceiver, STROBE_SIDLE);
            CC1101FlushRX(transceiver);
            return false;
        }

        if ((rxBytes & 0x7F) >= 1)
            break;

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if ((rxBytes & 0x7F) < 1)
        return false;

    // 2. Read the length byte
    BeginTransfer(transceiver);
    CC1101TransferByte(transceiver, 0xFF);   // RX FIFO burst read
    *length = CC1101TransferByte(transceiver, 0x00);
    EndTransfer(transceiver);

    // 3. Validate length
    if (*length == 0 || *length > 64)
    {
        printf("Invalid packet length: %u\n", *length);
        CC1101SendStrobe(transceiver, STROBE_SIDLE);
        CC1101FlushRX(transceiver);
        return false;
    }

    // 4. Wait until the full payload is in RX FIFO
    start = esp_timer_get_time();

    while ((esp_timer_get_time() - start) < 10000)
    {
        CC1101ReadRegister(transceiver, RXBYTES, &rxBytes);

        if (rxBytes & 0x80)
        {
            printf("RX FIFO overflow\n");
            CC1101SendStrobe(transceiver, STROBE_SIDLE);
            CC1101FlushRX(transceiver);
            return false;
        }

        if ((rxBytes & 0x7F) >= *length)
            break;

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if ((rxBytes & 0x7F) < *length)
    {
        printf("RX timeout. Have %u bytes, need %u bytes\n", rxBytes & 0x7F, *length);
        CC1101SendStrobe(transceiver, STROBE_SIDLE);
        CC1101FlushRX(transceiver);
        return false;
    }

    // 5. Read exactly payload length bytes
    BeginTransfer(transceiver);
    CC1101TransferByte(transceiver, 0xFF);   // RX FIFO burst read

    for (uint8_t i = 0; i < *length; i++)
        buffer[i] = CC1101TransferByte(transceiver, 0x00);

    EndTransfer(transceiver);

    // Optional null terminator for printf("%s")
    buffer[*length] = '\0';

    CC1101SendStrobe(transceiver, STROBE_SIDLE);
    CC1101FlushRX(transceiver);
    CC1101SendStrobe(transceiver, STROBE_SRX);

    return true;
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
CC1101_STATUS_BYTE CC1101SetBitRate(Transceiver *transceiver, uint32_t dataRate)
{
    if (transceiver == NULL || dataRate <= 600 || dataRate > 406300)
    {
        printf("(CC1101SetBitRate) Data rate out of range, or invalid transceiver\n");
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
            DRATE_M = (uint8_t)(temp + 0.5); // Round to the nearest integer
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
            mdmcfg4 &= 0x0F;        // Clear the CHANBW bits
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

    currentPKTCTRL0 &= ~(0x03);    // Clear the packet type bits
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

    CC1101SendStrobe(transceiver, STROBE_SFTX);        // Flush the TX FIFO
    return CC1101SendStrobe(transceiver, STROBE_SFRX); // Flush the RX FIFO
}

// Transceiver CreateTransceiver(void)

// Set the CC1101 Modulation Format (2-FSK, GFSK, ASK/OOK, 4-FSK, MSK)
CC1101_STATUS_BYTE CC1101SetModulationFormat(Transceiver *transceiver, CC1101_MOD_FORMAT modulationFormat)
{
    if (transceiver == NULL || !CC1101IsValidModulationFormat(modulationFormat))
    {
        printf("(CC1101SetModulationFormat) Invalid modulation format or transceiver\n");
        return 0;
    }

    uint8_t currentMDMCFG2 = 0;
    CC1101ReadRegister(transceiver, MDMCFG2, &currentMDMCFG2);

    currentMDMCFG2 &= ~CC1101_MOD_FORMAT_Msk;                      // Clear the modulation format bits
    currentMDMCFG2 |= (modulationFormat << CC1101_MOD_FORMAT_Pos); // Set the modulation format bits

    return CC1101WriteRegister(transceiver, MDMCFG2, currentMDMCFG2);
}
// Set the CC1101 Frequency Deviation (in Hz)
CC1101_STATUS_BYTE CC1101SetFrequencyDeviation(Transceiver *transceiver, uint32_t frequencyDeviation)
{
    if (transceiver == NULL || frequencyDeviation == 0)
    {
        printf("(CC1101SetFrequencyDeviation) Invalid frequency deviation or transceiver\n");
        return 0;
    }

    uint32_t closest = UINT32_MAX;
    uint8_t closest_dev_E = 0;
    uint8_t closest_dev_M = 0;
    for (uint8_t dev_E = 0; dev_E < 8; dev_E++)
    {
        for (uint8_t dev_M = 0; dev_M < 8; dev_M++)
        {
            uint32_t current = ((uint64_t)CC1101_CRYSTAL_FREQUENCY) / ((1 << 17)) * (8 + dev_M) * (1 << dev_E);

            uint32_t error = 0;
            if (current < frequencyDeviation)
                error = frequencyDeviation - current;
            else
                error = current - frequencyDeviation;

            if (error < closest)
            {
                closest = error;
                closest_dev_E = dev_E;
                closest_dev_M = dev_M;
            }
        }
    }

    uint8_t currentDEVIATN = 0;
    CC1101ReadRegister(transceiver, DEVIATN, &currentDEVIATN);

    currentDEVIATN &= ~(CC1101_DEVIATION_E_Msk | CC1101_DEVIATION_M_Msk);        // Clear the frequency deviation bits
    currentDEVIATN |= (closest_dev_E << CC1101_DEVIATION_E_Pos) | closest_dev_M; // Set the frequency deviation bits

    return CC1101WriteRegister(transceiver, DEVIATN, currentDEVIATN);
}
// Set the CC1101 CRC
CC1101_STATUS_BYTE CC1101SetCRC(Transceiver *transceiver, bool state)
{
    if (transceiver == NULL)
    {
        printf("(CC1101SetCRC) Invalid transceiver.");
    }

    uint8_t currentPKTCTRL0 = 0;
    CC1101ReadRegister(transceiver, PKTCTRL0, &currentPKTCTRL0);

    currentPKTCTRL0 &= ~(CC1101_CRC_EN_Msk);
    currentPKTCTRL0 |= (state << CC1101_CRC_EN_Pos);

    return CC1101WriteRegister(transceiver, PKTCTRL0, currentPKTCTRL0);
}
// Set the CC1101 CRC Autoflush
CC1101_STATUS_BYTE CC1101SetCRCAutoFlush(Transceiver *transceiver, bool state)
{
    if (transceiver == NULL)
    {
        printf("(CC1101SetCRC) Invalid transceiver.");
    }

    uint8_t currentPKTCTRL1 = 0;
    CC1101ReadRegister(transceiver, PKTCTRL1, &currentPKTCTRL1);

    currentPKTCTRL1 &= ~(CC1101_CRC_AUTOFLUSH_Msk);
    currentPKTCTRL1 |= (state << CC1101_CRC_AUTOFLUSH_Pos);

    return CC1101WriteRegister(transceiver, PKTCTRL1, currentPKTCTRL1);
}
CC1101_STATUS_BYTE CC1101SetAddressFiltering(Transceiver *transceiver, CC1101_ADR_CHK state)
{
    if (transceiver == NULL || state > 0x03)
    {
        printf("(CC1101SetCRC) Invalid transceiver or state.");
    }

    uint8_t currentMDMCFG2 = 0;
    CC1101ReadRegister(transceiver, PKTCTRL1, &currentMDMCFG2);

    currentMDMCFG2 &= ~(CC1101_ADR_CHK_Msk);
    currentMDMCFG2 |= (state << CC1101_ADR_CHK_Pos);

    return CC1101WriteRegister(transceiver, PKTCTRL1, currentMDMCFG2);
}

CC1101_STATUS_BYTE CC1101SetGDO0(Transceiver *transceiver, uint8_t setting);

CC1101_STATUS_BYTE CC1101SetEncoding(Transceiver *transceiver, uint8_t encoding)
{
    uint8_t reg = 0;
    CC1101ReadRegister(transceiver, PKTCTRL0, &reg);
    reg &= ~(0x40);
    CC1101WriteRegister(transceiver, PKTCTRL0, reg);

    CC1101ReadRegister(transceiver, MDMCFG2, &reg);
    reg &= ~(0x08);
    return CC1101WriteRegister(transceiver, MDMCFG2, reg);
}

CC1101_STATUS_BYTE CC1101CommonSetup(Transceiver *transceiver)
{
    CC1101_STATUS_BYTE status = 0;

    CC1101Reset(transceiver);

    status = CC1101SetFrequency(transceiver, 433456789);
    // printf("CC1101 Frequency: %u\n", status);

    status = CC1101SetBitRate(transceiver, 100000);
    // printf("CC1101 BitRate: %u\n", status);

    status = CC1101SetChannelFilterbandwidth(transceiver, 250000);
    // printf("CC1101 Bandwidth: %u\n", status);

    status = CC1101SetFrequencyDeviation(transceiver, 50000);
    // printf("CC1101 FrequencyDeviation: %u\n", status);

    status = CC1101SetPacketType(transceiver, CC1101_PACKET_TYPE_VARIABLE);
    // printf("CC1101 PacketType: %u\n", status);

    status = CC1101SetPacketLength(transceiver, 0x40); // Maximum packet length
    // printf("CC1101 PacketLength: %u\n", status);

    status = CC1101SetMinimumPreamble(transceiver, CC1101_PREAMBLE_2_BYTES);
    // printf("CC1101 PreambleLength: %u\n", status);

    status = CC1101SetModulationFormat(transceiver, CC1101_MOD_FORMAT_2FSK);
    // printf("CC1101 DataShapingg: %u\n", status);

    status = CC1101SetEncoding(transceiver, 0);
    // printf("CC1101 Encoding: %u\n", status);

    status = CC1101SetSyncWord(transceiver, 0xD391);
    // printf("CC1101 SyncWord: %u\n", status);

    status = CC1101SetWhitening(transceiver, 0);

    status = CC1101SetAddressFiltering(transceiver, 0);

    status = CC1101SetCRC(transceiver, 1);
    CC1101SetCRCAutoFlush(transceiver, 0);

    CC1101StatusBytes(transceiver, 0);

    CC1101StatusBytes(transceiver, 0);
    CC1101RXOff(transceiver, CC1101_PACKET_RECEIVED_IDLE);
    CC1101TXOff(transceiver, CC1101_PACKET_SENT_IDLE);

    status = CC1101SetAutoCalibration(transceiver, CC1101_AUTOCALIBRATION_CHANGETORXTX);

    status = CC1101FlushRX(transceiver);
    status = CC1101FlushTX(transceiver);

    CC1101SendStrobe(transceiver, STROBE_SIDLE);

    return status;
}

CC1101_STATUS_BYTE CC1101FlushTX(Transceiver *transceiver)
{
    if (transceiver == NULL)
    {
        printf("(CC1101FlushTX) Invalid transceiver\n");
        return 0;
    }

    CC1101SendStrobe(transceiver, STROBE_SIDLE);
    esp_rom_delay_us(1);

    return CC1101SendStrobe(transceiver, STROBE_SFTX);
}

CC1101_STATUS_BYTE CC1101FlushRX(Transceiver *transceiver)
{
    if (transceiver == NULL)
    {
        printf("(CC1101FlushRX) Invalid transceiver\n");
        return 0;
    }

    CC1101SendStrobe(transceiver, STROBE_SIDLE);
    esp_rom_delay_us(1);

    return CC1101SendStrobe(transceiver, STROBE_SFRX);
}

CC1101_STATUS_BYTE CC1101SetWhitening(Transceiver *transceiver, bool state)
{

    if (transceiver == NULL)
    {
        printf("(CC1101SetCRC) Invalid transceiver.");
    }

    uint8_t currentPKTCTRL0 = 0;
    CC1101ReadRegister(transceiver, PKTCTRL0, &currentPKTCTRL0);

    currentPKTCTRL0 &= ~(CC1101_WHITENING_Msk);
    currentPKTCTRL0 |= (state << CC1101_WHITENING_Pos);

    return CC1101WriteRegister(transceiver, PKTCTRL0, currentPKTCTRL0);
}

CC1101_STATUS_BYTE CC1101SetAutoCalibration(Transceiver *transceiver, CC1101_AUTOCALIBRATION calibration)
{
    if (transceiver == NULL)
    {
        printf("(CC1101SetAutoCalibration) Invalid transceiver.");
    }

    uint8_t regMCSM0 = 0;
    CC1101ReadRegister(transceiver, MCSM0, &regMCSM0);

    regMCSM0 &= ~(CC1101_AUTOCALIBRATION_Msk);
    regMCSM0 |= calibration << CC1101_AUTOCALIBRATION_Pos;

    return CC1101WriteRegister(transceiver, MCSM0, regMCSM0);
}

CC1101_STATUS_BYTE CC1101RXOff(Transceiver *transceiver, CC1101_PACKET_RECEIVED type)
{
    if (transceiver == NULL)
    {
        printf("(CC1101RXOff) Invalid transceiver.");
    }

    uint8_t regMCSM1 = 0;
    CC1101ReadRegister(transceiver, MCSM1, &regMCSM1);

    regMCSM1 &= ~(CC1101_RXOFF_MODE_Msk);
    regMCSM1 |= type << CC1101_RXOFF_MODE_Pos;

    return CC1101WriteRegister(transceiver, MCSM1, regMCSM1);
}

CC1101_STATUS_BYTE CC1101TXOff(Transceiver * transceiver, CC1101_PACKET_SENT type)
{
    if (transceiver == NULL)
    {
        printf("(CC1101RXOff) Invalid transceiver.");
    }

    uint8_t regMCSM1 = 0;
    CC1101ReadRegister(transceiver, MCSM1, &regMCSM1);

    regMCSM1 &= ~(CC1101_TXOFF_MODE_Msk);
    regMCSM1 |= type << CC1101_TXOFF_MODE_Pos;

    return CC1101WriteRegister(transceiver, MCSM1, regMCSM1);
}

CC1101_STATUS_BYTE CC1101StatusBytes(Transceiver *transceiver, bool state)
{
    if (transceiver == NULL)
    {
        printf("(CC1101RXOff) Invalid transceiver.");
    }

    uint8_t regPKTCTRL1 = 0;
    CC1101ReadRegister(transceiver, PKTCTRL1, &regPKTCTRL1);

    regPKTCTRL1 &= ~(CC1101_APPEND_STATUS_Msk);
    regPKTCTRL1 |= state << CC1101_APPEND_STATUS_Pos;

    return CC1101WriteRegister(transceiver, PKTCTRL1, regPKTCTRL1);
}

// HELPERS

static inline bool CC1101IsValidModulationFormat(CC1101_MOD_FORMAT format)
{
    switch (format)
    {
    case CC1101_MOD_FORMAT_2FSK:
    case CC1101_MOD_FORMAT_GFSK:
    case CC1101_MOD_FORMAT_ASK_OOK:
    case CC1101_MOD_FORMAT_4FSK:
    case CC1101_MOD_FORMAT_MSK:
        return true;

    default:
        return false;
    }
}