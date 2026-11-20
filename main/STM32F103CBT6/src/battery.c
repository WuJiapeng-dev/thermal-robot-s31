#include "battery.h"
#include "board_config.h"

static uint16_t adc_read_once(void)
{
    uint32_t timeout = ADC_CONVERSION_TIMEOUT;

    ADC_ClearFlag(BATTERY_ADC, ADC_FLAG_EOC);
    ADC_SoftwareStartConvCmd(BATTERY_ADC, ENABLE);
    while (ADC_GetFlagStatus(BATTERY_ADC, ADC_FLAG_EOC) == RESET) {
        if (--timeout == 0U) {
            return 0U;
        }
    }
    return ADC_GetConversionValue(BATTERY_ADC);
}

void battery_init(void)
{
    GPIO_InitTypeDef gpio;
    ADC_InitTypeDef adc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | BATTERY_ADC_CLK, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = BATTERY_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(BATTERY_PORT, &gpio);

    ADC_DeInit(BATTERY_ADC);
    ADC_StructInit(&adc);
    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1U;
    ADC_Init(BATTERY_ADC, &adc);
    ADC_RegularChannelConfig(BATTERY_ADC, BATTERY_ADC_CHANNEL, 1U, ADC_SampleTime_55Cycles5);
    ADC_Cmd(BATTERY_ADC, ENABLE);

    ADC_ResetCalibration(BATTERY_ADC);
    while (ADC_GetResetCalibrationStatus(BATTERY_ADC) != RESET) {
    }
    ADC_StartCalibration(BATTERY_ADC);
    while (ADC_GetCalibrationStatus(BATTERY_ADC) != RESET) {
    }
}

uint16_t battery_read_raw(void)
{
    uint16_t samples[BATTERY_MEDIAN_SAMPLES];
    uint8_t i;
    uint8_t j;

    for (i = 0U; i < BATTERY_MEDIAN_SAMPLES; ++i) {
        samples[i] = adc_read_once();
    }
    for (i = 0U; i < BATTERY_MEDIAN_SAMPLES; ++i) {
        for (j = (uint8_t)(i + 1U); j < BATTERY_MEDIAN_SAMPLES; ++j) {
            if (samples[j] < samples[i]) {
                uint16_t temp = samples[i];
                samples[i] = samples[j];
                samples[j] = temp;
            }
        }
    }
    return samples[BATTERY_MEDIAN_SAMPLES / 2U];
}

uint32_t battery_read_millivolts(void)
{
    uint32_t raw = battery_read_raw();
    uint32_t divider_mv = (raw * ADC_VDDA_MV) / ADC_FULL_SCALE;
    return divider_mv * (BATTERY_DIVIDER_TOP_KOHM + BATTERY_DIVIDER_BOTTOM_KOHM) /
           BATTERY_DIVIDER_BOTTOM_KOHM;
}

uint8_t battery_percent(uint32_t millivolts)
{
    if (millivolts <= BATTERY_EMPTY_MV) {
        return 0U;
    }
    if (millivolts >= BATTERY_FULL_MV) {
        return 100U;
    }
    return (uint8_t)(((millivolts - BATTERY_EMPTY_MV) * 100U) /
                     (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

uint8_t battery_is_undervoltage(uint32_t millivolts)
{
    return (millivolts < BATTERY_UNDERVOLTAGE_MV) ? 1U : 0U;
}
