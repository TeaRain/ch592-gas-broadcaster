PROJ := C:/project/Broadcaster-CH592
OTA  := $(PROJ)/OTA_Image

CC := riscv-none-embed-gcc
OBJCOPY := riscv-none-embed-objcopy
SIZE := riscv-none-embed-size

CFLAGS := -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore \
  -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections \
  -fno-common -g -DCLK_OSC32K=2 -I$(OTA)/IAP/include -I$(PROJ)/StdPeriphDriver/inc \
  -I$(PROJ)/RVMSIS -std=gnu99

IAP_SRC := $(OTA)/IAP/main.c \
  $(PROJ)/RVMSIS/core_riscv.c \
  $(PROJ)/StdPeriphDriver/CH59x_clk.c \
  $(PROJ)/StdPeriphDriver/CH59x_flash.c \
  $(PROJ)/StdPeriphDriver/CH59x_gpio.c \
  $(PROJ)/StdPeriphDriver/CH59x_pwr.c \
  $(PROJ)/StdPeriphDriver/CH59x_sys.c
IAP_ASM := $(OTA)/IAP/startup_CH592.S
IAP_OBJS := $(addprefix obj/iap/,$(notdir $(IAP_SRC:.c=.o))) $(addprefix obj/iap/,$(notdir $(IAP_ASM:.S=.o)))

all: jumpiap iap

jumpiap: JumpIAP/jumpiap.hex
iap: IAP/iap.hex

obj/jumpiap/startup_CH592.o: $(OTA)/JumpIAP/startup_CH592.S
	@mkdir -p obj/jumpiap
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

JumpIAP/jumpiap.elf: obj/jumpiap/startup_CH592.o
	$(CC) $(CFLAGS) -T $(OTA)/JumpIAP/Link.ld -nostartfiles -Xlinker --gc-sections \
	  -Xlinker --print-memory-usage -Wl,-Map=JumpIAP/jumpiap.map \
	  --specs=nano.specs --specs=nosys.specs -o $@ $^

JumpIAP/jumpiap.hex: JumpIAP/jumpiap.elf
	$(OBJCOPY) -O ihex $< $@
	$(SIZE) --format=berkeley $<

obj/iap/%.o: $(OTA)/IAP/%.c
	@mkdir -p obj/iap
	$(CC) $(CFLAGS) -c -o $@ $<

obj/iap/%.o: $(PROJ)/RVMSIS/%.c
	@mkdir -p obj/iap
	$(CC) $(CFLAGS) -c -o $@ $<

obj/iap/%.o: $(PROJ)/StdPeriphDriver/%.c
	@mkdir -p obj/iap
	$(CC) $(CFLAGS) -c -o $@ $<

obj/iap/%.o: $(OTA)/IAP/%.S
	@mkdir -p obj/iap
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

IAP/iap.elf: $(IAP_OBJS)
	$(CC) -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore \
	  -Os -T $(OTA)/IAP/Link.ld -nostartfiles -Xlinker --gc-sections \
	  -L$(PROJ)/StdPeriphDriver -Xlinker --print-memory-usage -Wl,-Map=IAP/iap.map \
	  --specs=nano.specs --specs=nosys.specs -o $@ $^ -lISP592 -lm

IAP/iap.hex: IAP/iap.elf
	$(OBJCOPY) -O ihex $< $@
	$(SIZE) --format=berkeley $<

clean:
	rm -rf obj/jumpiap obj/iap JumpIAP/jumpiap.elf JumpIAP/jumpiap.hex JumpIAP/jumpiap.map \
	  IAP/iap.elf IAP/iap.hex IAP/iap.map
