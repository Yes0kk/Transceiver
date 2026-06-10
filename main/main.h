#include <stdio.h>
#include <string.h>
#include "c1101/c1101.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "esp_timer.h"


void Setup(void);
void DataLoop(void);
void PrintStatusByte(CC1101_STATUS_BYTE status);
void Test(void);
char *ByteToBinary(uint8_t value);
void CheckValues(void);
void Setupa(void);

