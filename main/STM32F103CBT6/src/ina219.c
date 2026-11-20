#include "ina219.h"
#include "board_config.h"
#include "timebase.h"

#if ENABLE_MOTION_DRIVE
static uint8_t g_initialized;

static uint8_t write_register(uint8_t reg, uint16_t value)
{
    uint32_t timeout = I2C_TIMEOUT;
    I2C_GenerateSTART(INA219_I2C, ENABLE);
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_MODE_SELECT) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    I2C_Send7bitAddress(INA219_I2C, (uint8_t)(INA219_ADDRESS_7BIT << 1), I2C_Direction_Transmitter);
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    I2C_SendData(INA219_I2C, reg);
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    I2C_SendData(INA219_I2C, (uint8_t)(value >> 8));
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    I2C_SendData(INA219_I2C, (uint8_t)value);
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    I2C_GenerateSTOP(INA219_I2C, ENABLE);
    return 1U;
}

static uint8_t read_register(uint8_t reg, uint16_t *value)
{
    uint32_t timeout = I2C_TIMEOUT;
    I2C_GenerateSTART(INA219_I2C, ENABLE);
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_MODE_SELECT) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    I2C_Send7bitAddress(INA219_I2C, (uint8_t)(INA219_ADDRESS_7BIT << 1), I2C_Direction_Transmitter);
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    I2C_SendData(INA219_I2C, reg);
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    I2C_GenerateSTART(INA219_I2C, ENABLE);
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_MODE_SELECT) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    I2C_Send7bitAddress(INA219_I2C, (uint8_t)(INA219_ADDRESS_7BIT << 1), I2C_Direction_Receiver);
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_BYTE_RECEIVED) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    *value = (uint16_t)(I2C_ReceiveData(INA219_I2C) << 8);
    I2C_AcknowledgeConfig(INA219_I2C, DISABLE);
    I2C_GenerateSTOP(INA219_I2C, ENABLE);
    timeout = I2C_TIMEOUT;
    while (I2C_CheckEvent(INA219_I2C, I2C_EVENT_MASTER_BYTE_RECEIVED) == ERROR) {
        if (--timeout == 0U) { return 0U; }
    }
    *value |= I2C_ReceiveData(INA219_I2C);
    I2C_AcknowledgeConfig(INA219_I2C, ENABLE);
    return 1U;
}

#endif /* ENABLE_MOTION_DRIVE */

void ina219_init(void)
{
#if ENABLE_MOTION_DRIVE
    GPIO_InitTypeDef gpio;
    I2C_InitTypeDef i2c;

    if (g_initialized) {
        return;
    }
    RCC_APB1PeriphClockCmd(INA219_I2C_CLK, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    /* 已在 board_init 执行 I2C1 remap：PB8/PB9；PB6/PB7 留给 TIM4。 */
    GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = INA219_SCL_PIN | INA219_SDA_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    I2C_DeInit(INA219_I2C);
    I2C_StructInit(&i2c);
    i2c.I2C_Mode = I2C_Mode_I2C;
    i2c.I2C_DutyCycle = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1 = 0U;
    i2c.I2C_Ack = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    i2c.I2C_ClockSpeed = INA219_I2C_SPEED;
    I2C_Init(INA219_I2C, &i2c);
    I2C_Cmd(INA219_I2C, ENABLE);

    (void)write_register(0x00U, INA219_CONFIG);
    (void)write_register(0x05U, INA219_CALIBRATION);
    g_initialized = 1U;
#endif
}

uint8_t ina219_read(uint32_t *bus_mv, int32_t *current_ma, uint32_t *power_mw)
{
#if ENABLE_MOTION_DRIVE
    uint16_t bus_raw;
    uint16_t current_raw;
    uint16_t power_raw;

    if (!g_initialized) {
        ina219_init();
    }
    if (!read_register(0x02U, &bus_raw) ||
        !read_register(0x04U, &current_raw) ||
        !read_register(0x03U, &power_raw)) {
        return 0U;
    }
    *bus_mv = (uint32_t)bus_raw * 4U / 10U;
    *current_ma = (int32_t)((int16_t)current_raw) / 10;
    *power_mw = (uint32_t)power_raw * 2U;
    return 1U;
#else
    (void)bus_mv;
    (void)current_ma;
    (void)power_mw;
    return 0U;
#endif
}
