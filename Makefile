################################################################################
# Automatically-generated file. Do not edit!
################################################################################


RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include src/lua/subdir.mk
-include src/io/subdir.mk
-include src/hashmap/subdir.mk
-include src/fallocator/subdir.mk
-include src/driver/subdir.mk
-include src/chunkpool/subdir.mk
-include src/cacheitem/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif


# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: cacheismo

# Tool invocations
cacheismo: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	gcc  -o"cacheismo" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS)$(C_DEPS)$(EXECUTABLES) cacheismo
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

