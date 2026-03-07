################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Library/Bluenrg/bluenrg2_intf.c 

OBJS += \
./Library/Bluenrg/bluenrg2_intf.o 

C_DEPS += \
./Library/Bluenrg/bluenrg2_intf.d 


# Each subdirectory must supply rules for building sources it contributes
Library/Bluenrg/%.o Library/Bluenrg/%.su Library/Bluenrg/%.cyclo: ../Library/Bluenrg/%.c Library/Bluenrg/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H745xx -DDATA_IN_D2_SRAM -DUSE_PWR_DIRECT_SMPS_SUPPLY -c -I../Core/Inc -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/Common/Inc" -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/CM7/Library/led" -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/CM7/Library/eth" -I../LWIP/App -I../LWIP/Target -I../../Middlewares/Third_Party/LwIP/src/include -I../../Middlewares/Third_Party/LwIP/system -I../../Drivers/BSP/Components/lan8742 -I../../Middlewares/Third_Party/LwIP/src/include/netif/ppp -I../../Middlewares/Third_Party/LwIP/src/include/lwip -I../../Middlewares/Third_Party/LwIP/src/include/lwip/apps -I../../Middlewares/Third_Party/LwIP/src/include/lwip/priv -I../../Middlewares/Third_Party/LwIP/src/include/lwip/prot -I../../Middlewares/Third_Party/LwIP/src/include/netif -I../../Middlewares/Third_Party/LwIP/src/include/compat/posix -I../../Middlewares/Third_Party/LwIP/src/include/compat/posix/arpa -I../../Middlewares/Third_Party/LwIP/src/include/compat/posix/net -I../../Middlewares/Third_Party/LwIP/src/include/compat/posix/sys -I../../Middlewares/Third_Party/LwIP/src/include/compat/stdc -I../../Middlewares/Third_Party/LwIP/system/arch -I../FATFS/Target -I../FATFS/App -I../../Middlewares/Third_Party/FatFs/src -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2" -I../../Middlewares/ST/BlueNRG-2/hci/hci_tl_patterns/Basic -I../../Middlewares/ST/BlueNRG-2/utils -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/Middlewares/ST/BlueNRG-2/includes" -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/Middlewares/ST/BlueNRG-2/hci/hci_tl_patterns/Basic" -I"C:/Users/vigne/STM32CubeIDE/workspace/AEMSv02-Firmware/Middlewares/ST/BlueNRG-2/utils" -I../BlueNRG-2/Target -I../../Middlewares/ST/BlueNRG-2/includes -I../../Middlewares/Third_Party/FreeRTOS/Source/include -I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Library-2f-Bluenrg

clean-Library-2f-Bluenrg:
	-$(RM) ./Library/Bluenrg/bluenrg2_intf.cyclo ./Library/Bluenrg/bluenrg2_intf.d ./Library/Bluenrg/bluenrg2_intf.o ./Library/Bluenrg/bluenrg2_intf.su

.PHONY: clean-Library-2f-Bluenrg

