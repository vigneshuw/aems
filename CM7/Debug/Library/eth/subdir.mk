################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Library/eth/eth.c 

OBJS += \
./Library/eth/eth.o 

C_DEPS += \
./Library/eth/eth.d 


# Each subdirectory must supply rules for building sources it contributes
Library/eth/%.o Library/eth/%.su Library/eth/%.cyclo: ../Library/eth/%.c Library/eth/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H745xx -DUSE_PWR_DIRECT_SMPS_SUPPLY -c -I../Core/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I"C:/Users/vigne/STM32CubeIDE/workspace_1.17.0/AEMSv02-Firmware/CM7/Library/led" -I"C:/Users/vigne/STM32CubeIDE/workspace_1.17.0/AEMSv02-Firmware/CM7/Library/eth" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Library-2f-eth

clean-Library-2f-eth:
	-$(RM) ./Library/eth/eth.cyclo ./Library/eth/eth.d ./Library/eth/eth.o ./Library/eth/eth.su

.PHONY: clean-Library-2f-eth

