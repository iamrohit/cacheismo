################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
./src/io/connection.c \
./src/io/datastream.c 

OBJS += \
./src/io/connection.o \
./src/io/datastream.o 

C_DEPS += \
./src/io/connection.d \
./src/io/datastream.d 


# Each subdirectory must supply rules for building sources it contributes
src/io/%.o: ./src/io/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -std=gnu99 -I/usr/include/lua5.1 -O2 -g -Wall -c -fmessage-length=0 -v -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


