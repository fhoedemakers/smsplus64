all: smsPlus64.z64
	@echo "Builded $(if $(RELEASE),release,debug)"
.PHONY: all

BUILD_DIR = build

# include n64Release.mk when release parameter is set
ifeq ($(RELEASE),1)
include n64Release.mk
else
include n64.mk
endif
# add current folder and infones subfolder to include path
INCDIR = -I. -Ismsplus -Iassets
# add INCDIR to CFLAGS
CFLAGS += $(INCDIR) # -DLSB_FIRST=0
# add INCDIR to CXXFLAGS
CXXFLAGS += $(INCDIR) #-DLSB_FIRST=0

SUBDIRS = $(SOURCE_DIR) $(SOURCE_DIR)/smsplus $(SOURCE_DIR)/assets
#$(BUILD_DIR)/%.o: %.c 
# 	@mkdir -p $(dir $@)
# 	@echo "    [CC] $<"
# 	$(CC) -c $(CFLAGS) -o $@ $<

# $(BUILD_DIR)/%.o: %.cpp
# 	@mkdir -p $(dir $@)
# 	@echo "    [CXX] $<"
# 	$(CXX) -c $(CXXFLAGS) -o $@ $<vpath %.c $(SUBDIRS)
vpath %.cpp $(SUBDIRS)
vpath %.c $(SUBDIRS)
# 

OBJS = $(BUILD_DIR)/smsPlus64.o $(BUILD_DIR)/loadrom.o $(BUILD_DIR)/render.o $(BUILD_DIR)/sms.o  $(BUILD_DIR)/sn76496.o $(BUILD_DIR)/system.o $(BUILD_DIR)/vdp.o $(BUILD_DIR)/z80.o $(BUILD_DIR)/builtinrom.o $(BUILD_DIR)/menu.o $(BUILD_DIR)/RomLister.o $(BUILD_DIR)/FrensHelpers.o 

smsPlus64.z64: N64_ROM_TITLE = "SMSPlus emulator"
smsPlus64.z64: $(BUILD_DIR)/smsPlus64.dfs
$(BUILD_DIR)/smsPlus64.dfs: $(wildcard filesystem/*)
$(BUILD_DIR)/smsPlus64.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) *.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)
