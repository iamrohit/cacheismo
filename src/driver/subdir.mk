################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
./src/driver/commands.c \
./src/driver/driver.c \
./src/driver/parser.c 

OBJS += \
./src/driver/commands.o \
./src/driver/driver.o \
./src/driver/parser.o 

C_DEPS += \
./src/driver/commands.d \
./src/driver/driver.d \
./src/driver/parser.d 


# Each subdirectory must supply rules for building sources it contributes
src/driver/%.o: ./src/driver/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -std=gnu99 -I/usr/include/lua5.1 -O2 -g -Wall -c -fmessage-length=0 -v -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


