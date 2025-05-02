#PROGS = driver-gyro.c
PROGS = driver-accel.c
#PROGS = temp.c
# when you build yours swap this.
COMMON_SRC = mpu-6050.c
COMMON_SRC += mbox.c
COMMON_SRC += i2c.c
#STAFF_OBJS += staff-mpu-6050.o

# set to 0 if you don't want it to automatically run.
RUN=1

# define this if you need to give the device for your pi
TTYUSB = 
BOOTLOADER = my-install

# for division
LGCC ?= $(CS240LX_2025_PATH)/lib/libgcc.a

LIBS += $(LGCC)
LIB_POST := $(LGCC)
LIBS += $(LIBM)
CFLAGS += $(LIBM_INC)
include $(CS240LX_2025_PATH)/libpi/mk/Makefile.robust-v2
