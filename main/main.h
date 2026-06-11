#include <stdio.h>
#include <string.h>
#include "c1101/c1101.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "esp_timer.h"

void SetupTransceivers(void);
void DataLoop(void);
void Test(void);

