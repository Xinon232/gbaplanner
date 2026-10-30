#---------------------------------------------------------------------------------------------------------------------
# gbaplanner 10.0 (calendar and to-do list)
#
# Stack: Butano + devkitPro devkitARM, C++ (like gbawriter / gbavocab / gbareader).
#
# Build:         make LIBBUTANO=/path/to/butano/butano
# Memory check:  make memory-check
# Host tests:    make host-test
# Clean:         make clean
#
# Requirements:
#   - devkitPro installed at /opt/devkitpro; DEVKITPRO and DEVKITARM set
#   - LIBBUTANO set to the pinned Butano checkout (see README)
#   - the gbaSD submodule: git submodule update --init
#---------------------------------------------------------------------------------------------------------------------

TARGET      :=  gbaplanner
BUILD       :=  build
LIBBUTANO   ?=  /path/to/butano/butano
PYTHON      :=  python3
# App screens, the data core (src/core), the holiday table (src/holidays),
# gbaSD's drivers and its shared FatFs with exfat_repair (gbasd/fatfs; the
# app's settings: include/fatfs/ffconf.h).
SOURCES     :=  src/app src/platform src/fonts gbasd/fatfs gbasd/src src/core src/holidays
INCLUDES    :=  include include/fatfs gbasd/include gbasd/fatfs references/superfw/src references/superfw/src/fonts \
                include/core include/holidays
DATA        :=
GRAPHICS    :=
AUDIO       :=
AUDIOBACKEND :=  null
AUDIOTOOL   :=
DMGAUDIO    :=
DMGAUDIOBACKEND :=  null
ROMTITLE    :=  GBA PLANNER
ROMCODE     :=  APLN
# Optional SuperCard second-ROM-mirror transfers (as in the other apps).
SC_FAST_ROM_MIRROR ?= 0
USERFLAGS   :=  -DSC_FAST_ROM_MIRROR=$(SC_FAST_ROM_MIRROR) -fstack-usage
USERCXXFLAGS :=
USERASFLAGS :=
USERLDFLAGS :=
USERLIBDIRS :=
USERLIBS    :=
DEFAULTLIBS :=
STACKTRACE  :=
USERBUILD   :=
EXTTOOL     :=

#---------------------------------------------------------------------------------------------------------------------
# Export absolute butano path:
#---------------------------------------------------------------------------------------------------------------------
ifndef LIBBUTANOABS
	export LIBBUTANOABS	:=	$(realpath $(LIBBUTANO))
endif

#---------------------------------------------------------------------------------------------------------------------
# Include main makefile:
#---------------------------------------------------------------------------------------------------------------------
include $(LIBBUTANOABS)/butano.mak

ROM := $(TARGET).gba

.PHONY: memory-check host-test
memory-check:
	@$(PYTHON) tests/check_memory.py $(TARGET).elf --build $(BUILD) --core
host-test:
	@./tests/run_host_tests.sh
