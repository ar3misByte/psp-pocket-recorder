TARGET = recorder
OBJS = src/main.o src/player.o src/dec.o src/wav.o src/mp3info.o src/gfx.o src/ui.o

INCDIR = src
CFLAGS = -O2 -G0 -Wall -Wno-unused-function
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =
LDFLAGS =
LIBS = -lmpg123 -lpspaudio -lpsppower -lpsprtc -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Pocket Recorder
PSP_EBOOT_ICON = assets/ICON0.PNG
PSP_EBOOT_PIC1 = assets/PIC1.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
