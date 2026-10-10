TARGET = space_invaders
OBJS = main.o

BUILD_PRX = 1
PSP_EBOOT_TITLE = Space Invaders PSP

CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
