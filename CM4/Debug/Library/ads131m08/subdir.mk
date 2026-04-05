################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Library/ads131m08/ads131m08.c 

OBJS += \
./Library/ads131m08/ads131m08.o 

C_DEPS += \
./Library/ads131m08/ads131m08.d 


# Each subdirectory must supply rules for building sources it contributes
Library/ads131m08/%.o Library/ads131m08/%.su Library/ads131m08/%.cyclo: ../Library/ads131m08/%.c Library/ads131m08/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32H745xx -DUSE_PWR_DIRECT_SMPS_SUPPLY -c -I../Core/Inc -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/Common/Inc" -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/CM4/Library/ads131m08" -I../FATFS/Target -I../FATFS/App -I../../Middlewares/Third_Party/FatFs/src -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/CM4/Library/emmc_fs" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Library-2f-ads131m08

clean-Library-2f-ads131m08:
	-$(RM) ./Library/ads131m08/ads131m08.cyclo ./Library/ads131m08/ads131m08.d ./Library/ads131m08/ads131m08.o ./Library/ads131m08/ads131m08.su

.PHONY: clean-Library-2f-ads131m08

