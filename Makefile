# ============================================================================
# Sirpair OS Makefile
# OS for ThinkPad X220 (Intel i5-2520M Sandy Bridge)
# Disk I/O via USB driver stack (PCI -> EHCI -> USB -> Mass Storage)
#
# Directory layout:
#   boot/      - Boot loader (bootasm.S, bootmain.c)
#   kernel/    - Kernel source (.c, .S)
#   include/   - Header files (.h)
#   user/      - User programs and library
#   tools/     - Build tools (mkfs, sign.pl, vectors.pl)
#   docs/      - Documentation (README, etc.)
#   build/     - Build output (all intermediate files)
# ============================================================================

# Source directories
BOOT_DIR    = boot
KERNEL_DIR  = kernel
USER_DIR    = user
INCLUDE_DIR = include
TOOLS_DIR   = tools
DOCS_DIR    = docs

# TinyCC 系统化回归：短小 C 源打进 /home/t*.c（与 tools/tcc-sys-regress.py 中路径一致）
TCC_SYS_CASES = \
	$(TOOLS_DIR)/tcc_sys_cases/t01.c \
	$(TOOLS_DIR)/tcc_sys_cases/t02.c \
	$(TOOLS_DIR)/tcc_sys_cases/t03.c \
	$(TOOLS_DIR)/tcc_sys_cases/t04.c \
	$(TOOLS_DIR)/tcc_sys_cases/t05.c \
	$(TOOLS_DIR)/tcc_sys_cases/t06.c \
	$(TOOLS_DIR)/tcc_sys_cases/t07.c \
	$(TOOLS_DIR)/tcc_sys_cases/t08.c

# Build output directory
BUILD_DIR   = build

# ============================================================================
# Kernel object files (compiled from kernel/*.c and kernel/*.S)
# ============================================================================
KERNEL_OBJ_NAMES = \
	bio.o\
	console.o\
	fb_console.o\
	font8x16.o\
	null.o\
	exec.o\
	file.o\
	fs.o\
	disk.o\
	ioapic.o\
	lapic.o\
	log.o\
	kbd.o\
	kalloc.o\
	kmalloc.o\
	errno.o\
	main.o\
	net.o\
	mp.o\
	mouse.o\
	pci.o\
	picirq.o\
	pipe.o\
	proc.o\
	rtc.o\
	rtcio.o\
	spinlock.o\
	string.o\
	swtch.o\
	syscall.o\
	sysfile.o\
	sysproc.o\
	timer.o\
	trapasm.o\
	trap.o\
	uart.o\
	unixsock.o\
	usb.o\
	vectors.o\
	vm.o\
	microps/util.o\
	microps/net.o\
	microps/arp.o\
	microps/ether.o\
	microps/icmp.o\
	microps/ip.o\
	microps/tcp.o\
	microps/udp.o\
	microps/sock.o\
	microps/driver/null.o\
	microps/driver/loopback.o\
	microps/driver/sirpair_e1000.o\
	microps/microps_mem.o\
	microps/microps_mutex.o\
	microps/microps_fmt.o\
	microps/microps_time.o\
	microps/intr_sirpair.o\
	microps/sched_sirpair.o\
	microps/microps_glue.o\
	microps/sirpair_sock_user.o

OBJS = $(addprefix $(BUILD_DIR)/, $(KERNEL_OBJ_NAMES))

# microps 头文件位于 kernel/microps/（platform.h、microps_net.h 等）
MICROPS_KERNEL_CFLAGS = -I$(KERNEL_DIR)/microps -include $(KERNEL_DIR)/microps/microps_shim.h

$(BUILD_DIR)/microps/%.o: $(KERNEL_DIR)/microps/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MICROPS_KERNEL_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/microps/driver/%.o: $(KERNEL_DIR)/microps/driver/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MICROPS_KERNEL_CFLAGS) -c -o $@ $<

# ============================================================================
# Cross-compiler setup
# ============================================================================
# Cross-compiling (e.g., on Mac OS X)
#TOOLPREFIX = i386-jos-elf-

# Using native tools (e.g., on X86 Linux)
#TOOLPREFIX =

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if i386-jos-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
	then echo 'i386-jos-elf-'; \
	elif i686-linux-gnu-objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
	then echo 'i686-linux-gnu-'; \
	elif objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
	then echo ''; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find an i386-*-elf version of GCC/binutils." 1>&2; \
	echo "*** Is the directory with i386-jos-elf-gcc in your PATH?" 1>&2; \
	echo "*** If your i386-*-elf toolchain is installed with a command" 1>&2; \
	echo "*** prefix other than 'i386-jos-elf-', set your TOOLPREFIX" 1>&2; \
	echo "*** environment variable to that prefix and run 'make' again." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# If the makefile can't find QEMU, specify its path here
#QEMU =

# Try to infer the correct QEMU
ifndef QEMU
QEMU = $(shell if which qemu-system-i386 > /dev/null 2>&1; \
	then echo qemu-system-i386; exit; \
	elif which qemu > /dev/null 2>&1; \
	then echo qemu; exit; \
	else \
	qemu=/Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu; \
	if test -x $$qemu; then echo $$qemu; exit; fi; fi; \
	echo "***" 1>&2; \
	echo "*** Error: Couldn't find a working QEMU executable." 1>&2; \
	echo "*** Is the directory containing the qemu binary in your PATH" 1>&2; \
	echo "*** or have you tried setting the QEMU variable in Makefile?" 1>&2; \
	echo "***" 1>&2; exit 1)
endif

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -fno-pic -static -fno-builtin -fno-strict-aliasing -Wall -MMD -MP -ggdb -m32 -Werror -fno-omit-frame-pointer
CFLAGS += -I$(INCLUDE_DIR)
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

ASFLAGS = -m32 -gdwarf-2 -Wa,-divide -I$(INCLUDE_DIR)

# FreeBSD ld wants ``elf_i386_fbsd''
LDFLAGS += -m $(shell $(LD) -V | grep elf_i386 2>/dev/null)

# ============================================================================
# Build directory creation (order-only prerequisite)
# ============================================================================
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ============================================================================
# Pattern rules for compilation (source files -> build/)
# ============================================================================

# Kernel C source -> build/*.o
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Kernel assembly source -> build/*.o
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# User C source -> build/*.o
$(BUILD_DIR)/%.o: $(USER_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# User assembly source -> build/*.o
$(BUILD_DIR)/%.o: $(USER_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Boot C source -> build/*.o
$(BUILD_DIR)/%.o: $(BOOT_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Boot assembly source -> build/*.o
$(BUILD_DIR)/%.o: $(BOOT_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# ============================================================================
# Combined image layout constants (must match param.h definitions)
# ============================================================================
FS_SECTOR_OFFSET = 10000
FS_SIZE = 65536

# ============================================================================
# sirpair-kernel.img: combined image (bootblock + kernel + filesystem)
# Layout:
#   Sector 0:               Boot sector (MBR, 512 bytes)
#   Sector 1 ~ 9999:        Kernel ELF + padding
#   Sector 10000 ~ 75535:   Filesystem (Sirpair 格式, 32MiB)
# Write to USB drive and boot on ThinkPad X220
# All disk I/O via USB driver stack, no IDE
# ============================================================================
sirpair-kernel.img: $(BUILD_DIR)/bootblock $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/fs.img
	@echo "=== Creating combined image sirpair-kernel.img ==="
	@echo "  Boot sector:  sector 0"
	@echo "  Kernel:       sector 1+"
	@echo "  Filesystem:   sector $(FS_SECTOR_OFFSET) (offset $$(( $(FS_SECTOR_OFFSET) * 512 )) bytes)"
	dd if=/dev/zero of=sirpair-kernel.img count=$$(( $(FS_SECTOR_OFFSET) + $(FS_SIZE) ))
	dd if=$(BUILD_DIR)/bootblock of=sirpair-kernel.img conv=notrunc
	dd if=$(BUILD_DIR)/kernel.elf of=sirpair-kernel.img seek=1 conv=notrunc
	dd if=$(BUILD_DIR)/fs.img of=sirpair-kernel.img seek=$(FS_SECTOR_OFFSET) conv=notrunc
	@echo "=== sirpair-kernel.img created ($$(( ($(FS_SECTOR_OFFSET) + $(FS_SIZE)) * 512 )) bytes) ==="

sirpairmemfs.img: $(BUILD_DIR)/bootblock $(BUILD_DIR)/kernelmemfs
	dd if=/dev/zero of=sirpairmemfs.img count=10000
	dd if=$(BUILD_DIR)/bootblock of=sirpairmemfs.img conv=notrunc
	dd if=$(BUILD_DIR)/kernelmemfs of=sirpairmemfs.img seek=1 conv=notrunc

# ============================================================================
# Boot block (from boot/)
# ============================================================================
$(BUILD_DIR)/bootblock: $(BOOT_DIR)/bootasm.S $(BOOT_DIR)/bootmain.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fno-pic -Os -nostdinc -I$(INCLUDE_DIR) -c -o $(BUILD_DIR)/bootmain.o $(BOOT_DIR)/bootmain.c
	$(CC) $(CFLAGS) -fno-pic -nostdinc -I$(INCLUDE_DIR) -c -o $(BUILD_DIR)/bootasm.o $(BOOT_DIR)/bootasm.S
	$(LD) $(LDFLAGS) -N -e start -Ttext 0x7C00 -o $(BUILD_DIR)/bootblock.o $(BUILD_DIR)/bootasm.o $(BUILD_DIR)/bootmain.o
	$(OBJDUMP) -S $(BUILD_DIR)/bootblock.o > $(BUILD_DIR)/bootblock.asm
	$(OBJCOPY) -S -O binary -j .text $(BUILD_DIR)/bootblock.o $(BUILD_DIR)/bootblock
	$(TOOLS_DIR)/sign.pl $(BUILD_DIR)/bootblock

# ============================================================================
# Kernel entry points (from kernel/)
# ============================================================================
$(BUILD_DIR)/entryother: $(KERNEL_DIR)/entryother.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fno-pic -nostdinc -I$(INCLUDE_DIR) -c -o $(BUILD_DIR)/entryother.o $(KERNEL_DIR)/entryother.S
	$(LD) $(LDFLAGS) -N -e start -Ttext 0x7000 -o $(BUILD_DIR)/bootblockother.o $(BUILD_DIR)/entryother.o
	$(OBJCOPY) -S -O binary -j .text $(BUILD_DIR)/bootblockother.o $(BUILD_DIR)/entryother
	$(OBJDUMP) -S $(BUILD_DIR)/bootblockother.o > $(BUILD_DIR)/entryother.asm

$(BUILD_DIR)/rebootreal: $(KERNEL_DIR)/rebootreal.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fno-pic -nostdinc -I$(INCLUDE_DIR) -c -o $(BUILD_DIR)/rebootreal.o $(KERNEL_DIR)/rebootreal.S
	$(LD) $(LDFLAGS) -N -e start -Ttext 0x6000 -o $(BUILD_DIR)/rebootreal.out $(BUILD_DIR)/rebootreal.o
	$(OBJCOPY) -S -O binary -j .text $(BUILD_DIR)/rebootreal.out $(BUILD_DIR)/rebootreal
	$(OBJDUMP) -S $(BUILD_DIR)/rebootreal.out > $(BUILD_DIR)/rebootreal.asm

$(BUILD_DIR)/vesareal: $(KERNEL_DIR)/vesareal.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fno-pic -nostdinc -I$(INCLUDE_DIR) -c -o $(BUILD_DIR)/vesareal.o $(KERNEL_DIR)/vesareal.S
	$(LD) $(LDFLAGS) -N -e start -Ttext 0x5000 -o $(BUILD_DIR)/vesareal.out $(BUILD_DIR)/vesareal.o
	$(OBJCOPY) -S -O binary -j .text $(BUILD_DIR)/vesareal.out $(BUILD_DIR)/vesareal
	$(OBJDUMP) -S $(BUILD_DIR)/vesareal.out > $(BUILD_DIR)/vesareal.asm

$(BUILD_DIR)/initcode: $(KERNEL_DIR)/initcode.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -nostdinc -I$(INCLUDE_DIR) -c -o $(BUILD_DIR)/initcode.o $(KERNEL_DIR)/initcode.S
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $(BUILD_DIR)/initcode.out $(BUILD_DIR)/initcode.o
	$(OBJCOPY) -S -O binary $(BUILD_DIR)/initcode.out $(BUILD_DIR)/initcode
	$(OBJDUMP) -S $(BUILD_DIR)/initcode.o > $(BUILD_DIR)/initcode.asm

# ============================================================================
# Kernel link
# NOTE: We cd into BUILD_DIR before linking so that -b binary files
# (initcode, entryother) generate correct symbol names like
# _binary_initcode_start instead of _binary_build_initcode_start
# ============================================================================
$(BUILD_DIR)/kernel.elf: $(OBJS) $(BUILD_DIR)/entry.o $(BUILD_DIR)/entryother $(BUILD_DIR)/rebootreal $(BUILD_DIR)/vesareal $(BUILD_DIR)/initcode kernel.ld | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(LD) $(LDFLAGS) -T ../kernel.ld -o kernel.elf entry.o $(KERNEL_OBJ_NAMES) -b binary initcode entryother rebootreal vesareal
	$(OBJDUMP) -S $(BUILD_DIR)/kernel.elf > $(BUILD_DIR)/kernel.asm
	$(OBJDUMP) -t $(BUILD_DIR)/kernel.elf | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD_DIR)/kernel.sym

# kernelmemfs is a copy of kernel that maintains the
# disk image in memory instead of writing to a disk.
MEMFS_OBJ_NAMES = $(filter-out disk.o,$(KERNEL_OBJ_NAMES)) memide.o
MEMFSOBJS = $(addprefix $(BUILD_DIR)/, $(MEMFS_OBJ_NAMES))
$(BUILD_DIR)/kernelmemfs: $(MEMFSOBJS) $(BUILD_DIR)/entry.o $(BUILD_DIR)/entryother $(BUILD_DIR)/rebootreal $(BUILD_DIR)/vesareal $(BUILD_DIR)/initcode $(BUILD_DIR)/fs.img | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(LD) $(LDFLAGS) -Ttext 0x100000 -e main -o kernelmemfs entry.o $(MEMFS_OBJ_NAMES) -b binary initcode entryother rebootreal vesareal fs.img
	$(OBJDUMP) -S $(BUILD_DIR)/kernelmemfs > $(BUILD_DIR)/kernelmemfs.asm
	$(OBJDUMP) -t $(BUILD_DIR)/kernelmemfs | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD_DIR)/kernelmemfs.sym

# ============================================================================
# Generated source files
# ============================================================================
$(BUILD_DIR)/vectors.S: $(TOOLS_DIR)/vectors.pl | $(BUILD_DIR)
	perl $(TOOLS_DIR)/vectors.pl > $(BUILD_DIR)/vectors.S

# Explicit rule for vectors.o (generated source, not in kernel/ directory)
$(BUILD_DIR)/vectors.o: $(BUILD_DIR)/vectors.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c -o $@ $<

tags: $(OBJS) $(BUILD_DIR)/_init
	etags $(KERNEL_DIR)/*.S $(KERNEL_DIR)/*.c $(BOOT_DIR)/*.S $(BOOT_DIR)/*.c \
	      $(USER_DIR)/*.c $(INCLUDE_DIR)/*.h

# ============================================================================
# User programs (from user/)
# ============================================================================
ULIB = $(BUILD_DIR)/exit.o $(BUILD_DIR)/ulib.o $(BUILD_DIR)/usys.o $(BUILD_DIR)/printf.o $(BUILD_DIR)/umalloc.o

# ============================================================================
# Lua 5.5 解释器（thirdparty/lua-5.5.0 + user/lua 桩代码）
# ============================================================================
LUA_DIR = $(BUILD_DIR)/luaobj
# Lua 依赖 <math.h> 中 GCC 内置数学函数；全局 CFLAGS 的 -fno-builtin 会迫使生成对 libm 的未解析引用。
# Sirpair 的 printf(int,...) 与 ISO printf 不同，需关闭 ISO printf 系列内置以免与 user.h 冲突。
LUA_CFLAGS = $(filter-out -fno-builtin,$(CFLAGS)) -I$(USER_DIR)/lua/include -I$(USER_DIR)/lua -Ithirdparty/lua-5.5.0 \
	-include $(USER_DIR)/lua/sirpair_lua_prefix.h -std=c99 -DNDEBUG -DSIRPAIR_LUA \
	-fno-builtin-printf -fno-builtin-fprintf -fno-builtin-sprintf -fno-builtin-vprintf -fno-builtin-vfprintf -fno-builtin-vsprintf -fno-builtin-vsnprintf \
	-Wno-nonnull-compare

LIBGCC_A = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

LUA_TP_NAMES = lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes \
	lparser lstate lstring ltable ltm lundump lvm lzio lauxlib \
	lbaselib ldblib liolib lmathlib loslib ltablib lstrlib lutf8lib loadlib lcorolib linit lua

LUA_STUB_NAMES = lua_putchar printf_embed lua_stdio lua_time lua_signal lua_locale lua_errno lua_libc lua_string_compat

LUA_TP_OBJS = $(addprefix $(LUA_DIR)/, $(addsuffix .o, $(LUA_TP_NAMES)))
LUA_STUB_OBJS = $(addprefix $(LUA_DIR)/, $(addsuffix .o, $(LUA_STUB_NAMES)))
LUA_ALL_OBJS = $(LUA_TP_OBJS) $(LUA_STUB_OBJS) $(LUA_DIR)/lua_setjmp.o

# 勿使用「无前置条件的 $(LUA_DIR) 目录目标 + order-only | $(LUA_DIR)」：该目录规则在 GNU make
# 下会被视为永远过期，每次构建都会先跑 mkdir，易触发不必要的全量重编 luaobj/*.o。
# 与 build/*.o 一致：目录在每条编译命令里 mkdir，依赖关系由 -MMD 生成的 .d 承担（见下方 -include）。
$(LUA_DIR)/%.o: thirdparty/lua-5.5.0/%.c
	@mkdir -p $(LUA_DIR)
	$(CC) $(LUA_CFLAGS) -c -o $@ $<

$(LUA_DIR)/%.o: $(USER_DIR)/lua/%.c
	@mkdir -p $(LUA_DIR)
	$(CC) $(LUA_CFLAGS) -c -o $@ $<

$(LUA_DIR)/lua_setjmp.o: $(USER_DIR)/lua/lua_setjmp.S
	@mkdir -p $(LUA_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/_lua: $(LUA_ALL_OBJS) $(ULIB) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^ $(LIBGCC_A)
	$(OBJDUMP) -S $@ > $(BUILD_DIR)/lua.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD_DIR)/lua.sym

# ============================================================================
# TinyCC 0.9.25（thirdparty/tcc-0.9.25）：在系统内运行的 i386 编译器前端
# ============================================================================
TCC_SRCDIR = thirdparty/tcc-0.9.25
TCC_CFLAGS = $(filter-out -Werror -fno-builtin,$(CFLAGS)) -std=c99 -O2 \
	-DSIRPAIR_TCC -DTCC_TARGET_I386 -DCONFIG_TCC_STATIC \
	-I$(TCC_SRCDIR) -Iuser/tcc -Iuser/tcc/include -Iuser/lua/include -I$(INCLUDE_DIR) \
	-fno-builtin-printf -fno-builtin-fprintf -fno-builtin-sprintf -fno-builtin-vfprintf \
	-Wno-pointer-sign -Wno-sign-compare -Wno-unused-parameter -Wno-unused-variable \
	-Wno-unused-but-set-variable -Wno-maybe-uninitialized -Wno-parentheses -Wno-type-limits \
	-Wno-misleading-indentation -Wno-array-bounds

TCC_LUA_OBJS = $(LUA_DIR)/lua_stdio.o $(LUA_DIR)/printf_embed.o $(LUA_DIR)/lua_time.o \
	$(LUA_DIR)/lua_libc.o $(LUA_DIR)/lua_string_compat.o \
	$(LUA_DIR)/lua_setjmp.o $(LUA_DIR)/lua_putchar.o $(LUA_DIR)/lua_errno.o

# 单文件 tcc.c → tcc_sirpair.o：全量重编时仅一条 gcc（与多文件 Lua 对比属正常）；增量依赖由 build/tcc_sirpair.d 承担。
$(BUILD_DIR)/tcc_sirpair.o: $(TCC_SRCDIR)/tcc.c | $(BUILD_DIR)
	$(CC) $(TCC_CFLAGS) -c -o $@ $(TCC_SRCDIR)/tcc.c

$(BUILD_DIR)/sirpair_tcc_misc.o: user/tcc/sirpair_tcc_misc.c user/tcc/sirpair_host_includes.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ user/tcc/sirpair_tcc_misc.c

# _tcc 使用 ISO printf（供 libtcc TCCSYM 取址），不能与 user/printf.c 的 printf(fd,...) 同时链接
$(BUILD_DIR)/printf_tcc_iso.o: user/tcc/printf_iso.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DSIRPAIR_TCC -Iuser/lua/include -c -o $@ user/tcc/printf_iso.c

$(BUILD_DIR)/tcc_ldexp_stub.o: user/tcc/tcc_ldexp_stub.c | $(BUILD_DIR)
	$(CC) $(filter-out -Werror -fno-builtin,$(CFLAGS)) -c -o $@ user/tcc/tcc_ldexp_stub.c

TCC_ULIB = $(BUILD_DIR)/exit.o $(BUILD_DIR)/ulib.o $(BUILD_DIR)/usys.o $(BUILD_DIR)/printf_tcc_iso.o $(BUILD_DIR)/umalloc.o

$(BUILD_DIR)/_tcc: $(BUILD_DIR)/tcc_sirpair.o $(BUILD_DIR)/sirpair_tcc_misc.o $(BUILD_DIR)/tcc_ldexp_stub.o $(TCC_LUA_OBJS) $(TCC_ULIB) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $(BUILD_DIR)/tcc_sirpair.o $(BUILD_DIR)/sirpair_tcc_misc.o $(BUILD_DIR)/tcc_ldexp_stub.o $(TCC_LUA_OBJS) $(TCC_ULIB) $(LIBGCC_A)
	$(OBJDUMP) -S $@ > $(BUILD_DIR)/tcc.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD_DIR)/tcc.sym

# 供映像内 /bin/tcc 链接可执行文件：与 _tcc 相同的用户态与 Lua 桩（ISO printf 等）
$(BUILD_DIR)/libtcc1_rt.o: $(TCC_SRCDIR)/lib/libtcc1.c | $(BUILD_DIR)
	$(CC) $(filter-out -Werror,$(CFLAGS)) -O2 -c -o $@ $<

$(BUILD_DIR)/libtcc1.a: $(BUILD_DIR)/libtcc1_rt.o | $(BUILD_DIR)
	$(TOOLPREFIX)ar rcs $@ $<

$(BUILD_DIR)/libsirpairrt.a: $(TCC_LUA_OBJS) $(TCC_ULIB) $(BUILD_DIR)/tcc_ldexp_stub.o | $(BUILD_DIR)
	$(TOOLPREFIX)ar rcs $@ $(TCC_LUA_OBJS) $(TCC_ULIB) $(BUILD_DIR)/tcc_ldexp_stub.o

# objdump 链接 disasm_i386.o（显式规则优先于下面的 _% 模式规则）
$(BUILD_DIR)/_objdump: $(BUILD_DIR)/objdump.o $(BUILD_DIR)/disasm_i386.o $(ULIB) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $(BUILD_DIR)/objdump.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD_DIR)/objdump.sym

# ============================================================================
# beanstalkd（thirdparty/beanstalkd-master + user/beanstalkd 移植）
# ============================================================================
BEAN_TP_DIR = thirdparty/beanstalkd-master
BEAN_USER = user/beanstalkd
BEAN_CPPFLAGS = -DSIRPAIR -I$(BEAN_USER) -I$(INCLUDE_DIR) -I$(BEAN_TP_DIR)
BEAN_XINCLUDE = -include $(BEAN_USER)/sirpair_pre.h -include $(BEAN_USER)/beanstalkd_sirpair_shim.h

BEAN_OBJS = \
	$(BUILD_DIR)/bs_conn.o \
	$(BUILD_DIR)/bs_file.o \
	$(BUILD_DIR)/bs_heap.o \
	$(BUILD_DIR)/bs_job.o \
	$(BUILD_DIR)/bs_ms.o \
	$(BUILD_DIR)/bs_primes.o \
	$(BUILD_DIR)/bs_prot.o \
	$(BUILD_DIR)/bs_serv.o \
	$(BUILD_DIR)/bs_tube.o \
	$(BUILD_DIR)/bs_walg.o \
	$(BUILD_DIR)/bs_sirpair.o \
	$(BUILD_DIR)/bs_net.o \
	$(BUILD_DIR)/bs_time.o \
	$(BUILD_DIR)/bs_main.o \
	$(BUILD_DIR)/bs_util.o \
	$(BUILD_DIR)/bs_mp_printf.o \
	$(BUILD_DIR)/bs_fmt.o \
	$(BUILD_DIR)/bs_errno.o \
	$(BUILD_DIR)/bs_strtoul.o \
	$(BUILD_DIR)/bs_libc_compat.o \
	$(BUILD_DIR)/bs_vers.o

$(BUILD_DIR)/bs_%.o: $(BEAN_TP_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BEAN_CPPFLAGS) $(BEAN_XINCLUDE) -c -o $@ $<

$(BUILD_DIR)/bs_sirpair.o: $(BEAN_USER)/sirpair.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BEAN_CPPFLAGS) $(BEAN_XINCLUDE) -c -o $@ $<

$(BUILD_DIR)/bs_net.o: $(BEAN_USER)/net_sirpair.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BEAN_CPPFLAGS) $(BEAN_XINCLUDE) -c -o $@ $<

$(BUILD_DIR)/bs_time.o: $(BEAN_USER)/time_sirpair.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BEAN_CPPFLAGS) $(BEAN_XINCLUDE) -c -o $@ $<

$(BUILD_DIR)/bs_main.o: $(BEAN_USER)/main_sirpair.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BEAN_CPPFLAGS) $(BEAN_XINCLUDE) -c -o $@ $<

$(BUILD_DIR)/bs_util.o: $(BEAN_USER)/util_sirpair.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BEAN_CPPFLAGS) $(BEAN_XINCLUDE) -c -o $@ $<

$(BUILD_DIR)/bs_mp_printf.o: $(BEAN_USER)/mp_printf.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BEAN_USER) -I$(INCLUDE_DIR) -c -o $@ $<

$(BUILD_DIR)/bs_fmt.o: $(BEAN_USER)/beanstalkd_fmt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BEAN_USER) -I$(INCLUDE_DIR) -c -o $@ $<

$(BUILD_DIR)/bs_errno.o: $(BEAN_USER)/errno_sirpair.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c -o $@ $<

$(BUILD_DIR)/bs_strtoul.o: $(BEAN_USER)/strtoul_sirpair.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c -o $@ $<

$(BUILD_DIR)/bs_libc_compat.o: $(BEAN_USER)/libc_compat.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c -o $@ $<

$(BUILD_DIR)/bs_vers.o: $(BEAN_USER)/vers.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(BEAN_TP_DIR) -c -o $@ $<

$(BUILD_DIR)/_beanstalkd: $(BEAN_OBJS) $(ULIB) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $(BEAN_OBJS) $(ULIB) $(LIBGCC_A)
	$(OBJDUMP) -S $@ > $(BUILD_DIR)/beanstalkd.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD_DIR)/beanstalkd.sym

$(BUILD_DIR)/_%: $(BUILD_DIR)/%.o $(ULIB) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $(BUILD_DIR)/$*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD_DIR)/$*.sym

$(BUILD_DIR)/gui_demo.raw: misc/dispaly-demo.bmp tools/bmp_to_rgb332.py | $(BUILD_DIR)
	python3 tools/bmp_to_rgb332.py misc/dispaly-demo.bmp $(BUILD_DIR)/gui_demo.raw 1024 768

$(BUILD_DIR)/gui_demo.o: $(BUILD_DIR)/gui_demo.raw | $(BUILD_DIR)
	cd $(BUILD_DIR) && $(LD) -r -b binary -o gui_demo.o gui_demo.raw

$(BUILD_DIR)/_gui: $(BUILD_DIR)/gui.o $(BUILD_DIR)/gui_demo.o $(ULIB) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $(BUILD_DIR)/gui.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD_DIR)/gui.sym

$(BUILD_DIR)/_forktest: $(BUILD_DIR)/forktest.o $(ULIB) | $(BUILD_DIR)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $(BUILD_DIR)/_forktest $(BUILD_DIR)/forktest.o $(BUILD_DIR)/exit.o $(BUILD_DIR)/ulib.o $(BUILD_DIR)/usys.o
	$(OBJDUMP) -S $(BUILD_DIR)/_forktest > $(BUILD_DIR)/forktest.asm

# ============================================================================
# Host tools (compiled with native gcc)
# ============================================================================
$(BUILD_DIR)/mkfs: $(TOOLS_DIR)/mkfs.c $(INCLUDE_DIR)/fs.h $(INCLUDE_DIR)/types.h $(INCLUDE_DIR)/stat.h $(INCLUDE_DIR)/param.h | $(BUILD_DIR)
	gcc -Werror -Wall -iquote $(INCLUDE_DIR) -o $(BUILD_DIR)/mkfs $(TOOLS_DIR)/mkfs.c

# ============================================================================
# Filesystem image
# ============================================================================
UPROGS=\
	$(BUILD_DIR)/_beanstalkd\
	$(BUILD_DIR)/_bstest\
	$(BUILD_DIR)/_bsregress\
	$(BUILD_DIR)/_cat\
	$(BUILD_DIR)/_date\
	$(BUILD_DIR)/_clear\
	$(BUILD_DIR)/_curl\
	$(BUILD_DIR)/_dead_loop\
	$(BUILD_DIR)/_desktop\
	$(BUILD_DIR)/_dhcp-client\
	$(BUILD_DIR)/_df\
	$(BUILD_DIR)/_echo-server\
	$(BUILD_DIR)/_dig\
	$(BUILD_DIR)/_echo\
	$(BUILD_DIR)/_forktest\
	$(BUILD_DIR)/_fb-scroll-bench\
	$(BUILD_DIR)/_game\
	$(BUILD_DIR)/_grep\
	$(BUILD_DIR)/_gui\
	$(BUILD_DIR)/_httpd-once\
	$(BUILD_DIR)/_ifconfig\
	$(BUILD_DIR)/_info\
	$(BUILD_DIR)/_init\
	$(BUILD_DIR)/_kill\
	$(BUILD_DIR)/_lua\
	$(BUILD_DIR)/_ln\
	$(BUILD_DIR)/_ls\
	$(BUILD_DIR)/_mkdir\
	$(BUILD_DIR)/_mv\
	$(BUILD_DIR)/_more\
	$(BUILD_DIR)/_netcat\
	$(BUILD_DIR)/_ping\
	$(BUILD_DIR)/_ps\
	$(BUILD_DIR)/_objdump\
	$(BUILD_DIR)/_pwd\
	$(BUILD_DIR)/_prog\
	$(BUILD_DIR)/_readelf\
	$(BUILD_DIR)/_reboot\
	$(BUILD_DIR)/_rm\
	$(BUILD_DIR)/_sh\
	$(BUILD_DIR)/_shutdown\
	$(BUILD_DIR)/_stressfs\
	$(BUILD_DIR)/_telnet\
	$(BUILD_DIR)/_tcc\
	$(BUILD_DIR)/_tcc_sys_regress\
	$(BUILD_DIR)/_touch\
	$(BUILD_DIR)/_top\
	$(BUILD_DIR)/_udp_line_client\
	$(BUILD_DIR)/_uname\
	$(BUILD_DIR)/_uptime\
	$(BUILD_DIR)/_usock_client\
	$(BUILD_DIR)/_usock_server\
	$(BUILD_DIR)/_usocktest\
	$(BUILD_DIR)/_usertests\
	$(BUILD_DIR)/_vi\
	$(BUILD_DIR)/_wc\
	$(BUILD_DIR)/_zombie\

$(BUILD_DIR)/fs.img: $(BUILD_DIR)/mkfs $(DOCS_DIR)/README $(UPROGS) tools/lua_regress.lua $(TCC_SYS_CASES) $(BUILD_DIR)/libtcc1_rt.o $(BUILD_DIR)/libsirpairrt.a user/kk.c | $(BUILD_DIR)
	$(BUILD_DIR)/mkfs $(BUILD_DIR)/fs.img $(DOCS_DIR)/README $(UPROGS) tools/lua_regress.lua $(TCC_SYS_CASES)

# ============================================================================
# Dependencies (auto-generated .d files in build/ 与 build/luaobj/)
# ============================================================================
-include $(BUILD_DIR)/*.d
-include $(wildcard $(LUA_DIR)/*.d)

# ============================================================================
# Clean
# ============================================================================
clean:
	rm -rf $(BUILD_DIR)
	rm -f sirpair-kernel.img sirpair.img sirpairmemfs.img .gdbinit
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg
	@# Clean legacy files from root (before build/ directory migration)
	rm -f *.o *.d *.asm *.sym vectors.S bootblock entryother \
	    initcode initcode.out kernel.elf fs.img kernelmemfs mkfs
	rm -f _cat _echo _forktest _grep _init _kill _ln _ls \
	    _clear _date _dead_loop _desktop _dhcp-client _dig _echo-server _fb-scroll-bench _game _ifconfig _mkdir _more _mv _netcat _objdump _ping _ps _pwd _readelf _rm _sh _stressfs _tcc _tcc_sys_regress _top _touch _udp_line_client _uname _uptime _usock_client _usock_server _usocktest _usertests _vi _wc _zombie

# make a printout
FILES = $(shell grep -v '^\#' $(DOCS_DIR)/runoff.list)
PRINT = $(DOCS_DIR)/runoff.list $(DOCS_DIR)/runoff.spec $(DOCS_DIR)/README \
        $(DOCS_DIR)/toc.hdr $(DOCS_DIR)/toc.ftr $(FILES)

sirpair.pdf: $(PRINT)
	$(DOCS_DIR)/runoff
	ls -l sirpair.pdf

print: sirpair.pdf

# ============================================================================
# QEMU configuration
# ThinkPad X220 hardware emulation:
#   CPU: Intel Core i5-2520M (Sandy Bridge)
#   Chipset: Intel QM67 Express (6 Series)
#   USB: Intel 6 Series EHCI controller
#   NIC: QEMU e1000e (82574L 类); 真机 X220 有线为 82579LM (e1000e 驱动)
# ============================================================================

# run in emulators
bochs : $(BUILD_DIR)/fs.img sirpair.img
	if [ ! -e .bochsrc ]; then ln -s $(TOOLS_DIR)/dot-bochsrc .bochsrc; fi
	bochs -q

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
# 默认四核与 tools/qemu_regress_common 及 docker-build.sh 一致；可用 make qemu CPUS=2 覆盖。
ifndef CPUS
CPUS := 4
endif

X220CPU = SandyBridge,-x2apic,-tsc-deadline,-avx,-syscall,-lm

# QEMU options - USB mode (emulate real USB drive boot, no IDE)
# Test USB driver stack (PCI -> EHCI -> USB -> Mass Storage -> SCSI)
# All disk I/O via EHCI USB controller
QEMUOPTS = -cpu $(X220CPU) \
	-smp $(CPUS) -m 512 \
	-usb -device usb-ehci,id=ehci \
	-device usb-storage,bus=ehci.0,drive=usbdisk,bootindex=1 \
	-drive if=none,id=usbdisk,file=sirpair-kernel.img,format=raw \
	-device e1000e,netdev=net0 \
	-netdev user,id=net0 \
	-rtc base=localtime,clock=host \
	$(QEMUEXTRA)

qemu: sirpair-kernel.img
	$(QEMU) -serial mon:stdio $(QEMUOPTS)

qemu-nox: sirpair-kernel.img
	$(QEMU) -nographic $(QEMUOPTS)

.gdbinit: .gdbinit.tmpl
	sed "s/localhost:1234/localhost:$(GDBPORT)/" < $^ > $@

qemu-gdb: sirpair-kernel.img .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio $(QEMUOPTS) -S $(QEMUGDB)

qemu-nox-gdb: sirpair-kernel.img .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic $(QEMUOPTS) -S $(QEMUGDB)

# ============================================================================
# Distribution
# ============================================================================
EXTRA=\
	$(TOOLS_DIR)/mkfs.c $(USER_DIR)/ulib.c $(INCLUDE_DIR)/user.h \
	$(USER_DIR)/cat.c $(USER_DIR)/clear.c $(USER_DIR)/echo.c $(USER_DIR)/forktest.c \
	$(USER_DIR)/curl.c \
	$(USER_DIR)/dead_loop.c $(USER_DIR)/dhcp-client.c $(USER_DIR)/dig.c $(USER_DIR)/game.c \
	$(USER_DIR)/grep.c $(USER_DIR)/kill.c $(USER_DIR)/ln.c $(USER_DIR)/ping.c \
	$(USER_DIR)/httpd-once.c \
	$(USER_DIR)/ls.c $(USER_DIR)/mkdir.c $(USER_DIR)/more.c $(USER_DIR)/pwd.c $(USER_DIR)/rm.c \
	$(USER_DIR)/stressfs.c $(USER_DIR)/top.c $(USER_DIR)/touch.c $(USER_DIR)/usertests.c $(USER_DIR)/wc.c \
	$(USER_DIR)/usock_client.c $(USER_DIR)/usock_server.c \
	$(USER_DIR)/usocktest.c \
	$(USER_DIR)/zombie.c $(USER_DIR)/printf.c $(USER_DIR)/umalloc.c \
	$(DOCS_DIR)/README $(TOOLS_DIR)/dot-bochsrc $(TOOLS_DIR)/*.pl \
	$(DOCS_DIR)/toc.* $(DOCS_DIR)/runoff $(DOCS_DIR)/runoff1 \
	$(DOCS_DIR)/runoff.list $(TOOLS_DIR)/gdbutil

dist:
	rm -rf dist
	mkdir dist
	for i in $(FILES); \
	do \
		grep -v PAGEBREAK $$i >dist/$$i; \
	done
	sed '/CUT HERE/,$$d' Makefile >dist/Makefile
	echo >dist/runoff.spec
	cp $(EXTRA) dist

dist-test:
	rm -rf dist
	make dist
	rm -rf dist-test
	mkdir dist-test
	cp dist/* dist-test
	cd dist-test; $(MAKE) print
	cd dist-test; $(MAKE) bochs || true
	cd dist-test; $(MAKE) qemu

# update this rule (change rev#) when it is time to
# make a new revision.
tar:
	rm -rf /tmp/sirpair
	mkdir -p /tmp/sirpair
	cp dist/* dist/.gdbinit.tmpl /tmp/sirpair
	(cd /tmp; tar cf - sirpair) | gzip >sirpair-rev5.tar.gz

.PHONY: dist-test dist clean qemu qemu-nox qemu-gdb qemu-nox-gdb bochs tags print
