################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
./src/lua/binding.c \
./src/lua/marshal.c 

OBJS += \
./src/lua/binding.o \
./src/lua/marshal.o 

C_DEPS += \
./src/lua/binding.d \
./src/lua/marshal.d 


# Each subdirectory must supply rules for building sources it contributes
src/lua/%.o: ./src/lua/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -std=gnu99 -I/usr/include/lua5.1 -O2 -g -Wall -c -fmessage-length=0 -v -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


