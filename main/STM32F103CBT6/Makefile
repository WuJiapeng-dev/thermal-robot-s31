TARGET := thermal_robot_stm32
BUILD_DIR := build
SPL_DIR ?= $(abspath ../spl)

CROSS_COMPILE ?= arm-none-eabi-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE := $(CROSS_COMPILE)size

SPL_DRIVER_DIR := $(SPL_DIR)/Libraries/STM32F10x_StdPeriph_Driver
CMSIS_CORE_DIR := $(SPL_DIR)/Libraries/CMSIS/CM3/CoreSupport
CMSIS_DEVICE_DIR := $(SPL_DIR)/Libraries/CMSIS/CM3/DeviceSupport/ST/STM32F10x

APP_SRCS := $(wildcard src/*.c)
SPL_NAMES := stm32f10x_adc.c stm32f10x_dma.c stm32f10x_gpio.c \
	stm32f10x_i2c.c stm32f10x_rcc.c stm32f10x_spi.c \
	stm32f10x_tim.c stm32f10x_usart.c misc.c
APP_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
SPL_OBJS := $(addprefix $(BUILD_DIR)/,$(SPL_NAMES:.c=.o))
OBJS := $(APP_OBJS) $(SPL_OBJS) \
	$(BUILD_DIR)/system_stm32f10x.o $(BUILD_DIR)/startup_stm32f10x_md.o

CPPFLAGS := -Iinc -I$(SPL_DRIVER_DIR)/inc -I$(CMSIS_CORE_DIR) -I$(CMSIS_DEVICE_DIR) \
	-DUSE_STDPERIPH_DRIVER -DSTM32F10X_MD
CFLAGS := -mcpu=cortex-m3 -mthumb -std=c99 -Os -g3 \
	-ffunction-sections -fdata-sections -fno-common \
	-Wall -Wextra -Werror=implicit-function-declaration -Werror=return-type \
	$(EXTRA_CFLAGS)
ASFLAGS := -mcpu=cortex-m3 -mthumb -g3
LDFLAGS := -mcpu=cortex-m3 -mthumb -Tstm32f103cb.ld \
	-Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map \
	--specs=nano.specs --specs=nosys.specs -Wl,-u,_printf_float
LDLIBS := -Wl,--start-group -lc -lm -lnosys -Wl,--end-group

.PHONY: all clean
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).hex
	$(SIZE) $<

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: $(SPL_DRIVER_DIR)/src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/system_stm32f10x.o: $(CMSIS_DEVICE_DIR)/system_stm32f10x.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/startup_stm32f10x_md.o: $(CMSIS_DEVICE_DIR)/startup/TrueSTUDIO/startup_stm32f10x_md.s | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) stm32f103cb.ld
	$(CC) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJS:.o=.d)
