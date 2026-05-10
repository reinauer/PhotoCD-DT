CC := m68k-amigaos-gcc
STRIP := m68k-amigaos-strip
HOST_CC ?= cc

include version.mk

ASSET_DIR := assets
ICONTOOL ?= icontool
OUTPUT_ROOT ?= $(BUILD_DIR)/amiga
PACKAGE_NAME := PhotoCD-DT-v$(PCD_VERSION)
PACKAGE_DIR := $(PACKAGE_NAME)
PACKAGE_INFO_SRC ?= $(ASSET_DIR)/PhotoCD-DT.info
PACKAGE_INFO_OUT := $(PACKAGE_NAME).info
PACKAGE_ARCHIVE := $(PACKAGE_NAME).lha
INSTALL_INFO_SRC := $(ASSET_DIR)/Install.info
README_INFO_SRC := $(ASSET_DIR)/README.md.info
PACKAGE_README_INFO_SRC := $(ASSET_DIR)/PhotoCD-DT.readme.info
PREFS_DRAWER_INFO_SRC := $(ASSET_DIR)/Prefs.info
PREFS_TOOL_INFO_SRC := $(ASSET_DIR)/Prefs/PhotoCD.info
DEFAULT_PREFS_SRC := $(ASSET_DIR)/Env/DataTypes/photocd.prefs

SRC_DIR := src
BUILD_DIR := build
DECODER_SRC_DIR := $(SRC_DIR)/decoder

INSTALL_OUT := $(OUTPUT_ROOT)/Install
README_OUT := $(OUTPUT_ROOT)/PhotoCD-DT.readme
DEFAULT_PREFS_OUT := $(OUTPUT_ROOT)/Env/DataTypes/photocd.prefs
DT_OUT := $(OUTPUT_ROOT)/Classes/DataTypes/photocd.datatype
PREFS_OUT := $(OUTPUT_ROOT)/Prefs/PhotoCD
DTPROBE_OUT := $(OUTPUT_ROOT)/C/dtprobe
DESC_OUT := $(OUTPUT_ROOT)/Devs/DataTypes/PhotoCD
HOST_DESCGEN_OUT := $(BUILD_DIR)/host/make_pcd_descriptor

DT_BUILD_DIR := $(BUILD_DIR)/datatype

WARN_CFLAGS := -Wall -Wextra -Wno-array-bounds -Wno-pointer-sign
OPT_CFLAGS := -Os
DECODER_CPPFLAGS := -I$(DECODER_SRC_DIR)
DEPFLAGS := -MMD -MP
VERSION_CPPFLAGS := \
	-DPCD_VERSION_MAJOR=$(PCD_VERSION_MAJOR) \
	-DPCD_VERSION_REVISION=$(PCD_VERSION_REVISION) \
	-DPCD_VERSION_DATE=\"$(PCD_VERSION_DATE)\"

AMIGA_COMMON_CFLAGS := -noixemul $(OPT_CFLAGS) $(WARN_CFLAGS)
AMIGA_TOOL_CFLAGS := $(OPT_CFLAGS) $(WARN_CFLAGS) -fomit-frame-pointer -mcrt=clib2
HOST_COMMON_CFLAGS := $(OPT_CFLAGS) $(WARN_CFLAGS)

DT_CFLAGS := $(AMIGA_COMMON_CFLAGS) -DmNoPThreads $(DECODER_CPPFLAGS) $(VERSION_CPPFLAGS)
DT_LDFLAGS := -noixemul -nostartfiles
PREFS_CFLAGS := $(AMIGA_TOOL_CFLAGS)
PREFS_LDFLAGS := -lgcc -lc -lamiga

DECODER_UNITS := \
	photocd \
	photocd_core \
	photocd_data \
	photocd_huffman \
	photocd_convert

DT_OBJS := \
	$(DT_BUILD_DIR)/datatype_init.o \
	$(DT_BUILD_DIR)/datatype_methods.o \
	$(addprefix $(DT_BUILD_DIR)/,$(addsuffix .o,$(DECODER_UNITS)))
DT_DEPS := $(DT_OBJS:.o=.d)

.PHONY: all datatype prefs dtprobe descriptor clean metadata runtime-assets lha lha-clean FORCE

all: metadata runtime-assets datatype prefs

metadata: $(INSTALL_OUT) $(README_OUT)

runtime-assets: $(DEFAULT_PREFS_OUT)

datatype: $(DT_OUT)

prefs: $(PREFS_OUT)

dtprobe: $(DTPROBE_OUT)

descriptor: $(DESC_OUT)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(INSTALL_OUT) $(README_OUT) $(DEFAULT_PREFS_OUT) $(DT_OUT) $(PREFS_OUT) $(DTPROBE_OUT) $(DESC_OUT)
	rm -rf PhotoCD-DT-v* PhotoCD-DT-v*.info PhotoCD-DT-v*.lha

lha:
	test -f $(PACKAGE_INFO_SRC) || { echo "missing $(PACKAGE_INFO_SRC)" >&2; exit 1; }
	command -v lha >/dev/null || { echo "lha not found" >&2; exit 1; }
	command -v $(ICONTOOL) >/dev/null || { echo "icontool not found" >&2; exit 1; }
	$(MAKE) lha-clean
	mkdir -p $(PACKAGE_DIR)
	$(MAKE) OUTPUT_ROOT=$(PACKAGE_DIR) metadata runtime-assets datatype prefs dtprobe descriptor
	cp -R src $(PACKAGE_DIR)
	mkdir -p $(PACKAGE_DIR)/tools
	cp tools/*.py tools/*.c $(PACKAGE_DIR)/tools/
	cp README.md Makefile version.mk Install.in PhotoCD-DT.readme.in $(PACKAGE_DIR)/
	cp $(INSTALL_INFO_SRC) $(PACKAGE_DIR)/Install.info
	$(ICONTOOL) --set "APPNAME=photocd.datatype $(PCD_VERSION)" $(PACKAGE_DIR)/Install.info
	cp $(PACKAGE_README_INFO_SRC) $(PACKAGE_DIR)/PhotoCD-DT.readme.info
	cp $(PREFS_DRAWER_INFO_SRC) $(PACKAGE_DIR)/Prefs.info
	cp $(README_INFO_SRC) $(PACKAGE_DIR)/README.md.info
	cp $(PREFS_TOOL_INFO_SRC) $(PACKAGE_DIR)/Prefs/PhotoCD.info
	cp $(PACKAGE_INFO_SRC) $(PACKAGE_INFO_OUT)
	rm -f $(PACKAGE_ARCHIVE)
	lha a $(PACKAGE_ARCHIVE) $(PACKAGE_INFO_OUT) $(PACKAGE_DIR)

lha-clean:
	rm -rf PhotoCD-DT-v* PhotoCD-DT-v*.info PhotoCD-DT-v*.lha

$(INSTALL_OUT): Install.in version.mk
	mkdir -p $(dir $@)
	sed -e 's/@PCD_VERSION@/$(PCD_VERSION)/g' $< > $@

$(README_OUT): PhotoCD-DT.readme.in version.mk
	mkdir -p $(dir $@)
	sed -e 's/@PCD_VERSION@/$(PCD_VERSION)/g' $< > $@

$(DEFAULT_PREFS_OUT): $(DEFAULT_PREFS_SRC)
	mkdir -p $(dir $@)
	cp $< $@

$(DT_BUILD_DIR):
	mkdir -p $@

$(DT_BUILD_DIR)/datatype_init.o: version.mk FORCE
$(DT_BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(DT_BUILD_DIR)
	$(CC) $(DT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(DT_BUILD_DIR)/photocd.o: $(DECODER_SRC_DIR)/photocd.c | $(DT_BUILD_DIR)
	$(CC) $(DT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(DT_BUILD_DIR)/photocd_%.o: $(DECODER_SRC_DIR)/photocd_%.c | $(DT_BUILD_DIR)
	$(CC) $(DT_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(DT_OUT): $(DT_OBJS)
	mkdir -p $(dir $@)
	$(CC) $(DT_LDFLAGS) -o $@ $(DT_OBJS)

$(PREFS_OUT): $(SRC_DIR)/photocd.c
	mkdir -p $(dir $@)
	$(CC) $(PREFS_CFLAGS) $(PREFS_LDFLAGS) -o $@ $<
	$(STRIP) -s $@

$(DTPROBE_OUT): $(SRC_DIR)/dtprobe.c
	mkdir -p $(dir $@)
	$(CC) $(AMIGA_COMMON_CFLAGS) -o $@ $<
	$(STRIP) -s $@

$(DESC_OUT): $(HOST_DESCGEN_OUT)
	mkdir -p $(dir $@)
	$(HOST_DESCGEN_OUT) -o $@

$(HOST_DESCGEN_OUT): tools/make_pcd_descriptor.c
	mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_COMMON_CFLAGS) -o $@ $<

-include $(DT_DEPS)
