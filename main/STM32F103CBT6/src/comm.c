#include "comm.h"
#include "board_config.h"
#include <stdio.h>

#define COMM_NO_FRAME 0xFFU

/* 每块 512B DMA 数据 + 1B 终止符；两块交替，禁止逐字节中断。 */
static uint8_t g_rx_buffer[COMM_RX_BUFFER_COUNT][COMM_RX_FRAME_MAX + 1U];
static volatile uint8_t g_active_buffer;
static volatile uint8_t g_ready_buffer = COMM_NO_FRAME;
static volatile uint16_t g_ready_length;
static volatile uint32_t g_oversize_count;
static volatile uint32_t g_drop_count;
static volatile uint32_t g_capture_done_seq;
static volatile int32_t g_capture_done_frames;

static void comm_rearm(uint8_t index)
{
    /* 仅改 CNDTR 不会切换内存地址；CMAR 必须同步指向对应的乒乓缓冲。 */
    COMM_DMA_CHANNEL->CMAR = (uint32_t)&g_rx_buffer[index][0];
    DMA_SetCurrDataCounter(COMM_DMA_CHANNEL, COMM_RX_FRAME_MAX);
    DMA_ClearFlag(DMA1_FLAG_TC6 | DMA1_FLAG_HT6 | DMA1_FLAG_TE6);
    g_active_buffer = index;
    DMA_Cmd(COMM_DMA_CHANNEL, ENABLE);
}

void comm_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    DMA_InitTypeDef dma;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(COMM_USART_CLK, ENABLE);
    RCC_AHBPeriphClockCmd(COMM_DMA_CLK, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = COMM_TX_PIN;
    GPIO_Init(COMM_TX_PORT, &gpio);
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpio.GPIO_Pin = COMM_RX_PIN;
    GPIO_Init(COMM_RX_PORT, &gpio);

    USART_StructInit(&usart);
    /* 72MHz 下 BRR 分频为 78，实际 923076 baud，较 921600 快约 0.16%。 */
    usart.USART_BaudRate = COMM_BAUD;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(COMM_USART, &usart);

    DMA_DeInit(COMM_DMA_CHANNEL);
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&COMM_USART->DR;
    dma.DMA_MemoryBaseAddr = (uint32_t)&g_rx_buffer[0][0];
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = COMM_RX_FRAME_MAX;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(COMM_DMA_CHANNEL, &dma);

    nvic.NVIC_IRQChannel = COMM_USART_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic.NVIC_IRQChannelSubPriority = 1U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    g_active_buffer = 0U;
    g_ready_buffer = COMM_NO_FRAME;
    g_ready_length = 0U;
    g_oversize_count = 0U;
    g_drop_count = 0U;
    g_capture_done_seq = 0U;
    g_capture_done_frames = 0;
    USART_ClearFlag(COMM_USART, USART_FLAG_IDLE);
    USART_DMACmd(COMM_USART, USART_DMAReq_Rx, ENABLE);
    USART_ITConfig(COMM_USART, USART_IT_IDLE, ENABLE);
    USART_Cmd(COMM_USART, ENABLE);
    DMA_Cmd(COMM_DMA_CHANNEL, ENABLE);
}

void comm_usart2_irq(void)
{
    volatile uint32_t dummy;
    uint32_t status;
    uint16_t remaining;
    uint16_t length;
    uint8_t completed;
    uint8_t overrun;

    if (USART_GetITStatus(COMM_USART, USART_IT_IDLE) == RESET) { return; }
    status = COMM_USART->SR;
    overrun = ((status & USART_SR_ORE) != 0U) ? 1U : 0U;
    dummy = status;
    dummy = COMM_USART->DR;             /* SR 后读 DR，清 IDLE/ORE；不打印、不延时。 */
    (void)dummy;

    DMA_Cmd(COMM_DMA_CHANNEL, DISABLE);
    remaining = DMA_GetCurrDataCounter(COMM_DMA_CHANNEL);
    length = (uint16_t)(COMM_RX_FRAME_MAX - remaining);
    completed = g_active_buffer;

    if (length == 0U) {
        comm_rearm(completed);
        return;
    }
    /* 缓冲已满时无法区分“刚好 512B”与后续溢出，整帧丢弃最安全。 */
    if (overrun || length >= COMM_RX_FRAME_MAX) {
        g_rx_buffer[completed][0] = 0U;
        ++g_oversize_count;
        comm_rearm(completed);
        return;
    }

    g_rx_buffer[completed][length] = 0U;
    if (g_ready_buffer == COMM_NO_FRAME) {
        g_ready_length = length;
        g_ready_buffer = completed;
        comm_rearm((uint8_t)(completed ^ 1U));
    } else {
        /* 前一帧尚未由主循环释放，丢弃新帧而不是覆盖正在解析的数据。 */
        ++g_drop_count;
        comm_rearm(completed);
    }
}

uint8_t comm_frame_available(void)
{
    return (g_ready_buffer != COMM_NO_FRAME) ? 1U : 0U;
}

const char *comm_acquire_frame(uint16_t *length)
{
    if (g_ready_buffer == COMM_NO_FRAME || length == 0) { return 0; }
    *length = g_ready_length;
    return (const char *)&g_rx_buffer[g_ready_buffer][0];
}

void comm_release_frame(void)
{
    __DMB();
    g_ready_buffer = COMM_NO_FRAME;
}

uint32_t comm_oversize_count(void)
{
    return g_oversize_count;
}

uint32_t comm_drop_count(void)
{
    return g_drop_count;
}

static void comm_write(const char *text)
{
    while (*text != '\0') {
        uint32_t timeout = UART_TX_TIMEOUT;
        while (USART_GetFlagStatus(COMM_USART, USART_FLAG_TXE) == RESET) {
            if (--timeout == 0U) { return; }
        }
        USART_SendData(COMM_USART, (uint8_t)*text++);
    }
}

void comm_send_ready(void)
{
    comm_write("{\"cmd\":\"ready\"}\n");
}

void comm_send_ack(int32_t id)
{
    char frame[48];
    (void)snprintf(frame, sizeof(frame), "{\"cmd\":\"ack\",\"id\":%ld}\n", (long)id);
    comm_write(frame);
}

void comm_send_capture(uint32_t sequence, uint32_t frames)
{
    char frame[80];
    g_capture_done_seq = 0U;
    g_capture_done_frames = 0;
    (void)snprintf(frame, sizeof(frame),
                   "{\"cmd\":\"capture\",\"seq\":%lu,\"frames\":%lu}\n",
                   (unsigned long)sequence, (unsigned long)frames);
    comm_write(frame);
}

void comm_mark_capture_done(uint32_t sequence, int32_t captured_frames)
{
    g_capture_done_frames = captured_frames;
    __DMB();
    g_capture_done_seq = sequence;
}

uint8_t comm_capture_completed(uint32_t sequence)
{
    return (sequence != 0U && g_capture_done_seq == sequence) ? 1U : 0U;
}

int32_t comm_capture_frames(void)
{
    return g_capture_done_frames;
}
