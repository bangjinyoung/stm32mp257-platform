# Cortex-M33 non-secure FreeRTOS firmware for STM32MP257F-DK, loaded by Linux remoteproc.
# App sources live here; HAL, BSP, CMSIS and FreeRTOS come from STM32CubeMP2 (CUBE).
# Flags reproduce STM32CubeIDE configuration "CA35TDCID_m33_ns" of the
# FreeRTOS_ThreadCreation example (STM32CubeMP2 v1.3.2).
CUBE  ?= $(HOME)/st/stm32mp2/STM32CubeMP2-v1.3.2
APP   := $(CURDIR)
TC    ?= /opt/st/stm32cubeclt_1.22.0/GNU-tools-for-STM32/bin/arm-none-eabi-
CC    := $(TC)gcc
OBJCOPY := $(TC)objcopy
SIZE  := $(TC)size
OBJDUMP := $(TC)objdump
NAME  := m33_freertos
OUT   := out

HALS := $(CUBE)/Drivers/STM32MP2xx_HAL_Driver/Src
RTOS := $(CUBE)/Middlewares/Third_Party/FreeRTOS/Source

# .project linkedResources (order kept; duplicate icache entry collapsed)
C_SRCS := \
 $(APP)/Core/Src/syscalls.c \
 $(CUBE)/Drivers/CMSIS/Device/ST/STM32MP2xx/Source/Templates/system_stm32mp2xx_m33_ns.c \
 $(HALS)/stm32mp2xx_hal.c $(HALS)/stm32mp2xx_hal_cortex.c $(HALS)/stm32mp2xx_hal_dma.c \
 $(HALS)/stm32mp2xx_hal_dma_ex.c $(HALS)/stm32mp2xx_hal_exti.c $(HALS)/stm32mp2xx_hal_gpio.c \
 $(HALS)/stm32mp2xx_hal_icache.c $(HALS)/stm32mp2xx_hal_pwr.c $(HALS)/stm32mp2xx_hal_pwr_ex.c \
 $(HALS)/stm32mp2xx_hal_rcc.c $(HALS)/stm32mp2xx_hal_rcc_ex.c $(HALS)/stm32mp2xx_hal_tim.c \
 $(HALS)/stm32mp2xx_hal_tim_ex.c \
 $(HALS)/stm32mp2xx_hal_uart.c $(HALS)/stm32mp2xx_hal_uart_ex.c \
 $(HALS)/stm32mp2xx_ll_dma.c $(HALS)/stm32mp2xx_ll_exti.c $(HALS)/stm32mp2xx_ll_gpio.c \
 $(HALS)/stm32mp2xx_ll_rcc.c $(HALS)/stm32mp2xx_ll_usart.c \
 $(CUBE)/Utilities/ResourcesManager/res_mgr.c \
 $(APP)/Core/Src/main.c \
 $(APP)/Core/Src/stm32mp2xx_hal_msp.c \
 $(APP)/Core/Src/stm32mp2xx_hal_timebase_tim.c \
 $(APP)/Core/Src/stm32mp2xx_it.c \
 $(HALS)/stm32mp2xx_hal_dcache.c \
 $(APP)/Core/Src/thread_syscalls.c \
 $(CUBE)/Drivers/BSP/STM32MP257F-DK/stm32mp257f_disco.c \
 $(RTOS)/CMSIS_RTOS_V2/cmsis_os2.c $(RTOS)/croutine.c $(RTOS)/event_groups.c \
 $(RTOS)/portable/MemMang/heap_4.c $(RTOS)/list.c \
 $(RTOS)/portable/GCC/ARM_CM33_NTZ/non_secure/port.c \
 $(RTOS)/portable/GCC/ARM_CM33_NTZ/non_secure/portasm.c \
 $(RTOS)/queue.c $(RTOS)/stream_buffer.c $(RTOS)/tasks.c $(RTOS)/timers.c \
 $(APP)/FREERTOS/App/app_freertos.c \
 $(CUBE)/Utilities/CoproSync/copro_sync.c \
 $(HALS)/stm32mp2xx_hal_ipcc.c
S_SRCS := $(APP)/startup_stm32mp257cxx_m33.s

LDSCRIPT := $(CUBE)/Drivers/CMSIS/Device/ST/STM32MP2xx/Source/Templates/gcc/linker/stm32mp2xx_DDR_m33_ns.ld

# toolChain options: MCU STM32MP257FAIx, fpu fpv5-sp-d16, floatabi hard, thumb2, runtime nano_c
ARCH := -mcpu=cortex-m33 --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb

# C compiler definedsymbols (verbatim, incl. the stray space in the UART one)
C_DEFS := -DSTM32MP257Cxx -DICACHE_DCACHE_USE -DSTM32MP25xxxx \
 '-DUSE_HAL_UART_REGISTER_CALLBACKS =1' -DUSE_FULL_LL_DRIVER \
 -DTX_SINGLE_MODE_NON_SECURE=1 -DCORE_CM33 -DUSE_HAL_DRIVER -DTX_INCLUDE_USER_DEFINE_FILE
# Assembler definedsymbols
AS_DEFS := -DDEBUG -DTX_SINGLE_MODE_NON_SECURE=1

C_INCS := -I$(APP)/Core/Inc -I$(APP)/FREERTOS/App \
 -I$(RTOS)/include -I$(RTOS)/portable/GCC/ARM_CM33_NTZ/non_secure -I$(RTOS)/CMSIS_RTOS_V2 \
 -I$(CUBE)/Drivers/CMSIS/RTOS2/Include -I$(CUBE)/Utilities/ResourcesManager \
 -I$(CUBE)/Drivers/STM32MP2xx_HAL_Driver/Inc -I$(CUBE)/Drivers/CMSIS/Device/ST/STM32MP2xx/Include \
 -I$(CUBE)/Drivers/CMSIS/Core/Include -I$(CUBE)/Drivers/BSP/STM32MP257F-DK -I$(CUBE)/Utilities/CoproSync

# CubeIDE managed-build fixed flags (plugin defaults, not stored in XML) + XML: -g3, -O0
CFLAGS := -mcpu=cortex-m33 -std=gnu11 -g3 $(C_DEFS) $(C_INCS) -O0 -ffunction-sections -fdata-sections \
 -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb
ASFLAGS := -mcpu=cortex-m33 -g3 $(AS_DEFS) -x assembler-with-cpp -MMD -MP --specs=nano.specs \
 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb

LIBDIR := $(CUBE)/Middlewares/ST/STM32_USBPD_Library/Core/lib
LDFLAGS := -mcpu=cortex-m33 -T$(LDSCRIPT) --specs=nosys.specs -Wl,-Map=$(OUT)/$(NAME).map \
 -Wl,--gc-sections -static -z max-page-size=4096 -L$(LIBDIR) --specs=nano.specs \
 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -Wl,--start-group -lc -lm -Wl,--end-group
LIBS := -l:USBPDCORE_PD3_FULL_CM33_wc32.a

OBJS := $(addprefix $(OUT)/obj/,$(notdir $(C_SRCS:.c=.o))) $(addprefix $(OUT)/obj/,$(notdir $(S_SRCS:.s=.o)))
vpath %.c $(sort $(dir $(C_SRCS)))
vpath %.s $(sort $(dir $(S_SRCS)))

all: $(OUT)/$(NAME).elf $(OUT)/$(NAME)_stripped.elf $(OUT)/$(NAME).list

$(OUT)/obj/%.o: %.c | $(OUT)/obj
	$(CC) $< $(CFLAGS) -c -o $@
$(OUT)/obj/%.o: %.s | $(OUT)/obj
	$(CC) $(ASFLAGS) -c -o $@ $<
$(OUT)/obj:
	mkdir -p $@

$(OUT)/$(NAME).elf: $(OBJS) $(LDSCRIPT)
	$(CC) -o $@ $(OBJS) $(LIBS) $(LDFLAGS)
	$(SIZE) $@

# CubeIDE "postbuildStep" for CA35TDCID_m33_ns is empty; stripped copy added per factory naming
$(OUT)/$(NAME)_stripped.elf: $(OUT)/$(NAME).elf
	$(OBJCOPY) -S $< $@
$(OUT)/$(NAME).list: $(OUT)/$(NAME).elf
	$(OBJDUMP) -h -S $< > $@

clean:
	rm -rf $(OUT)
-include $(OBJS:.o=.d)
.PHONY: all clean
