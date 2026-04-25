# Sirpair OS （<a href="/README.md">English</a>）

<blockquote>
想要获取最新进展，请访问 Sirpair OS 网站主页：<a href="http://sirpair.com">http://sirpair.com</a>，联系：<a href="mailto:os-dev@sirpair.com">os-dev@sirpair.com</a>
</blockquote>

Sirpair OS 是一款32 位 x86 操作系统，目标硬件为 ThinkPad X220/X223等机型，同时支持在 QEMU 等模拟器中运行。该项目采用了部分 xv6/linux 教学型内核结构，开发并具备了USB 启动、网络、图形、用户态工具链和第三方程序移植能力，并移植了lua、tcc和beanstalk等著名开源软件。

本文档按以下顺序整合了三部分内容：

1. 功能与能力介绍
2. 安装与编译说明
3. 架构设计与技术原理

## 目录

- [功能与能力介绍](#功能与能力介绍)
- [安装与编译说明](#安装与编译说明)
- [架构设计与技术原理](#架构设计与技术原理)

## 功能与能力介绍

### 1. 项目定位

Sirpair OS 是一款 32 位 x86 操作系统，目标硬件为 ThinkPad X220/X223等机型，同时支持在 QEMU 等模拟器中运行。项目采用了部分 xv6/linux 教学型内核结构，开发并具备了以下能力：

- USB 启动与 USB 磁盘 I/O
- 完整的EXT2文件系统
- 多核启动与调度
- 有线网络与 DHCP/DNS/Ping/TCP/UDP
- 图形帧缓冲与鼠标交互
- 更完整的用户态命令集
- Lua、TinyCC、beanstalkd 等第三方程序移植

从代码结构看，系统主体位于 `boot/`、`kernel/`、`include/`、`user/`，并通过 `thirdparty/` 集成多个外部组件。

### 2. 内核基础能力

Sirpair OS 具备一个小型 Unix 风格操作系统的核心能力：

- 进程创建、退出、等待、杀死
- 多核 CPU 启动与调度
- 页式内存管理与内核页表
- 中断、陷阱、系统调用
- 文件、目录、管道、设备节点
- Unix 域套接字
- RTC 时间与 uptime 统计
- 串口控制台与帧缓冲控制台

从 `kernel/main.c` 的初始化流程可以看出，系统在启动时依次完成物理页分配、页表、串口、MP、PIC/IOAPIC、控制台、进程表、IDT、RTC、缓冲区缓存、文件表、socket 表、inode 缓存等初始化，然后继续进行磁盘、网络和文件系统日志恢复。

### 3. 存储与启动能力

Sirpair OS 的一个核心特点是完整的 USB 启动和 USB 存储路径。项目的镜像文件 `sirpair-kernel.img` 采用三段式布局：

- 扇区 0：引导扇区
- 扇区 1 开始：内核 ELF
- 扇区 10000 开始：文件系统

这意味着该镜像既可以被 QEMU 当作 USB 存储设备启动，也可以直接写入 U 盘后在 ThinkPad X220 真机上引导。

与传统内核不同，比如 xv6或者早期的linux内核0.10等 常见的 IDE 路径不同，Sirpair OS 明确转向：

`PCI -> EHCI -> USB -> Mass Storage -> SCSI -> Block I/O`

这条链路使系统更贴近真实 USB 启动场景。

### 4. 文件系统能力

Sirpair OS 借鉴了 linux/xv6 的简洁文件系统模型，并保留了日志机制用于元数据一致性保护。系统支持：

- 普通文件读写
- 目录创建与遍历
- 链接与重命名相关操作
- 文件状态与文件系统统计
- 日志恢复

日志层采用物理 redo log 设计。所有可能修改文件系统的系统调用，都会在事务边界内进行，从而在异常掉电或重启时保证元数据一致性。

### 5. 网络能力

Sirpair OS 的网络子系统是本项目最重要的增强之一。系统支持：

- Intel e1000/e1000e 系列网卡
- DHCP 自动获取地址
- ARP、ICMP、UDP、TCP
- DNS 解析
- Ping
- 用户态 TCP/UDP socket
- Unix 域 socket

内核网络部分负责：

- PCI 枚举网卡
- 初始化 e1000/e1000e 寄存器和 DMA ring
- 处理收发队列
- 完成 DHCP 获取 IP

DHCP 成功后，系统会把更高层的协议处理切换到 `microps` 协议栈，以便复用更成熟的 L3/L4 处理能力。这种设计兼顾了硬件控制能力和协议栈复用能力。

### 6. 图形与交互能力

除了传统串口和文本控制台，Sirpair OS 还支持图形显示与鼠标交互。

图形相关能力包括：

- 帧缓冲显示
- 图形模式切换
- GUI 像素缓冲提交
- 桌面程序 `desktop`
- 图形展示程序 `gui`
- 图形小游戏 `game`

系统调用层提供了：

- `gui()`
- `guimode()`
- `mouse()`
- `consize()`

用户态 `desktop` 程序基于 1024x768 桌面缓冲和鼠标事件，构造出一个简易桌面环境。该设计说明系统已具备从“命令行 OS”走向“轻量图形 OS”的基础能力。

### 7. Shell 与用户空间能力

系统的第一个用户态程序是 `/init`，它负责：

- 打开或创建设备节点 `console`
- 确保 `null` 设备存在
- 启动 `/bin/sh`

Shell 启动后会显示欢迎界面，并进入命令解析循环。用户态程序覆盖了基础命令、系统工具、网络工具、调试工具和图形程序。

基础命令包括：

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

系统与监控类工具包括：

- `ps`
- `top`
- `uptime`
- `date`
- `df`
- `uname`
- `info`

网络工具包括：

- `ifconfig`
- `ping`
- `dig`
- `curl`
- `telnet`
- `netcat`
- `httpd-once`
- `echo-server`
- `dhcp-client`

图形与交互类程序包括：

- `desktop`
- `gui`
- `game`

### 8. 第三方程序移植能力

Sirpair OS 不只运行自带小命令，还移植了多个较重的用户态组件：

#### 8.1 Lua 解释器

项目集成了 `Lua 5.5`，并通过 `user/lua/` 下的兼容层适配 Sirpair OS 的用户态接口。这意味着系统具备脚本执行能力，可用于交互测试和扩展实验。

#### 8.2 TinyCC 编译器

项目集成了 `TinyCC 0.9.25`，构建后会在镜像内生成 `/bin/tcc`。这让系统具备在目标 OS 内部进行 C 代码编译的能力，是一个很有代表性的“自举式增强”。

#### 8.3 beanstalkd

项目还移植了 `beanstalkd`，并提供了适配 Sirpair OS 的 shim 层。这说明系统不仅能运行简单工具，也已经可以承载服务型进程。

#### 8.4 readelf / objdump

用户态中还提供了 `readelf`、`objdump` 等二进制分析工具，增强了系统自检、调试和教学展示能力。

### 9. 工程化与测试能力

项目并非“只要能启动即可”，而是包含了较完善的自动化回归与冒烟测试机制。`docker-build.sh` 中集成了：

- 编译环境检查
- 基础构建产物校验
- 镜像布局校验
- QEMU 冒烟测试
- 命令级回归测试
- GUI 与 framebuffer 回归
- Lua / TinyCC / beanstalkd 回归

这表明项目已经具备较强的工程化和持续验证能力。

### 10. 总结

从代码实现看，Sirpair OS 已经不是一个“仅能演示进程切换和系统调用”的最小教学内核，而是一个具备以下综合能力的小型操作系统实验平台：

- 可从 USB 启动
- 可在真机和模拟器运行
- 具备多核、文件系统、日志恢复和设备驱动
- 支持有线网络、DHCP、DNS、TCP/UDP 和 socket
- 支持 shell、常用命令和用户态服务程序
- 支持图形帧缓冲、桌面程序和鼠标
- 支持脚本解释器与系统内编译器

它兼具教学、实验、系统移植和功能验证价值。

## 安装与编译说明

### 1. 文档目的

本文档基于项目当前代码仓库整理，说明 Sirpair OS 的安装、编译、运行、测试和镜像使用流程。当前仓库最推荐的方式是通过 `docker-build.sh` 在 Docker 环境中完成交叉编译和测试。

### 2. 构建方式概览

项目支持两种主要构建方式：

- 方式一：Docker 方式，推荐
- 方式二：本机直接执行 `make`

从仓库脚本设计看，Docker 方式是主路径，因为它已经封装了：

- 编译环境检查
- 清理旧产物
- 交叉编译
- 构建产物校验
- 合并镜像校验
- QEMU 运行
- 回归测试和冒烟测试

### 3. 目录与产物说明

构建相关的重要目录如下：

- `boot/`：引导加载器代码
- `kernel/`：内核代码
- `include/`：头文件
- `user/`：用户态程序
- `tools/`：工具程序和测试脚本
- `docs/`：文档
- `build/`：编译输出目录

构建完成后，最重要的输出文件为：

- `build/bootblock`
- `build/kernel.elf`
- `build/fs.img`
- `sirpair-kernel.img`

其中 `sirpair-kernel.img` 是最终可启动的合并镜像。

### 4. 推荐方式：Docker 编译

#### 4.1 前置条件

需要满足以下条件：

- 已安装 Docker
- Docker 守护进程可正常运行
- 本地已存在镜像 `x220-os-dev:latest`

注意：当前仓库内可以看到 `docker-build.sh` 会直接使用 `x220-os-dev:latest`，但没有在仓库根目录看到对应镜像的构建脚本。因此在实际操作时，需要你们已有该镜像，或在外部环境中提前构建好它。

#### 4.2 基本编译命令

在项目根目录执行：

```bash
./docker-build.sh build
```

如果省略参数，脚本默认也是 `build`：

```bash
./docker-build.sh
```

#### 4.3 Docker 编译流程

`docker-build.sh build` 实际会依次执行以下步骤：

1. 检查 Docker 和编译镜像是否可用
2. 执行 `make clean`
3. 执行 `make sirpair-kernel.img`
4. 检查 `bootblock`、`kernel.elf`、`fs.img` 和关键用户程序
5. 校验最终镜像布局和引导签名

#### 4.4 编译成功后的结果

成功后会得到：

- `sirpair-kernel.img`：最终启动镜像
- `build/`：中间产物目录

脚本还会输出镜像布局摘要和后续可执行操作，例如图形运行、VNC 运行、串口运行和写盘。

### 5. Makefile 构建过程详解

#### 5.1 主要构建目标

项目的核心目标是：

```bash
make sirpair-kernel.img
```

这个目标依赖：

- `build/bootblock`
- `build/kernel.elf`
- `build/fs.img`

#### 5.2 bootblock 构建

引导扇区由 `boot/bootasm.S` 和 `boot/bootmain.c` 构成，构建过程会：

1. 编译汇编和 C 文件
2. 链接到 `0x7C00`
3. 提取 `.text`
4. 用 `tools/sign.pl` 写入 `0x55AA` 引导签名

#### 5.3 kernel.elf 构建

内核构建分为三部分：

1. 编译 `kernel/` 下所有 `.c` 和 `.S`
2. 生成中断向量表 `vectors.S`
3. 与 `entry.o`、`initcode`、`entryother` 等二进制段一起链接成 `kernel.elf`

#### 5.4 用户程序构建

`user/` 下的各类命令会先编译为 `build/*.o`，再链接成以下形式的用户程序：

- `build/_ls`
- `build/_sh`
- `build/_ping`
- `build/_lua`
- `build/_tcc`
- `build/_beanstalkd`

这些程序随后会被写入 `fs.img`。

#### 5.5 文件系统镜像构建

文件系统镜像由 `build/mkfs` 负责生成：

```bash
build/mkfs build/fs.img docs/README <用户程序列表>
```

写入镜像的内容包括：

- `docs/README`
- 所有用户态程序
- `tools/lua_regress.lua`
- `tools/tcc_sys_cases/` 中的 TinyCC 用例

#### 5.6 合并镜像构建

最终镜像 `sirpair-kernel.img` 通过 `dd` 合成：

- 先创建一个全零镜像文件
- 将 bootblock 写入扇区 0
- 将 kernel.elf 写入扇区 1
- 将 fs.img 写入扇区 10000

### 6. 镜像布局说明

镜像布局由 `Makefile` 和 `include/param.h` 共同确定：

- 文件系统起始扇区：`10000`
- 文件系统大小：`65536` 扇区

最终布局如下：

- 扇区 0：引导扇区
- 扇区 1 到 9999：内核 ELF 与填充
- 扇区 10000 到 75535：文件系统

由于每扇区为 512 字节，所以镜像总大小约为：

```text
(10000 + 65536) * 512
```

约等于 36.8 MB。

### 7. 原生本机编译方式

如果不使用 Docker，也可以直接执行 `make`，但需要本机具备 32 位 x86 交叉工具链。

#### 7.1 需要的工具

Makefile 会尝试自动探测以下工具链前缀：

- `i386-jos-elf-`
- `i686-linux-gnu-`
- 无前缀本地工具链

本质要求是：

- `gcc` 可生成 `elf32-i386`
- `ld`、`objdump`、`objcopy` 等配套工具可用

#### 7.2 原生命令

典型命令如下：

```bash
make sirpair-kernel.img
```

图形运行：

```bash
make qemu
```

纯串口运行：

```bash
make qemu-nox
```

GDB 调试：

```bash
make qemu-gdb
```

#### 7.3 注意事项

在 macOS 上直接原生构建通常不如 Docker 稳定，因为：

- 需要专门的交叉编译器
- 需要兼容的 `elf32-i386` binutils
- QEMU 路径和行为可能与 Linux 环境不同

因此更建议使用 Docker 方式。

### 8. 运行方式

#### 8.1 宿主机原生 GUI 运行

需要宿主机安装 `qemu-system-i386`：

```bash
brew install qemu
./docker-build.sh run
```

特征：

- 使用宿主机 QEMU 图形窗口
- 以 USB 存储设备方式挂载镜像
- 模拟 X220 风格的 CPU、EHCI、e1000e

#### 8.2 Docker 内 VNC 图形运行

```bash
./docker-build.sh run-vnc
```

特征：

- 不依赖宿主机 QEMU
- 容器内运行 QEMU
- 通过 `vnc://localhost:5900` 查看图形界面

#### 8.3 串口方式运行

```bash
./docker-build.sh run-nox
```

适合：

- 调试启动流程
- 查看 `boot:` 阶段输出
- 在无图形环境下验证系统行为

#### 8.4 调试模式

```bash
./docker-build.sh debug
```

该模式会以 GDB stub 启动 QEMU，方便进行内核调试。

### 9. 测试与验证

#### 9.1 快速回归

```bash
./docker-build.sh test
```

该命令会执行关键链路测试，包括：

- 基础命令输出
- `ifconfig`
- shell 行编辑
- framebuffer 光标和滚动
- QEMU 冒烟

#### 9.2 全量回归

```bash
./docker-build.sh test-full
```

它会执行更全面的自动化验证，例如：

- `vi`
- `desktop`
- `date`
- `mv`
- `df`
- `dig`
- `Lua`
- `beanstalkd`
- `telnet`
- `netcat`
- `echo-server`
- `TinyCC`

#### 9.3 仅冒烟测试

```bash
./docker-build.sh smoke
```

脚本会检查串口中是否出现：

- `Sirpair OS Booting`
- 足够数量的 `boot:` 阶段行
- `init: starting sh`

这可用于快速确认 USB 启动链路和用户态初始化是否正常。

### 10. 清理构建产物

执行：

```bash
./docker-build.sh clean
```

或直接：

```bash
make clean
```

该操作会清理：

- `build/`
- `sirpair-kernel.img`
- `sirpairmemfs.img`
- 若干遗留根目录中间产物

### 11. 将镜像写入 U 盘

构建完成后，可以将镜像直接写入 U 盘：

```bash
sudo dd if=sirpair-kernel.img of=/dev/sdX bs=512
```

使用时请注意：

- 将 `/dev/sdX` 替换为实际设备
- 写盘前确认磁盘号，避免误覆盖
- macOS 和 Linux 的设备命名规则不同，需要按实际系统调整

### 12. 常见问题

#### 12.1 Docker 镜像不存在

如果脚本提示：

```text
Docker 镜像 x220-os-dev:latest 不存在
```

说明当前机器尚未准备好项目所需构建环境，需要先获取或构建该镜像。

#### 12.2 宿主机没有 QEMU

如果执行 `run` 或 `debug` 时报错缺少 `qemu-system-i386`，可以：

- 安装 `qemu`
- 改用 `run-vnc`
- 改用 `run-nox`

#### 12.3 原生 make 找不到交叉工具链

如果直接 `make` 报错找不到 `i386-*-elf` 工具链，需要安装能生成 `elf32-i386` 的 GCC/binutils，或者直接改用 Docker 方式。

### 13. 推荐使用流程

对于首次使用项目的开发者，推荐按如下顺序操作：

1. 准备 Docker 和 `x220-os-dev:latest`
2. 执行 `./docker-build.sh build`
3. 执行 `./docker-build.sh test`
4. 需要图形时执行 `./docker-build.sh run` 或 `run-vnc`
5. 需要调试时执行 `./docker-build.sh debug`
6. 需要真机启动时将 `sirpair-kernel.img` 写入 U 盘

### 14. 总结

Sirpair OS 的构建系统已经相对完整，推荐直接使用 Docker 编译脚本完成从编译、校验、测试到运行的全流程。对大多数开发者而言，最实用的命令组合是：

```bash
./docker-build.sh build
./docker-build.sh test
./docker-build.sh run-nox
```

如果需要图形展示，再使用：

```bash
./docker-build.sh run
```

如果需要写入真机，则使用最终产物：

```bash
sirpair-kernel.img
```

## 架构设计与技术原理

### 1. 架构总览

从代码实现看，Sirpair OS 可以抽象为以下六层：

1. 引导层
2. 内核核心层
3. 设备驱动层
4. 子系统层
5. 系统调用接口层
6. 用户空间层

其总体结构如下：

```text
+------------------------------------------------------+
| User Programs                                        |
| sh, ls, vi, ping, ifconfig, desktop, lua, tcc, ...   |
+------------------------------------------------------+
| System Call ABI                                      |
| file, proc, time, gui, mouse, socket, dhcp, dig      |
+------------------------------------------------------+
| Kernel Subsystems                                    |
| proc, vm, fs, log, net, usb, console, gui, socket    |
+------------------------------------------------------+
| Device Drivers                                       |
| UART, PIC, IOAPIC, EHCI USB, e1000/e1000e, mouse     |
+------------------------------------------------------+
| Boot Loader                                          |
| bootasm.S + bootmain.c                               |
+------------------------------------------------------+
| Hardware / QEMU / ThinkPad X220                      |
+------------------------------------------------------+
```

Sirpair OS 围绕真实硬件能力做了更多增强，尤其是：

- USB 启动
- 真机网络
- 图形输出
- 第三方用户程序移植

### 2. 引导架构

#### 2.1 启动镜像布局

系统使用一个合并镜像 `sirpair-kernel.img`，镜像内部按固定扇区布局组织：

- 扇区 0：boot sector
- 扇区 1 开始：内核 ELF
- 扇区 10000 开始：文件系统

这类设计的优点是：

- BIOS 启动路径简单
- QEMU 可直接用 USB 存储设备模拟
- 真机写盘后可直接启动
- 不依赖复杂二级引导器

#### 2.2 Boot Loader 工作流程

引导层由两部分组成：

- `boot/bootasm.S`
- `boot/bootmain.c`

整体过程如下：

1. BIOS 将 boot sector 装入内存并开始执行
2. `bootasm.S` 进行早期汇编初始化
3. BIOS 将内核 ELF 预读到物理地址 `0x10000`
4. `bootmain()` 检查 ELF 魔数
5. `bootmain()` 遍历 Program Header
6. 将各段复制到目标物理地址
7. 清零 BSS
8. 跳转到内核入口

这种方式的本质是“极简 ELF 加载器”，避免在 bootloader 阶段引入复杂文件系统解析逻辑。

### 3. 内核启动架构

#### 3.1 启动顺序设计

内核入口在 `kernel/main.c` 中。Bootstrap Processor 启动后，初始化顺序大致为：

1. 早期页分配器
2. 内存探测
3. GDT / segmentation
4. 内核页表
5. 串口早期输出
6. MP、LAPIC、PIC、IOAPIC
7. 控制台、null、UART
8. 进程表
9. IDT / trap vectors
10. RTC
11. buffer cache
12. file table
13. unix socket table
14. inode cache
15. USB 磁盘
16. 有线网络与 DHCP
17. 文件系统日志恢复
18. AP 启动
19. 扩展页分配器
20. framebuffer 初始化
21. 创建第一个用户进程 `/init`

这种顺序体现了明确的依赖关系：

- 页表和分配器要先于复杂子系统
- IDT 和 ticks 要先于依赖时钟的 DHCP
- USB 磁盘必须先就绪，文件系统日志恢复才能执行
- AP 必须在关键单线程初始化后再放行，避免并发干扰

#### 3.2 多核启动策略

Sirpair OS 借鉴了 linux/xv6 的 SMP 设计思路，但做了更保守的同步控制。

关键点包括：

- BSP 负责完成大部分关键初始化
- AP 从 `entryother.S` 进入
- AP 在 `mpmain()` 中等待 `ap_go`
- 只有当 BSP 完成关键启动输出和初始化后，AP 才进入调度器

这种设计是为了降低真实硬件上的竞态风险，特别是：

- USB 初始化尚未稳定
- 早期控制台还在输出
- 文件系统和网络栈尚未完全 ready

### 4. 内存管理架构

#### 4.1 两阶段页分配

系统借鉴和使用 linux/xv6 风格的物理页分配器 `kalloc`，但内核启动时采用两阶段初始化：

- `kinit1()`：先初始化一部分物理页
- `kinit2()`：待更多系统资源稳定后，再接入剩余页

这样做的原因是：

- 内核早期尚未具备完整映射
- AP 启动、ACPI、页表扩展等都需要保留启动期资源

#### 4.2 虚拟内存组织

系统使用页表管理内核和用户地址空间：

- 内核高地址映射
- 用户空间独立页表
- `copyuvm`、`allocuvm`、`deallocuvm` 等操作维护用户虚拟地址空间

此外，为支持更复杂的用户程序，`USTACK_PAGES` 已扩展到 16 页，说明该系统已考虑如 Lua 这类需要更深用户栈的应用。

### 5. 进程与调度架构

Sirpair OS 的进程管理思路：

- `fork`
- `exec`
- `exit`
- `wait`
- `scheduler`

调度仍是简单内核调度器，但已配合以下增强：

- 前台进程控制 `setfgpid`
- shell 与控制台交互改进
- 后台任务使用 `null` 设备隔离标准输入


### 6. 文件系统与日志架构

#### 6.1 文件系统组织

文件系统借鉴和采用了 linux/xv6 的 inode、目录项和块缓存模型，主要模块包括：

- `bio.c`
- `fs.c`
- `file.c`
- `sysfile.c`

该文件系统支持：

- 路径解析
- inode 缓存
- 目录链接
- 文件读写
- stat/statfs
- lseek

#### 6.2 日志机制原理

日志模块 `kernel/log.c` 使用物理 redo log：

- 修改块先写入日志区
- 写日志头作为真正 commit 点
- 再将日志块拷回原位
- 最后清空日志头

特点是：

- 任一时刻只允许一个事务处于活动状态
- 简化了并发修改时的一致性处理
- 崩溃恢复过程直接重放日志

这对教学型内核而言实现简单、行为明确。

### 7. 存储架构：USB-only Block I/O

#### 7.1 设计目标

Sirpair OS 存储栈最大的变化是完全转向 USB，删除了 IDE 磁盘依赖。其目标是：

- 支持 U 盘镜像启动
- 贴近 X220 真实启动路径
- 在 QEMU 中模拟 EHCI + USB storage

#### 7.2 USB 子系统结构

USB 路径主要由以下部分组成：

- PCI 枚举 EHCI 控制器
- EHCI MMIO 访问
- qTD / QH DMA 结构
- 设备枚举
- Mass Storage 初始化
- 块 I/O 读写接口

代码中特别强调：

- DMA 结构体必须正确包含 64 位 EHCI 所需字段
- DMA 缓冲区需静态分配并满足对齐要求
- 采用两阶段控制器初始化和轮询恢复机制

这种实现方式明显是为真实硬件稳定性做过针对性优化。

#### 7.3 与文件系统的衔接

`diskinit()` 和 `diskrw()` 把 USB Mass Storage 包装成块设备接口，供 `bread()`、`bwrite()` 和日志层复用。这样上层文件系统无需感知底层是 IDE 还是 USB。

这体现了良好的分层设计：

- 设备层关心 USB 协议和 DMA
- 文件系统层只关心块号和缓冲区

### 8. 网络架构

#### 8.1 总体设计

网络栈采用“自研驱动 + 第三方协议栈结合”的方式：

- 底层：内核原生 `e1000/e1000e` 驱动、ARP、DHCP、收发队列管理
- 上层：`microps` 负责更稳定的 L3/L4 处理

这种架构兼顾：

- 真实网卡控制
- 启动期网络 bring-up
- 协议处理复用

#### 8.2 网卡驱动层

`kernel/net.c` 负责：

- PCI 扫描 Intel 网卡
- 映射 MMIO BAR
- 初始化 TX/RX ring
- 收包和发包
- DHCP、ARP、ICMP、UDP、TCP 早期处理

对 `82574`、`82579LM`、QEMU `e1000/e1000e` 的兼容处理较多，说明该模块是按真机行为做过专门适配的。

#### 8.3 DHCP 与网络启动

系统启动时会：

1. 初始化网卡
2. 打开发包收包能力
3. 执行 DHCP 获取地址
4. 若成功，将配置同步到 `microps`
5. 切换到 `microps` 作为主协议处理路径

这使系统既能在引导阶段快速完成网络配置，又能在稳定运行阶段使用更成熟的网络栈。

#### 8.4 用户态网络接口

系统调用层暴露出类 Unix socket 接口：

- `socket`
- `bind`
- `listen`
- `accept`
- `connect`
- `send`
- `recv`
- `recvfrom`
- `sendto`

同时支持：

- `AF_INET`
- `AF_UNIX`

其中：

- `AF_INET` 走 TCP/UDP 网络 slot
- `AF_UNIX` 走本地 Unix socket 表

这使网络编程接口保持统一，而底层实现可按域类型分别处理。

### 9. 图形架构

#### 9.1 设计思路

Sirpair OS 的图形系统采用“内核提供 framebuffer 能力，用户态负责绘制”的方案。

内核负责：

- 初始化 framebuffer
- 切换 GUI 模式
- 将用户像素缓冲输出到显存
- 处理控制台与图形模式关系

用户态负责：

- 生成像素缓冲
- 接收鼠标事件
- 完成窗口、桌面、图标等绘制逻辑

#### 9.2 显示路径

`sys_gui()` 支持用户传入两种缓冲格式：

- `1024x768` 的 RGB332
- `1024x768x3` 的 BGR 真彩色

内核再根据当前 VESA/VGA 模式把像素写入显存。这样做的优势是：

- 简化用户态 GUI 程序
- 避免把复杂显示硬件细节暴露给应用
- 便于在不同显示模式之间兼容

#### 9.3 输入路径

鼠标输入通过 `sys_mouse()` 暴露给用户态：

- `mouseinit()`
- `mousepoll()`

`desktop` 程序利用这些接口构造出图形桌面，这是一种非常直接的“内核最小图形服务 + 用户态桌面逻辑”架构。

### 10. 系统调用架构

Sirpair OS 的系统调用接口,主要分为以下几类：

#### 10.1 传统 Unix 类

- 进程管理
- 文件和目录
- 管道
- 内存分配
- 睡眠与 uptime

#### 10.2 系统增强类

- `time`
- `statfs`
- `lseek`
- `getcwd`
- `getprocs`
- `reboot`
- `shutdown`

#### 10.3 网络类

- `socket`
- `bind`
- `listen`
- `accept`
- `connect`
- `send`
- `recv`
- `recvfrom`
- `sendto`
- `dhcp`
- `getnetcfg`
- `ping`
- `dig`

#### 10.4 图形类

- `gui`
- `guimode`
- `mouse`
- `consize`

从接口分类可以看出，Sirpair OS 已从“教学用系统调用集合”扩展为更接近轻量实验操作系统的平台接口集合。

### 11. 用户空间架构

#### 11.1 启动链路

系统的用户态启动流程为：

1. 内核创建第一个进程
2. 执行 `/init`
3. `/init` 保证 `console` 和 `null` 设备存在
4. `/init` 拉起 `/bin/sh`
5. shell 接收命令并 fork/exec 其他程序

#### 11.2 用户态生态

用户空间大致分为四类程序：

- 基础命令
- 系统监控命令
- 网络工具
- 图形和高级程序

此外，系统还支持：

- Lua 脚本解释
- TinyCC 在线编译
- beanstalkd 服务运行
- ELF 分析工具

这意味着用户空间已经形成一个较完整的小型工具链生态。

### 12. 第三方组件移植架构

Sirpair OS 对外部程序的移植不是简单复制源码，而是通过 shim 层和兼容头进行适配。

典型例子：

- `user/lua/`：适配 Lua 的标准库依赖
- `user/tcc/`：适配 TinyCC 所需的用户态接口和 printf 行为
- `user/beanstalkd/`：适配 beanstalkd 的 libc、时间、网络与格式化输出

这种方式的本质是：

- 保留原项目主体逻辑
- 在 Sirpair OS 中补出缺失运行时
- 通过少量兼容层把程序移植进来

这是一种很典型的实验 OS 用户态移植策略。

### 13. 工程与验证架构

项目的工程化能力主要体现在两点：

#### 13.1 构建自动化

`docker-build.sh` 把：

- 构建
- 运行
- 测试
- 校验
- 调试

集中到一个统一入口里，降低了开发和验证成本。

#### 13.2 回归测试

项目包含大量自动化回归脚本，覆盖：

- 命令行为
- 网络链路
- 图形桌面
- shell 行编辑
- framebuffer 性能
- Lua/TCC/beanstalkd

这使 Sirpair OS 从“代码能运行”提升到“功能可持续验证”的阶段。

### 14. 技术特点总结

从架构角度看，Sirpair OS 具有以下鲜明特点：

- 代码简洁可读结构
- 面向真实硬件增强 USB 和网卡能力
- 采用分层设计连接驱动、子系统和用户态
- 在网络上使用“底层自研 + 上层复用”的混合方案
- 在图形上使用“内核 framebuffer + 用户态绘制”的轻量方案
- 通过 shim 层引入 Lua、TinyCC、beanstalkd 等外部组件
- 用自动化回归保证功能持续可用

### 15. 结论

Sirpair OS 的设计围绕着“可在模拟器和真机上运行的轻量实验操作系统”这一目标做了大量调研和开发，借鉴了xv6/linux/unix等大量优秀的操作思想。

其技术路线可以概括为：

- 启动层保持简单直接
- 内核层延续 linux/xv6 风格
- 驱动层强化真机可用性
- 子系统层重点增强 USB、网络和图形
- 接口层扩展系统调用
- 用户态层通过命令集和第三方移植提升实用性

因此，Sirpair OS 既适合用来学习传统 Unix 风格内核结构，也适合做真实设备驱动、协议栈、图形和用户态移植的综合实验平台。

## 感谢
最后，感谢linux、xv6、unix、lua、microps和beanstalked等开源软件优秀的思想，本项目才能得到顺利的开展。
