# Sirpair OS（<a href="/README-CN.md">中文</a>）

<blockquote>
For the latest project updates, visit the Sirpair OS homepage:
<a href="http://sirpair.com">http://sirpair.com</a><br/>
Contact: <a href="mailto:os-dev@sirpair.com">os-dev@sirpair.com</a>
</blockquote>

Sirpair OS is a 32-bit x86 operating system targeting real hardware such as ThinkPad X220/X223, while also running in emulators like QEMU.  
The project adopts parts of xv6/Linux teaching-kernel structures and has evolved with USB boot, networking, graphics, user-space tooling, and third-party software porting capabilities, including Lua, TinyCC, and beanstalkd.

This document is organized into three parts in the following order:

1. Features and capabilities
2. Installation and build guide
3. Architecture and technical principles

## Table of Contents

- [Features and Capabilities](#features-and-capabilities)
- [Installation and Build Guide](#installation-and-build-guide)
- [Architecture and Technical Principles](#architecture-and-technical-principles)

## Features and Capabilities

### 1. Project Positioning

Sirpair OS is a 32-bit x86 operating system for ThinkPad X220/X223 class machines, and also supports emulators such as QEMU.  
It uses part of the xv6/Linux teaching-kernel design and currently provides:

- USB boot and USB disk I/O
- Full EXT2 file system support
- Multicore startup and scheduling
- Wired networking with DHCP/DNS/Ping/TCP/UDP
- Framebuffer graphics and mouse interaction
- A richer user-space command set
- Third-party program ports (Lua, TinyCC, beanstalkd, etc.)

Core code is mainly located in `boot/`, `kernel/`, `include/`, and `user/`, with external components integrated under `thirdparty/`.

### 2. Core Kernel Capabilities

Sirpair OS includes the core capabilities of a small Unix-style operating system:

- Process creation, exit, wait, and kill
- Multicore CPU startup and scheduling
- Paged memory management and kernel page tables
- Interrupts, traps, and system calls
- Files, directories, pipes, and device nodes
- Unix domain sockets
- RTC time and uptime statistics
- Serial console and framebuffer console

From the initialization flow in `kernel/main.c`, the system initializes physical page allocation, page tables, serial output, MP, PIC/IOAPIC, console, process table, IDT, RTC, buffer cache, file table, socket table, inode cache, then continues with disk, networking, and file-system log recovery.

### 3. Storage and Boot Path

A core feature of Sirpair OS is its complete USB boot and USB storage path.  
The image file `sirpair-kernel.img` uses a three-part layout:

- Sector 0: boot sector
- Starting from sector 1: kernel ELF
- Starting from sector 10000: file system

This means the same image can boot in QEMU as a USB storage device and can also be written directly to a USB stick for booting on ThinkPad X220 hardware.

Unlike traditional IDE-based paths in older kernels (for example, xv6 or early Linux 0.10), Sirpair OS explicitly moves to:

`PCI -> EHCI -> USB -> Mass Storage -> SCSI -> Block I/O`

This chain is much closer to real USB boot scenarios.

### 4. File System Capabilities

Sirpair OS borrows the simple Linux/xv6 style file-system model and keeps a logging mechanism for metadata consistency. It supports:

- Regular file read/write
- Directory creation and traversal
- Link and rename-related operations
- File status and file-system statistics
- Journal recovery

The log layer uses a physical redo-log design. System calls that may modify file-system state execute within transaction boundaries, ensuring metadata consistency under crashes or sudden reboot.

### 5. Networking Capabilities

The networking subsystem is one of the most important enhancements in this project.  
It supports:

- Intel e1000/e1000e series NICs
- DHCP automatic addressing
- ARP, ICMP, UDP, TCP
- DNS resolution
- Ping
- User-space TCP/UDP sockets
- Unix domain sockets

Kernel networking is responsible for:

- PCI NIC enumeration
- e1000/e1000e register and DMA ring initialization
- RX/TX queue handling
- DHCP-based IP acquisition

After DHCP succeeds, higher-layer protocol processing is switched to the `microps` stack to reuse mature L3/L4 implementations.

### 6. Graphics and Interaction

Besides serial and text console, Sirpair OS supports graphics and mouse interaction.

Graphics-related capabilities include:

- Framebuffer display
- Graphics mode switching
- GUI pixel buffer submission
- Desktop program: `desktop`
- Graphics demo: `gui`
- Mini game: `game`

System calls include:

- `gui()`
- `guimode()`
- `mouse()`
- `consize()`

The user-space `desktop` program builds a minimal desktop environment on top of a 1024x768 framebuffer and mouse events, showing that the system can evolve from a command-line OS toward a lightweight graphical OS.

### 7. Shell and User-Space Capabilities

The first user-space process is `/init`, which:

- Opens or creates the `console` device node
- Ensures the `null` device exists
- Launches `/bin/sh`

After shell startup, a welcome screen is displayed and command parsing loop begins. User-space programs cover basic commands, system tools, network tools, debug tools, and graphics programs.

Basic commands include:

- `ls`
- `cat`
- `echo`
- `grep`
- `pwd`
- `mkdir`
- `rm`
- `mv`
- `more`
- `wc`
- `vi`

System and monitoring tools include:

- `ps`
- `top`
- `uptime`
- `date`
- `df`
- `uname`
- `info`

Network tools include:

- `ifconfig`
- `ping`
- `dig`
- `curl`
- `telnet`
- `netcat`
- `httpd-once`
- `echo-server`
- `dhcp-client`

Graphics and interaction programs include:

- `desktop`
- `gui`
- `game`

### 8. Third-Party Program Porting

Sirpair OS runs not only built-in small commands but also heavier user-space components:

#### 8.1 Lua Interpreter

The project integrates `Lua 5.5` with compatibility layers under `user/lua/` adapted to Sirpair OS user-space interfaces.

#### 8.2 TinyCC Compiler

The project integrates `TinyCC 0.9.25`; after build, `/bin/tcc` is available inside the image.

#### 8.3 beanstalkd

The project ports `beanstalkd` with Sirpair-specific shims, demonstrating support for service-style processes.

#### 8.4 readelf / objdump

User-space also provides binary analysis tools such as `readelf` and `objdump`.

### 9. Engineering and Testing

Sirpair OS is not "just bootable"; it includes a relatively complete automation pipeline. `docker-build.sh` integrates:

- Build-environment checks
- Build artifact verification
- Image layout verification
- QEMU smoke tests
- Command-level regressions
- GUI and framebuffer regressions
- Lua / TinyCC / beanstalkd regressions

### 10. Summary

From the codebase perspective, Sirpair OS is no longer just a minimal teaching kernel for process switching and syscalls. It is now a small experimental OS platform with:

- USB boot capability
- Runtime support on both real hardware and emulators
- Multicore, file system, journal recovery, and drivers
- Wired networking, DHCP, DNS, TCP/UDP, and sockets
- Shell, practical commands, and user-space service programs
- Framebuffer graphics, desktop apps, and mouse support
- Script interpreter and in-system compiler support

It has value for education, experimentation, system porting, and feature validation.

## Installation and Build Guide

### 1. Document Purpose

This section describes installation, build, run, test, and image usage for the current Sirpair OS repository.  
The recommended method is `docker-build.sh` in Docker for cross-compilation and testing.

### 2. Build Methods Overview

Two major build methods are supported:

- Method 1: Docker (recommended)
- Method 2: Native host `make`

From script design, Docker is the primary path because it already wraps:

- Build environment checks
- Cleanup of old artifacts
- Cross-compilation
- Build artifact verification
- Merged image verification
- QEMU run
- Regression and smoke tests

### 3. Directory and Artifact Reference

Important build-related directories:

- `boot/`: bootloader code
- `kernel/`: kernel code
- `include/`: headers
- `user/`: user-space programs
- `tools/`: utilities and test scripts
- `docs/`: documentation
- `build/`: build output directory

Most important outputs after build:

- `build/bootblock`
- `build/kernel.elf`
- `build/fs.img`
- `sirpair-kernel.img`

`sirpair-kernel.img` is the final bootable merged image.

### 4. Recommended Method: Docker Build

#### 4.1 Prerequisites

Requirements:

- Docker installed
- Docker daemon running
- Local image `x220-os-dev:latest` available

Note: `docker-build.sh` directly uses `x220-os-dev:latest`, while no image-building script is included at repository root. In practice, prepare this image beforehand.

#### 4.2 Basic Build Command

Run in project root:

```bash
./docker-build.sh build
```

If omitted, default action is also `build`:

```bash
./docker-build.sh
```

#### 4.3 Docker Build Flow

`docker-build.sh build` performs:

1. Check Docker and build image availability
2. Run `make clean`
3. Run `make sirpair-kernel.img`
4. Verify `bootblock`, `kernel.elf`, `fs.img`, and key user programs
5. Verify final image layout and boot signature

#### 4.4 Build Results

Successful build produces:

- `sirpair-kernel.img`: final boot image
- `build/`: intermediate artifacts

The script also prints image layout summary and next actions (GUI run, VNC run, serial run, image flashing).

### 5. Makefile Build Details

#### 5.1 Main Target

Core build target:

```bash
make sirpair-kernel.img
```

This depends on:

- `build/bootblock`
- `build/kernel.elf`
- `build/fs.img`

#### 5.2 Bootblock Build

Boot sector is built from `boot/bootasm.S` and `boot/bootmain.c`:

1. Compile assembly and C
2. Link at `0x7C00`
3. Extract `.text`
4. Write `0x55AA` boot signature via `tools/sign.pl`

#### 5.3 kernel.elf Build

Kernel build includes:

1. Compile all `.c` and `.S` under `kernel/`
2. Generate interrupt vectors `vectors.S`
3. Link with `entry.o`, `initcode`, `entryother`, and related binary blobs into `kernel.elf`

#### 5.4 User Program Build

Programs under `user/` compile into `build/*.o`, then link into user binaries such as:

- `build/_ls`
- `build/_sh`
- `build/_ping`
- `build/_lua`
- `build/_tcc`
- `build/_beanstalkd`

These binaries are packed into `fs.img`.

#### 5.5 File-System Image Build

File-system image is generated by `build/mkfs`:

```bash
build/mkfs build/fs.img docs/README <user_program_list>
```

Image contents include:

- `docs/README`
- all user programs
- `tools/lua_regress.lua`
- TinyCC test cases under `tools/tcc_sys_cases/`

#### 5.6 Merged Image Build

Final `sirpair-kernel.img` is assembled via `dd`:

- Create a zero-filled image
- Write bootblock to sector 0
- Write kernel ELF to sector 1
- Write `fs.img` to sector 10000

### 6. Image Layout

Layout is defined by `Makefile` and `include/param.h`:

- File-system start sector: `10000`
- File-system size: `65536` sectors

Final layout:

- Sector 0: boot sector
- Sector 1 to 9999: kernel ELF and padding
- Sector 10000 to 75535: file system

Each sector is 512 bytes, so total image size is approximately:

```text
(10000 + 65536) * 512
```

About 36.8 MB.

### 7. Native Host Build

You can build with native `make` without Docker, but a 32-bit x86 cross-toolchain is required.

#### 7.1 Required Toolchain

Makefile auto-detects toolchain prefixes:

- `i386-jos-elf-`
- `i686-linux-gnu-`
- no-prefix local toolchain

Essential requirements:

- `gcc` capable of `elf32-i386`
- compatible `ld`, `objdump`, `objcopy`

#### 7.2 Native Commands

Typical commands:

```bash
make sirpair-kernel.img
```

GUI run:

```bash
make qemu
```

Serial-only run:

```bash
make qemu-nox
```

GDB debugging:

```bash
make qemu-gdb
```

#### 7.3 Notes

Native build on macOS is usually less stable than Docker due to cross-toolchain and binutils compatibility, plus QEMU differences. Docker is recommended.

### 8. Run Modes

#### 8.1 Native Host GUI Run

Requires host `qemu-system-i386`:

```bash
brew install qemu
./docker-build.sh run
```

Characteristics:

- Host QEMU GUI window
- Image attached as USB storage
- X220-like CPU, EHCI, e1000e simulation

#### 8.2 Docker VNC GUI Run

```bash
./docker-build.sh run-vnc
```

Characteristics:

- No host QEMU dependency
- QEMU runs inside container
- GUI via `vnc://localhost:5900`

#### 8.3 Serial Run

```bash
./docker-build.sh run-nox
```

Suitable for boot-flow debugging, `boot:` stage logs, and headless validation.

#### 8.4 Debug Mode

```bash
./docker-build.sh debug
```

Starts QEMU with GDB stub for kernel debugging.

### 9. Testing and Validation

#### 9.1 Quick Regression

```bash
./docker-build.sh test
```

Covers key paths:

- Basic command outputs
- `ifconfig`
- Shell line editing
- Framebuffer cursor and scrolling
- QEMU smoke

#### 9.2 Full Regression

```bash
./docker-build.sh test-full
```

Runs broader validation, including:

- `vi`
- `desktop`
- `date`
- `mv`
- `df`
- `dig`
- Lua
- beanstalkd
- `telnet`
- `netcat`
- `echo-server`
- TinyCC

#### 9.3 Smoke-Only Test

```bash
./docker-build.sh smoke
```

Checks serial output for:

- `Sirpair OS Booting`
- sufficient `boot:` stage lines
- `init: starting sh`

Useful for quickly validating USB boot path and user-space init.

### 10. Cleanup

```bash
./docker-build.sh clean
```

or:

```bash
make clean
```

Removes:

- `build/`
- `sirpair-kernel.img`
- `sirpairmemfs.img`
- various root-level intermediates

### 11. Write Image to USB Drive

After build, write image directly to USB drive:

```bash
sudo dd if=sirpair-kernel.img of=/dev/sdX bs=512
```

Notes:

- Replace `/dev/sdX` with your actual device
- Verify disk identifier carefully before writing
- Device naming differs between macOS and Linux

### 12. Common Issues

#### 12.1 Docker Image Missing

If script reports:

```text
Docker image x220-os-dev:latest not found
```

Prepare or obtain the required image first.

#### 12.2 Host QEMU Missing

If `run` or `debug` reports missing `qemu-system-i386`, either install QEMU or use `run-vnc` / `run-nox`.

#### 12.3 Native make Cannot Find Cross Toolchain

Install GCC/binutils capable of generating `elf32-i386`, or use Docker build.

### 13. Recommended Workflow

For first-time contributors:

1. Prepare Docker and `x220-os-dev:latest`
2. Run `./docker-build.sh build`
3. Run `./docker-build.sh test`
4. For GUI, run `./docker-build.sh run` or `run-vnc`
5. For debugging, run `./docker-build.sh debug`
6. For real hardware boot, write `sirpair-kernel.img` to USB

### 14. Build/Run Summary

For most developers, the most practical command set is:

```bash
./docker-build.sh build
./docker-build.sh test
./docker-build.sh run-nox
```

For GUI:

```bash
./docker-build.sh run
```

For real hardware flashing, use:

```bash
sirpair-kernel.img
```

## Architecture and Technical Principles

### 1. Architecture Overview

From implementation perspective, Sirpair OS can be abstracted into six layers:

1. Boot layer
2. Kernel core layer
3. Device driver layer
4. Subsystem layer
5. System call interface layer
6. User-space layer

Overall structure:

```text
+------------------------------------------------------+
| User Programs                                        |
| sh, ls, vi, ping, ifconfig, desktop, lua, tcc, ...  |
+------------------------------------------------------+
| System Call ABI                                      |
| file, proc, time, gui, mouse, socket, dhcp, dig     |
+------------------------------------------------------+
| Kernel Subsystems                                    |
| proc, vm, fs, log, net, usb, console, gui, socket   |
+------------------------------------------------------+
| Device Drivers                                       |
| UART, PIC, IOAPIC, EHCI USB, e1000/e1000e, mouse    |
+------------------------------------------------------+
| Boot Loader                                          |
| bootasm.S + bootmain.c                               |
+------------------------------------------------------+
| Hardware / QEMU / ThinkPad X220                      |
+------------------------------------------------------+
```

Sirpair OS adds practical enhancements for real hardware, especially:

- USB boot
- Real-machine networking
- Graphics output
- Third-party user-program ports

### 2. Boot Architecture

#### 2.1 Boot Image Layout

The merged image `sirpair-kernel.img` uses fixed sector layout:

- Sector 0: boot sector
- From sector 1: kernel ELF
- From sector 10000: file system

Benefits:

- Simple BIOS startup path
- Direct USB storage emulation in QEMU
- Direct boot after flashing to real USB drive
- No complex second-stage loader dependency

#### 2.2 Boot Loader Flow

Boot layer consists of:

- `boot/bootasm.S`
- `boot/bootmain.c`

Flow:

1. BIOS loads boot sector and jumps to it
2. `bootasm.S` performs early assembly init
3. BIOS preloads kernel ELF at physical `0x10000`
4. `bootmain()` checks ELF magic
5. `bootmain()` iterates program headers
6. Copy segments to target physical addresses
7. Zero BSS
8. Jump to kernel entry

This is effectively a minimal ELF loader and avoids complex file-system parsing in bootloader stage.

### 3. Kernel Startup Architecture

#### 3.1 Initialization Order

Kernel entry is in `kernel/main.c`. BSP initialization order is roughly:

1. Early page allocator
2. Memory probing
3. GDT / segmentation
4. Kernel page tables
5. Early serial output
6. MP, LAPIC, PIC, IOAPIC
7. Console, null, UART
8. Process table
9. IDT / trap vectors
10. RTC
11. Buffer cache
12. File table
13. Unix socket table
14. Inode cache
15. USB disk
16. Wired network and DHCP
17. File-system journal recovery
18. AP startup
19. Extended page allocator
20. Framebuffer init
21. Create first user process `/init`

This order reflects explicit dependencies:

- Page table and allocator precede complex subsystems
- IDT and ticks precede DHCP/timer-dependent paths
- USB disk must be ready before journal recovery
- APs are released after key single-thread setup for stability

#### 3.2 Multicore Startup Strategy

Sirpair OS borrows SMP ideas from Linux/xv6 while using conservative synchronization:

- BSP completes most critical initialization
- APs enter from `entryother.S`
- APs wait for `ap_go` in `mpmain()`
- APs enter scheduler only after BSP completes critical startup work

### 4. Memory Management Architecture

#### 4.1 Two-Stage Page Allocation

Using xv6-style `kalloc`, startup uses two stages:

- `kinit1()`: initialize part of physical pages
- `kinit2()`: add remaining pages after more subsystems become stable

#### 4.2 Virtual Memory Organization

Virtual memory uses page tables for kernel/user spaces:

- Kernel high-address mapping
- Independent user page tables
- `copyuvm`, `allocuvm`, `deallocuvm` maintain user address space

To support larger user programs, `USTACK_PAGES` is expanded to 16 pages.

### 5. Process and Scheduling

Process model includes:

- `fork`
- `exec`
- `exit`
- `wait`
- `scheduler`

Enhancements include:

- Foreground process control via `setfgpid`
- Better shell/console interaction
- Background-task stdin isolation via `null`

### 6. File System and Journal

#### 6.1 File-System Organization

File system follows Linux/xv6-style inode, directory entry, and buffer-cache model:

- `bio.c`
- `fs.c`
- `file.c`
- `sysfile.c`

Supports path resolution, inode cache, directory links, file I/O, `stat/statfs`, and `lseek`.

#### 6.2 Journal Principle

`kernel/log.c` uses physical redo log:

- Write modified blocks to log area
- Commit by writing log header
- Replay log blocks to home locations
- Clear log header

Features:

- Single active transaction at a time
- Simple consistency behavior
- Straightforward crash recovery

### 7. Storage Architecture: USB-Only Block I/O

#### 7.1 Design Goal

Sirpair OS removes IDE disk dependency and fully shifts to USB:

- Support USB-stick image boot
- Match X220 real startup path
- Simulate EHCI + USB storage in QEMU

#### 7.2 USB Subsystem Structure

USB path includes:

- PCI EHCI controller enumeration
- EHCI MMIO access
- qTD/QH DMA structures
- Device enumeration
- Mass storage initialization
- Block I/O read/write interface

The implementation emphasizes correct DMA structure layout, alignment-safe static DMA buffers, and staged controller initialization with recovery polling.

#### 7.3 Integration with File System

`diskinit()` and `diskrw()` wrap USB Mass Storage into block-device interfaces for `bread()`, `bwrite()`, and logging layers, keeping upper FS logic storage-agnostic.

### 8. Network Architecture

#### 8.1 Overall Design

Networking combines custom low-level driver logic with reusable upper protocol stack:

- Lower layer: native `e1000/e1000e`, ARP, DHCP, RX/TX queue management
- Upper layer: `microps` for stable L3/L4 processing

#### 8.2 NIC Driver Layer

`kernel/net.c` handles:

- PCI scan for Intel NICs
- MMIO BAR mapping
- TX/RX ring initialization
- Packet RX/TX
- Early DHCP/ARP/ICMP/UDP/TCP processing

#### 8.3 DHCP and Network Bring-up

Startup flow:

1. Initialize NIC
2. Enable TX/RX
3. Run DHCP
4. Sync config into `microps`
5. Switch to `microps` as primary protocol path

#### 8.4 User-Space Network API

Syscall layer exposes Unix-like socket API:

- `socket`, `bind`, `listen`, `accept`, `connect`
- `send`, `recv`, `recvfrom`, `sendto`

Supports both `AF_INET` and `AF_UNIX`.

### 9. Graphics Architecture

#### 9.1 Design

Kernel provides framebuffer services, user-space draws:

Kernel:

- Framebuffer initialization
- GUI mode switching
- User pixel-buffer blit to VRAM
- Console/graphics mode coordination

User-space:

- Pixel-buffer generation
- Mouse event handling
- Desktop/window/icon rendering logic

#### 9.2 Display Path

`sys_gui()` supports:

- `1024x768` RGB332 buffer
- `1024x768x3` BGR true-color buffer

Kernel writes to VRAM based on current VESA/VGA mode.

#### 9.3 Input Path

Mouse input via `sys_mouse()`:

- `mouseinit()`
- `mousepoll()`

`desktop` uses these to build a lightweight graphical desktop.

### 10. System Call Architecture

Syscalls are grouped into:

#### 10.1 Traditional Unix-like

- Process management
- File and directory operations
- Pipe operations
- Memory growth
- Sleep and uptime

#### 10.2 System-enhanced

- `time`
- `statfs`
- `lseek`
- `getcwd`
- `getprocs`
- `reboot`
- `shutdown`

#### 10.3 Networking

- `socket`, `bind`, `listen`, `accept`, `connect`
- `send`, `recv`, `recvfrom`, `sendto`
- `dhcp`, `getnetcfg`, `ping`, `dig`

#### 10.4 Graphics

- `gui`
- `guimode`
- `mouse`
- `consize`

### 11. User-Space Architecture

Startup chain:

1. Kernel creates first process
2. Execute `/init`
3. `/init` ensures `console` and `null`
4. `/init` launches `/bin/sh`
5. Shell forks/execs other commands

User-space includes:

- Basic commands
- System monitoring tools
- Networking utilities
- Graphics and advanced programs
- Lua scripting
- TinyCC on-system compilation
- beanstalkd service runtime
- ELF analysis tools

### 12. Third-Party Porting Strategy

Ports are not simple source copies; Sirpair OS uses compatibility headers and shim layers:

- `user/lua/`: Lua runtime compatibility
- `user/tcc/`: TinyCC interface and printf behavior adaptation
- `user/beanstalkd/`: libc/time/network/formatting shims

### 13. Engineering and Validation

#### 13.1 Build Automation

`docker-build.sh` unifies build, run, test, verify, and debug into one entry.

#### 13.2 Regression Coverage

Automated regressions cover:

- Command behavior
- Network paths
- Graphical desktop
- Shell line editing
- Framebuffer performance
- Lua/TCC/beanstalkd

### 14. Key Technical Characteristics

- Readable, compact system-kernel structure
- Real-hardware-oriented USB and NIC enhancements
- Layered design from drivers to user-space
- Hybrid networking: custom low level + reusable upper stack
- Lightweight graphics model: kernel framebuffer + user rendering
- Third-party ecosystem via shims (Lua, TinyCC, beanstalkd)
- Continuous capability validation via automated regressions

### 15. Conclusion

Sirpair OS is designed as a lightweight experimental OS that runs on both emulators and real hardware, while borrowing strong operating-system ideas from Linux/xv6/Unix.

Its path can be summarized as:

- Keep boot path simple and direct
- Retain Linux/xv6-style kernel structure
- Strengthen real-hardware driver usability
- Focus subsystem enhancements on USB, networking, and graphics
- Expand syscall interfaces
- Improve practicality with command ecosystem and third-party ports

It is suitable both for learning Unix-style kernel architecture and for comprehensive experiments involving drivers, protocol stacks, graphics, and user-space porting.

## Acknowledgements

Finally, thanks to the open-source communities and ideas behind Linux, xv6, Unix, Lua, microps, and beanstalkd, which made this project possible.
