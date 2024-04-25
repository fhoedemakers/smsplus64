all: infones64.z64
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
INCDIR = -I. -Iinfones -Iassets
# add INCDIR to CFLAGS
CFLAGS += $(INCDIR)
# add INCDIR to CXXFLAGS
CXXFLAGS += $(INCDIR)

SUBDIRS = $(SOURCE_DIR) $(SOURCE_DIR)/infones $(SOURCE_DIR)/assets
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

OBJS = $(BUILD_DIR)/infones64.o $(BUILD_DIR)/InfoNES.o $(BUILD_DIR)/tar.o $(BUILD_DIR)/InfoNES_Mapper.o  $(BUILD_DIR)/InfoNES_pAPU.o $(BUILD_DIR)/K6502.o $(BUILD_DIR)/builtinrom.o 

infones64.z64: N64_ROM_TITLE = "InfoNES NES emulator"

$(BUILD_DIR)/infones64.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) *.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)
