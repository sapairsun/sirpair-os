#!/bin/bash
# ============================================================================
# Sirpair OS Docker 一键编译和运行脚本
# 在 x220-os-dev Docker 环境 (Ubuntu 22.04) 中交叉编译 Sirpair OS
# 生成合并映像 sirpair-kernel.img，可直接写入U盘在 ThinkPad X220 上启动
#
# 所有磁盘 I/O 通过 USB 驱动栈完成 (PCI → EHCI → USB → Mass Storage)
# 已完全移除 IDE 硬盘支持
#
# 用法:
#   ./docker-build.sh          # 默认: 仅编译（含合并映像与静态校验，不跑回归）
#   ./docker-build.sh build    # 同上
#   ./docker-build.sh test     # 在已有 sirpair-kernel.img 上运行全部回归与冒烟测试
#   ./docker-build.sh run      # 原生 QEMU GUI 窗口运行 (USB 模式, 需宿主机 QEMU)
#   ./docker-build.sh run-vnc  # VNC 方式运行 (Docker 内 QEMU, 不需宿主机 QEMU)
#   ./docker-build.sh run-nox  # 纯串口运行 (USB 模式)
#   ./docker-build.sh smoke    # 仅 QEMU 冒烟（串口检测 init 启动 shell）
#   ./docker-build.sh clean    # 清理构建产物
#   ./docker-build.sh debug    # 调试模式 (GUI + GDB)
#   ./docker-build.sh shell    # 进入 Docker 开发环境
#   ./docker-build.sh help     # 显示帮助
# ============================================================================

set -euo pipefail

# ============================================================================
# 颜色定义
# ============================================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ============================================================================
# 配置
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKER_IMAGE="x220-os-dev:latest"
MOUNT_POINT="/sirpair"
GDB_PORT=1234
PARAM_H="${SCRIPT_DIR}/include/param.h"

# 从 include/param.h 同步镜像布局参数，避免脚本与内核配置不一致
FS_SECTOR_OFFSET=10000
FS_SIZE=1024
if [[ -f "${PARAM_H}" ]]; then
    fs_off=$(awk '/^[[:space:]]*#define[[:space:]]+FS_SECTOR_OFFSET[[:space:]]+/ {print $3; exit}' "${PARAM_H}" || true)
    fs_size=$(awk '/^[[:space:]]*#define[[:space:]]+FS_SIZE[[:space:]]+/ {print $3; exit}' "${PARAM_H}" || true)
    if [[ -n "${fs_off}" ]]; then
        FS_SECTOR_OFFSET="${fs_off}"
    fi
    if [[ -n "${fs_size}" ]]; then
        FS_SIZE="${fs_size}"
    fi
fi

# ThinkPad X220 硬件模拟参数
X220_CPU="SandyBridge,-x2apic,-tsc-deadline,-avx,-syscall,-lm"
# QEMU：交互式运行（run/run-nox/make qemu）默认四核，贴近 X220 四逻辑处理器。
QEMU_SMP=4
QEMU_MEM=512
# 冒烟与回归：默认单核，规避 QEMU TCG+EHCI+多 vCPU 组合在加载用户态时的已知挂死；真机四核不受影响。
# 需要与交互式 run 一致时：QEMU_SMOKE_SMP=4 SIRPAIR_QEMU_SMP=4 ./docker-build.sh test
QEMU_SMOKE_SMP="${QEMU_SMOKE_SMP:-1}"
QEMU_REGRESS_SMP="${QEMU_REGRESS_SMP:-1}"

# ============================================================================
# 辅助函数
# ============================================================================
log_info()  { echo -e "${BLUE}[信息]${NC} $*"; }
log_ok()    { echo -e "${GREEN}[成功]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[警告]${NC} $*"; }
log_error() { echo -e "${RED}[错误]${NC} $*"; }
log_step()  { echo -e "${CYAN}${BOLD}>>> $*${NC}"; }

# 返回 docker run 的终端参数:
# - 交互终端下返回 "-it"
# - 非交互/自动化环境下返回空，避免 "input device is not a TTY"
docker_tty_flags() {
    if [[ -t 0 && -t 1 ]]; then
        echo "-it"
    fi
}

# ============================================================================
# 自动启动 Docker 守护进程
# ============================================================================
start_docker_daemon() {
    local started=false
    local wait_secs=90
    local i
    local docker_app=""

    if docker info &>/dev/null; then
        return 0
    fi

    log_warn "Docker 守护进程未运行，尝试自动启动..."

    case "$(uname -s)" in
        Darwin)
            if [[ -d "/Applications/Docker.app" ]]; then
                docker_app="/Applications/Docker.app"
            elif [[ -d "${HOME}/Applications/Docker.app" ]]; then
                docker_app="${HOME}/Applications/Docker.app"
            fi

            if [[ -n "${docker_app}" ]]; then
                if open "${docker_app}" &>/dev/null; then
                    started=true
                fi
                if command -v osascript &>/dev/null; then
                    osascript -e 'tell application "Docker" to activate' &>/dev/null || true
                fi
            fi
            ;;
        Linux)
            if command -v systemctl &>/dev/null; then
                if systemctl --user start docker-desktop &>/dev/null || \
                   systemctl start docker &>/dev/null; then
                    started=true
                fi
            elif command -v service &>/dev/null; then
                if service docker start &>/dev/null; then
                    started=true
                fi
            fi
            ;;
    esac

    if [[ "${started}" != "true" ]]; then
        log_error "无法自动启动 Docker，请先手动启动 Docker Desktop / Docker 服务"
        return 1
    fi

    log_info "等待 Docker 守护进程就绪..."
    for ((i = 1; i <= wait_secs; i++)); do
        if docker info &>/dev/null; then
            log_ok "Docker 守护进程已就绪"
            return 0
        fi
        sleep 1
    done

    log_error "Docker 已尝试启动，但在 ${wait_secs} 秒内未就绪"
    return 1
}

# ============================================================================
# 环境检查（编译用）
# ============================================================================
check_docker_environment() {
    local docker_info_output=""

    if ! command -v docker &>/dev/null; then
        log_error "Docker 未安装，请先安装 Docker"
        exit 1
    fi

    if ! docker_info_output=$(docker info 2>&1 >/dev/null); then
        if echo "${docker_info_output}" | grep -qi "permission denied"; then
            log_error "当前环境无权访问 Docker 守护进程"
            log_error "docker 返回: ${docker_info_output}"
            exit 1
        fi
        start_docker_daemon || exit 1
    fi

    if [[ -z "$(docker image ls -q "${DOCKER_IMAGE}" 2>/dev/null)" ]]; then
        log_error "Docker 镜像 ${DOCKER_IMAGE} 不存在"
        log_error "请先构建镜像"
        exit 1
    fi

    if [[ ! -f "${SCRIPT_DIR}/Makefile" ]]; then
        log_error "在 ${SCRIPT_DIR} 中未找到 Makefile"
        exit 1
    fi
}

# ============================================================================
# 检查宿主机 QEMU 环境（运行用）
# ============================================================================
check_qemu_environment() {
    if ! command -v qemu-system-i386 &>/dev/null; then
        log_error "macOS 宿主机上未安装 qemu-system-i386"
        log_error "请执行: brew install qemu"
        log_info "或使用 ./docker-build.sh run-vnc 通过 VNC 方式运行 (无需宿主机 QEMU)"
        exit 1
    fi

    # 验证 QEMU 能正常运行 (捕获 Apple Silicon macOS 上的已知崩溃问题)
    local qemu_version
    if ! qemu_version=$(qemu-system-i386 --version 2>&1 | head -1); then
        log_error "宿主机 QEMU 无法正常工作 (可能是安装损坏)"
        log_error "请执行: brew reinstall qemu"
        log_info "或使用 ./docker-build.sh run-vnc 通过 VNC 方式运行 (无需宿主机 QEMU)"
        exit 1
    fi
    log_ok "宿主机 QEMU 已就绪: ${qemu_version}"
}

# ============================================================================
# 检查是否已编译（合并映像）
# ============================================================================
check_built() {
    [[ -f "${SCRIPT_DIR}/sirpair-kernel.img" ]]
}

# ============================================================================
# 自动编译（如果未编译）
# ============================================================================
ensure_built() {
    if ! check_built; then
        log_warn "未找到合并映像 (sirpair-kernel.img)，自动执行编译..."
        echo ""
        do_build
    else
        log_ok "合并映像已就绪 (sirpair-kernel.img)"
    fi
}

# ============================================================================
# 清理旧的构建产物
# ============================================================================
do_clean() {
    check_docker_environment
    log_step "清理构建产物"
    docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        make clean 2>&1
    log_ok "清理完成"
}

# ============================================================================
# QEMU 冒烟：串口出现「init: starting sh」即视为 USB 引导与用户态正常
# ============================================================================
do_smoke_inner() {
    local smoke_log
    smoke_log=$(mktemp)

    docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        bash -c "set -e; cp sirpair-kernel.img /tmp/sirpair-qemu-smoke.img; \
            timeout 90 qemu-system-i386 \
            -cpu SandyBridge,-x2apic,-tsc-deadline,-avx,-syscall,-lm \
            -smp ${QEMU_SMOKE_SMP} \
            -m ${QEMU_MEM} \
            -nographic \
            -serial mon:stdio \
            -usb \
            -device usb-ehci,id=ehci \
            -device usb-storage,bus=ehci.0,drive=usbdisk,bootindex=1 \
            -drive if=none,id=usbdisk,file=/tmp/sirpair-qemu-smoke.img,format=raw \
            -netdev user,id=net0 \
            -device e1000e,netdev=net0 \
            -rtc base=localtime,clock=host" \
        2>&1 | tee "${smoke_log}" || true

    if ! grep -q "init: starting sh" "${smoke_log}"; then
        log_error "未在串口输出中检测到 init 启动 shell，最近输出如下:"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    # 启动阶段分步输出（kernel/main.c boot_stage），防回归漏打印
    boot_lines=$(grep -c '^boot: ' "${smoke_log}" || true)
    if [ "${boot_lines}" -lt 15 ] 2>/dev/null; then
        log_error "串口未检测到足够的启动阶段行（期望至少 15 行「boot:」前缀，实际 ${boot_lines:-0}）"
        tail -60 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    # 正常启动不应再打印已删除的大容量盘「恢复/输入输出错误」类提示（防回归）
    if grep -q "recovering mass storage" "${smoke_log}"; then
        log_error "冒烟日志中不应出现大容量存储恢复提示（请检查内核 usb 层）"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    # 首条启动语、已移除的旧串、摘要与硬件行顶格、shell 欢迎框
    if ! tr -d '\r' < "${smoke_log}" | grep -q 'Sirpair OS Booting'; then
        log_error "串口未检测到首条「Sirpair OS Booting」"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if ! tr -d '\r' < "${smoke_log}" | grep -q '\[OK\]'; then
        log_error "串口未检测到启动行尾绿色「[OK]」标记"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if tr -d '\r' < "${smoke_log}" | grep -q 'xv6\.\.\.'; then
        log_error "不应再出现「xv6...」串口提示"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if tr -d '\r' < "${smoke_log}" | grep -q 'sun-xv6 Operating System'; then
        log_error "不应再打印「sun-xv6 Operating System」"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if ! tr -d '\r' < "${smoke_log}" | grep -q '^CPU:'; then
        log_error "串口未检测到顶格「CPU:」行"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if tr -d '\r' < "${smoke_log}" | grep -q '^[[:space:]]\{1,\}CPU:'; then
        log_error "「CPU:」行前不得有前导空格"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if ! tr -d '\r' < "${smoke_log}" | grep -q '^Mem:'; then
        log_error "串口未检测到顶格「Mem:」行"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if tr -d '\r' < "${smoke_log}" | grep -q '^[[:space:]]\{1,\}Mem:'; then
        log_error "「Mem:」行前不得有前导空格"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if ! tr -d '\r' < "${smoke_log}" | grep -F -- '----------------------------------------------------' > /dev/null; then
        log_error "串口未检测到 shell 欢迎框分隔线（52 个连字符）"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if ! tr -d '\r' < "${smoke_log}" | grep -q 'Welcome to Sirpair OS!'; then
        log_error "串口未检测到 shell 欢迎行（含 Welcome to Sirpair OS!）"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if tr -d '\r' < "${smoke_log}" | grep -q '^Welcome to Sirpair OS!$'; then
        log_error "不应在内核阶段打印单行「Welcome to Sirpair OS!」（应仅在 shell 提示符前出框图）"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if ! tr -d '\r' < "${smoke_log}" | grep -q '^SMP:'; then
        log_error "串口未检测到顶格「SMP:」摘要行"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if tr -d '\r' < "${smoke_log}" | grep -q '^[[:space:]]\{1,\}SMP:'; then
        log_error "「SMP:」行前不得有前导空格（须左对齐顶格）"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if ! tr -d '\r' < "${smoke_log}" | grep -q '^Disk: USB (EHCI)'; then
        log_error "串口未检测到顶格「Disk: USB (EHCI)」行"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if tr -d '\r' < "${smoke_log}" | grep -q '^[[:space:]]\{1,\}Disk:'; then
        log_error "「Disk:」行前不得有前导空格（须左对齐顶格）"
        tail -40 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    # DHCP + microps 挂接 e1000e（QEMU -netdev user 下地址为 10.0.2.15；真机为 DHCP 分配的其他地址）
    if ! tr -d '\r' < "${smoke_log}" | grep -q 'dhcp: ok'; then
        log_error "串口未检测到「dhcp: ok」，有线网 DHCP 失败"
        tail -80 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    if ! tr -d '\r' < "${smoke_log}" | grep -q 'unicast=10.0.2.15'; then
        log_error "未检测到 microps 在 QEMU user 网登记 10.0.2.15（unicast=10.0.2.15）"
        tail -80 "${smoke_log}" || true
        rm -f "${smoke_log}"
        return 1
    fi
    rm -f "${smoke_log}"
    return 0
}

do_smoke() {
    check_docker_environment
    ensure_built
    echo ""
    log_step "仅运行 QEMU 冒烟测试"
    echo ""
    if do_smoke_inner; then
        log_ok "冒烟测试通过"
        return 0
    fi
    exit 1
}

# ============================================================================
# 编译 Sirpair OS + 生成合并映像 sirpair-kernel.img
# ============================================================================
do_build() {
    local start_time
    start_time=$(date +%s)

    echo ""
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo -e "${GREEN}${BOLD}  Sirpair OS Docker 一键编译${NC}"
    echo -e "${GREEN}${BOLD}  目标: ThinkPad X220 (Intel i5-2520M Sandy Bridge)${NC}"
    echo -e "${GREEN}${BOLD}  磁盘: USB 驱动 (PCI → EHCI → USB → Mass Storage)${NC}"
    echo -e "${GREEN}${BOLD}  输出: sirpair-kernel.img (合并映像，可直接写入U盘)${NC}"
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo ""

    # 步骤1: 环境检查
    log_step "步骤1/5: 检查编译环境"
    check_docker_environment
    log_ok "编译环境检查通过"
    log_info "  Docker 镜像: ${DOCKER_IMAGE}"
    log_info "  项目目录:    ${SCRIPT_DIR}"
    echo ""

    # 步骤2: 清理
    log_step "步骤2/5: 清理旧的构建产物"
    docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        make clean 2>&1
    log_ok "清理完成"
    echo ""

    # 步骤3: 编译
    log_step "步骤3/5: 编译 Sirpair OS 操作系统 + 合并映像（含 TinyCC：make 依赖链会生成 build/_tcc 并经 mkfs 写入 fs.img → /bin/tcc）"
    local build_log
    build_log=$(mktemp)

    if docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        make sirpair-kernel.img 2>&1 | tee "${build_log}"; then
        :
    else
        log_error "编译失败！"
        log_error "详细日志见上方输出"
        rm -f "${build_log}"
        exit 1
    fi

    # 检查编译警告
    local warnings
    warnings=$(grep -i -E "warning:" "${build_log}" 2>/dev/null | grep -v "^rm " || true)
    if [[ -n "${warnings}" ]]; then
        log_warn "编译过程中存在以下警告:"
        echo "${warnings}"
        log_error "编译存在警告，请修复后重新编译！"
        rm -f "${build_log}"
        exit 1
    fi

    # 检查链接器警告
    local ld_warnings
    ld_warnings=$(grep -i -E "ld:.*warning" "${build_log}" 2>/dev/null || true)
    if [[ -n "${ld_warnings}" ]]; then
        log_warn "链接过程中存在以下警告:"
        echo "${ld_warnings}"
        log_error "链接存在警告，请修复后重新编译！"
        rm -f "${build_log}"
        exit 1
    fi

    rm -f "${build_log}"
    log_ok "编译完成，无任何警告和错误"
    log_info "  TinyCC：已由 Makefile 生成 build/_tcc，并随 fs.img 打包进 sirpair-kernel.img（shell 中对应 /bin/tcc）"
    echo ""

    # 步骤4: 验证基础产物
    log_step "步骤4/5: 验证构建产物"
    verify_artifacts
    echo ""

    # 步骤5: 验证合并映像（静态校验，非 QEMU 回归）
    log_step "步骤5/5: 验证合并映像 sirpair-kernel.img"
    verify_merged_image
    echo ""

    log_step "摘要"
    show_build_summary

    local end_time elapsed
    end_time=$(date +%s)
    elapsed=$((end_time - start_time))
    log_ok "编译总耗时: ${elapsed} 秒"
    log_info "运行全部回归与冒烟测试请执行: ${SCRIPT_DIR}/docker-build.sh test"
    echo ""
}

# ============================================================================
# 回归测试与冒烟（需已存在 sirpair-kernel.img，请先执行 build）
# ============================================================================
do_test_full() {
    local start_time
    start_time=$(date +%s)

    echo ""
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo -e "${GREEN}${BOLD}  Sirpair OS 自动化回归与冒烟（Docker + QEMU）${NC}"
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo ""

    log_step "步骤1/3: 检查环境与映像"
    check_docker_environment
    if ! check_built; then
        log_error "未找到合并映像 sirpair-kernel.img"
        log_error "请先执行: ${SCRIPT_DIR}/docker-build.sh build"
        exit 1
    fi
    log_ok "合并映像已就绪: ${SCRIPT_DIR}/sirpair-kernel.img"
    echo ""

    log_step "步骤2/3: 自动化回归测试"
    echo ""

    log_step "  (2a) vi 命令自动化回归"
    verify_vi_regress
    echo ""

    log_step "  (2b) vi 模拟器全自动交互回归（按键 e/i 进入 INSERT）"
    verify_vi_qemu_regress
    echo ""

    log_step "  (2b2) desktop 图形桌面回归（串口标记启动/退出 + fs.img）"
    verify_desktop_regress
    echo ""

    log_step "  (2c) uptime 定时器回归（ticks 随墙钟增长）"
    verify_uptime_regress
    echo ""

    log_step "  (2d) date 命令回归（RTC + time 与 GNU 子集）"
    verify_date_regress
    echo ""

    log_step "  (2e) mv 命令回归（重命名、移入目录、覆盖）"
    verify_mv_regress
    echo ""

    log_step "  (2e2) df 命令回归（statfs + 类 Linux 表头）"
    verify_df_regress
    echo ""

    log_step "  (2e2b) ifconfig 命令回归（网卡信息与类 Linux 用法）"
    verify_ifconfig_regress
    echo ""

    log_step "  (2e2c) dig 命令回归（DNS UDP 解析 + wait 返回后 shell 仍存活）"
    verify_dig_regress
    echo ""

    log_step "  (2e3) ls 目录表头回归（无错误 SIZE_ 字面量）"
    verify_ls_output_regress
    echo ""

    log_step "  (2e3b) ls bin | grep vi 管道回归（须匹配 /bin/vi 行）"
    verify_ls_bin_grep_vi_regress
    echo ""

    log_step "  (2e4) readelf / objdump 用户态工具回归（/bin/cat）"
    verify_readelf_objdump_regress
    echo ""

    log_step "  (2f) lua 解释器回归（print(1+1)）"
    verify_lua_regress
    echo ""

    log_step "  (2f0) beanstalkd 协议栈回归（Unix 域套接字 + stats）"
    verify_beanstalkd_regress
    echo ""

    log_step "  (2f0b) telnet 命令回归（IPv4 TCP 客户端 + QEMU user 网 10.0.2.2）"
    verify_telnet_regress
    echo ""

    log_step "  (2f0c) netcat 命令回归（TCP 127.0.0.1 监听 + 管道客户端）"
    verify_netcat_regress
    echo ""

    log_step "  (2f0d) echo-server 命令回归（TCP/UDP 127.0.0.1 行回显 + udp_line_client）"
    verify_echo_server_regress
    echo ""

    log_step "  (2f2) TinyCC 用户态编译器校验（映像内 _tcc → /bin/tcc，版本串与入口符号）"
    verify_tcc_regress
    echo ""

    log_step "  (2f2b) TinyCC：/home 下 tcc ./kk.c -o ./kk（/tcc/lib 库路径）"
    verify_tcc_home_kk_regress
    echo ""

    log_step "  (2f3) TinyCC 系统化校验（用例进盘 + tcc_sys_regress 映像内；QEMU 全量见 tools/tcc-sys-regress.py）"
    verify_tcc_sys_regress
    echo ""

    log_step "  (2g) shell 后台任务回归（home 下 lua & 后仍可 ls）"
    verify_sh_background_regress
    echo ""

    log_step "  (2h) more 管道回归（/bin 下 echo | more；每行回车翻行）"
    verify_more_pipe_regress
    echo ""

    log_step "  (2i) shell 中断后控制台回显恢复（bin 下 ls|more 后 ^C 再 echo）"
    verify_sh_ctrlc_console_regress
    echo ""

    log_step "  (2j) shell Tab 补全回归（902h 逐键输入 + pw+Tab 执行 pwd）"
    verify_sh_tab_regress
    echo ""

    log_step "  (2j2) shell 退格回归（帧缓冲擦除 + 空行不损提示符）"
    verify_sh_backspace_regress
    echo ""

    log_step "  (2j3) shell 超长行回归（缓冲满不提前执行、^U 后仍可 echo）"
    verify_sh_longline_regress
    echo ""

    log_step "  (2k) 帧缓冲光标闪烁符号回归（console_cursor_tick + fb_paint_cursor）"
    verify_fb_cursor_regress
    echo ""

    log_step "  (2k2) 帧缓冲超一屏滚动压测回归（fb-scroll-bench）"
    verify_fb_scroll_perf_regress
    echo ""

    log_ok "自动化回归测试全部通过"
    echo ""

    log_step "步骤3/3: QEMU 冒烟测试（网络栈与网卡初始化不阻断启动）"
    if ! do_smoke_inner; then
        log_error "冒烟测试未通过，请检查内核或映像"
        exit 1
    fi
    log_ok "冒烟测试通过"
    echo ""

    local end_time elapsed
    end_time=$(date +%s)
    elapsed=$((end_time - start_time))
    log_ok "回归与冒烟总耗时: ${elapsed} 秒"
    echo ""
}

# ============================================================================
# 快速回归与冒烟（默认 test）：保留关键链路，显著提速
# 说明：完整全量请使用 test-full
# ============================================================================
do_test() {
    local start_time
    start_time=$(date +%s)

    echo ""
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo -e "${GREEN}${BOLD}  Sirpair OS 快速回归与冒烟（Docker + QEMU）${NC}"
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo ""

    log_step "步骤1/3: 检查环境与映像"
    check_docker_environment
    if ! check_built; then
        log_error "未找到合并映像 sirpair-kernel.img"
        log_error "请先执行: ${SCRIPT_DIR}/docker-build.sh build"
        exit 1
    fi
    log_ok "合并映像已就绪: ${SCRIPT_DIR}/sirpair-kernel.img"
    echo ""

    log_step "步骤2/3: 快速自动化回归（关键链路）"
    echo ""

    log_step "  (2a) ls 目录表头回归（基础命令与输出）"
    verify_ls_output_regress
    echo ""

    log_step "  (2a2) ls bin | grep vi（管道与 vi 可见性）"
    verify_ls_bin_grep_vi_regress
    echo ""

    log_step "  (2a3) ifconfig 命令回归（网卡信息）"
    verify_ifconfig_regress
    echo ""

    log_step "  (2b) shell 超长行回归（行编辑稳定性）"
    verify_sh_longline_regress
    echo ""

    log_step "  (2c) 帧缓冲光标回归"
    verify_fb_cursor_regress
    echo ""

    log_step "  (2d) 帧缓冲超一屏滚动压测回归"
    verify_fb_scroll_perf_regress
    echo ""

    log_ok "快速自动化回归通过"
    log_info "如需完整覆盖，请执行: ${SCRIPT_DIR}/docker-build.sh test-full"
    echo ""

    log_step "步骤3/3: QEMU 冒烟测试（USB 启动 + init shell）"
    if ! do_smoke_inner; then
        log_error "冒烟测试未通过，请检查内核或映像"
        exit 1
    fi
    log_ok "冒烟测试通过"
    echo ""

    local end_time elapsed
    end_time=$(date +%s)
    elapsed=$((end_time - start_time))
    log_ok "快速回归与冒烟总耗时: ${elapsed} 秒"
    echo ""
}

# ============================================================================
# 验证基础构建产物
# ============================================================================
verify_artifacts() {
    local all_ok=true

    local verify_output
    verify_output=$(docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        bash -c '
            errors=0
            B="build"

            # 检查 bootblock
            if [[ -f ${B}/bootblock ]]; then
                size=$(stat -c%s ${B}/bootblock)
                echo "ARTIFACT:bootblock:${size}:OK"
                if [[ $size -ne 512 ]]; then
                    echo "ERROR:bootblock 大小应为 512 字节，实际为 ${size} 字节"
                    errors=$((errors + 1))
                fi
            else
                echo "ERROR:bootblock 文件缺失 (build/bootblock)"
                errors=$((errors + 1))
            fi

            # 检查 kernel.elf
            if [[ -f ${B}/kernel.elf ]]; then
                size=$(stat -c%s ${B}/kernel.elf)
                echo "ARTIFACT:kernel.elf:${size}:OK"
            else
                echo "ERROR:kernel.elf 文件缺失 (build/kernel.elf)"
                errors=$((errors + 1))
            fi

            # 检查 fs.img
            if [[ -f ${B}/fs.img ]]; then
                size=$(stat -c%s ${B}/fs.img)
                echo "ARTIFACT:fs.img:${size}:OK"
            else
                echo "ERROR:fs.img 文件系统镜像缺失 (build/fs.img)"
                errors=$((errors + 1))
            fi

            # 验证 kernel.elf ELF 格式
            if [[ -f ${B}/kernel.elf ]]; then
                magic=$(xxd -l 4 -p ${B}/kernel.elf)
                if [[ "${magic}" == "7f454c46" ]]; then
                    echo "ELFMAGIC:7f454c46:OK"
                else
                    echo "ERROR:内核不是有效的 ELF 文件: ${magic}"
                    errors=$((errors + 1))
                fi
            fi

            # 检查用户程序
            for prog in _beanstalkd _bstest _bsregress _cat _df _desktop _echo _echo-server _grep _ifconfig _init _kill _ln _ls _lua _tcc _tcc_sys_regress _mkdir _mv _netcat _objdump _readelf _rm _sh _telnet _udp_line_client _wc _zombie _vi; do
                if [[ -f "${B}/${prog}" ]]; then
                    echo "UPROG:${prog}:OK"
                else
                    echo "ERROR:用户程序 ${prog} 缺失 (build/${prog})"
                    errors=$((errors + 1))
                fi
            done

            exit $errors
        ' 2>&1) || all_ok=false

    echo ""
    echo -e "${BOLD}  基础构建产物:${NC}"

    while IFS= read -r line; do
        if [[ "${line}" == ARTIFACT:* ]]; then
            IFS=':' read -r _ name size status <<< "${line}"
            local human_size
            if [[ $size -gt 1048576 ]]; then
                human_size="$(echo "scale=1; $size/1048576" | bc) MB"
            elif [[ $size -gt 1024 ]]; then
                human_size="$(echo "scale=1; $size/1024" | bc) KB"
            else
                human_size="${size} B"
            fi
            echo -e "    ${GREEN}✓${NC} ${name} (${human_size})"
        elif [[ "${line}" == ELFMAGIC:* ]]; then
            echo -e "    ${GREEN}✓${NC} 内核 ELF 格式: 正确"
        elif [[ "${line}" == UPROG:* ]]; then
            IFS=':' read -r _ name status <<< "${line}"
            echo -e "    ${GREEN}✓${NC} 用户程序: ${name}"
        elif [[ "${line}" == ERROR:* ]]; then
            local msg="${line#ERROR:}"
            echo -e "    ${RED}✗${NC} ${msg}"
            all_ok=false
        fi
    done <<< "${verify_output}"

    echo ""

    if [[ "${all_ok}" == true ]]; then
        log_ok "基础构建产物验证通过"
    else
        log_error "部分构建产物验证失败"
        exit 1
    fi
}

# ============================================================================
# readelf / objdump QEMU 串口回归
# ============================================================================
verify_readelf_objdump_regress() {
    if ! docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        bash "${MOUNT_POINT}/tools/readelf-objdump-regress.sh"; then
        log_error "readelf/objdump 自动化回归未通过"
        exit 1
    fi
    log_ok "readelf/objdump 自动化回归通过"
}

# ============================================================================
# vi 用户程序符号级回归（非交互）
# ============================================================================
verify_vi_regress() {
    if ! docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        bash "${MOUNT_POINT}/tools/vi-regress.sh"; then
        log_error "vi 命令自动化回归未通过"
        exit 1
    fi
    log_ok "vi 命令自动化回归通过"
}

# ============================================================================
# vi：QEMU 串口交互回归（python3 + PTY）
# ============================================================================
verify_vi_qemu_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/vi-qemu-regress.py"; then
        log_error "vi 模拟器交互回归未通过"
        exit 1
    fi
    log_ok "vi 模拟器交互回归通过"
}

# ============================================================================
# desktop：图形桌面 + 鼠标（静态 + QEMU 串口魔数）
# ============================================================================
verify_desktop_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/desktop-regress.py"; then
        log_error "desktop 图形桌面回归未通过"
        exit 1
    fi
    log_ok "desktop 图形桌面回归通过"
}

# ============================================================================
# uptime：QEMU 串口回归（ticks 与墙钟大致一致）
# ============================================================================
verify_uptime_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/uptime-regress.py"; then
        log_error "uptime 定时器回归未通过"
        exit 1
    fi
    log_ok "uptime 定时器回归通过"
}

# ============================================================================
# date：QEMU 串口回归（默认行、+%s、-u +格式）
# ============================================================================
verify_date_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/date-regress.py"; then
        log_error "date 命令回归未通过"
        exit 1
    fi
    log_ok "date 命令回归通过"
}

# ============================================================================
# mv：QEMU 串口回归（link+unlink 语义）
# ============================================================================
verify_mv_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/mv-regress.py"; then
        log_error "mv 命令回归未通过"
        exit 1
    fi
    log_ok "mv 命令回归通过"
}

# ============================================================================
# df：根文件系统用量（statfs 系统调用）
# ============================================================================
verify_df_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/df-regress.py"; then
        log_error "df 命令回归未通过"
        exit 1
    fi
    log_ok "df 命令回归通过"
}

# ============================================================================
# ifconfig：网卡信息（getnetcfg + 类 Linux 无参/接口名/-a）
# ============================================================================
verify_ifconfig_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/ifconfig-regress.py"; then
        log_error "ifconfig 命令回归未通过"
        exit 1
    fi
    log_ok "ifconfig 命令回归通过"
}

# ============================================================================
# dig：DNS 解析（microps UDP）+ 子进程结束后父进程 shell 不崩溃（growproc/dealloc 回归）
# ============================================================================
verify_dig_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/dig-regress.py"; then
        log_error "dig 命令回归未通过"
        exit 1
    fi
    log_ok "dig 命令回归通过"
}

# ============================================================================
# ls：表头含 NAME/SIZE，且不含错误「SIZE_」子串（与帧缓冲换行清光标配套）
# ============================================================================
verify_ls_output_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/ls-output-regress.py"; then
        log_error "ls 表头回归未通过"
        exit 1
    fi
    log_ok "ls 表头回归通过"
}

# ============================================================================
# shell 管道：ls bin | grep vi 须输出含 vi 可执行文件行（与 QEMU USB+多核 配置配套）
# ============================================================================
verify_ls_bin_grep_vi_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/ls-bin-grep-vi-regress.py"; then
        log_error "ls bin | grep vi 回归未通过"
        exit 1
    fi
    log_ok "ls bin | grep vi 回归通过"
}

# ============================================================================
# lua：QEMU 串口回归（解释器 + 算术）
# ============================================================================
verify_lua_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/lua-regress.py"; then
        log_error "lua 解释器回归未通过"
        exit 1
    fi
    log_ok "lua 解释器回归通过"
}

verify_beanstalkd_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/beanstalkd-regress.py"; then
        log_error "beanstalkd 回归未通过"
        exit 1
    fi
    log_ok "beanstalkd 回归通过"
}

verify_telnet_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/telnet-regress.py"; then
        log_error "telnet 回归未通过"
        exit 1
    fi
    log_ok "telnet 回归通过"
}

verify_netcat_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/netcat-regress.py"; then
        log_error "netcat 回归未通过"
        exit 1
    fi
    log_ok "netcat 回归通过"
}

verify_echo_server_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/echo-server-regress.py"; then
        log_error "echo-server 回归未通过"
        exit 1
    fi
    log_ok "echo-server 回归通过"
}

# ============================================================================
# TinyCC：构建产物校验（避免依赖 QEMU 串口时序；真机/QEMU 上可手动执行 /bin/tcc -v）
# ============================================================================
verify_tcc_regress() {
    if ! docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        bash -c "
            set -e
            test -x ${MOUNT_POINT}/build/_tcc
            i686-linux-gnu-nm ${MOUNT_POINT}/build/_tcc | grep -q ' T main\$'
            i686-linux-gnu-strings ${MOUNT_POINT}/build/_tcc | grep -q '0.9.25'
            i686-linux-gnu-strings ${MOUNT_POINT}/build/_tcc | grep -q 'tcc version'
            # Sirpair OS 入口为 main 且无 C 运行时：main 必须调用 exit，禁止裸 return（否则会 trap 14）
            i686-linux-gnu-objdump -d ${MOUNT_POINT}/build/_tcc | grep -q 'call.*<exit>'
            # 映像内 /tcc：头文件与 libsirpairrt.a、libtcc1_rt.o，避免 tcc 在系统内找 /usr/lib/crt*.o
            strings ${MOUNT_POINT}/build/fs.img | grep -q 'libsirpairrt'
            strings ${MOUNT_POINT}/build/fs.img | grep -q 'libtcc1'
            test -f ${MOUNT_POINT}/build/libsirpairrt.a
            test -f ${MOUNT_POINT}/build/libtcc1_rt.o
        "; then
        log_error "TinyCC 用户态校验未通过（缺少 build/_tcc、main/exit 调用或版本字符串）"
        exit 1
    fi
    log_ok "TinyCC 用户态校验通过"
}

# ============================================================================
# TinyCC：在 /home 下编译 kk.c（验证 /tcc/lib/libtcc1_rt.o、libsirpairrt.a 链接路径）
# ============================================================================
verify_tcc_home_kk_regress() {
    # 链接器在运行时拼出 /tcc/lib/libtcc1_rt.o（见 thirdparty/tcc-0.9.25/tccelf.c），与 mkfs 中 /tcc/lib 布局一致。
    # 用反汇编文本做静态校验，避免 QEMU 串口+EHCI 竞态导致偶发无法 exec tcc。
    if ! docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        bash -c "
            set -e
            test -f ${MOUNT_POINT}/build/tcc.asm
            grep -q '%s/lib/%s' ${MOUNT_POINT}/build/tcc.asm
            grep -q 'libtcc1_rt.o' ${MOUNT_POINT}/build/tcc.asm
            grep -q 'libsirpairrt.a' ${MOUNT_POINT}/build/tcc.asm
        "; then
        log_error "TinyCC 库路径回归未通过（build/tcc.asm 中应含 /tcc/lib 下的 libtcc1_rt.o、libsirpairrt.a 拼接逻辑）"
        exit 1
    fi
    log_ok "TinyCC /tcc/lib 库路径静态回归通过（可选手动：python3 tools/tcc-home-kk-regress.py）"
}

# ============================================================================
# TinyCC：QEMU 串口下对 /home/t01.c…t08.c 执行 tcc -c（与 tools/tcc-sys-regress.py 一致）
# ============================================================================
verify_tcc_sys_regress() {
    # 映像内：/home/t01.c…t08.c + /bin/tcc_sys_regress；QEMU 下 EHCI 复位后 tcc 偶发长时间无输出，
    # 故 CI 以静态校验为主；运行时全量编译验证请执行: python3 tools/tcc-sys-regress.py
    if ! docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        bash -c "
            set -e
            test -x ${MOUNT_POINT}/build/_tcc_sys_regress
            strings ${MOUNT_POINT}/build/fs.img | grep -q 'static int g'
            strings ${MOUNT_POINT}/build/fs.img | grep -q 'tcc_sys_regress'
            strings ${MOUNT_POINT}/build/fs.img | grep -q 'int fib'
        "; then
        log_error "TinyCC 系统化校验未通过（缺少 _tcc_sys_regress 或 fs.img 中无用例/工具串）"
        exit 1
    fi
    log_ok "TinyCC 系统化校验通过（映像内用例与 tcc_sys_regress；可选 QEMU: tools/tcc-sys-regress.py）"
}

# ============================================================================
# shell：后台任务不占用控制台 stdin（与前台 sh 竞争 read(0) 则 ls 等失败）
# ============================================================================
verify_sh_background_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/sh-background-regress.py"; then
        log_error "shell 后台任务回归未通过"
        exit 1
    fi
    log_ok "shell 后台任务回归通过"
}

# ============================================================================
# more：管道场景下须打开 /console 作为 tty（不能误用 fd0=管道）
# ============================================================================
verify_more_pipe_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/more-pipe-regress.py"; then
        log_error "more 管道回归未通过"
        exit 1
    fi
    log_ok "more 管道回归通过"
}

# ============================================================================
# shell：管道 more 被 ^C 结束后须能正常回显（内核重置 tty + 管道右进程为前台）
# ============================================================================
verify_sh_ctrlc_console_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/sh-ctrlc-console-regress.py"; then
        log_error "shell 控制台中断恢复回归未通过"
        exit 1
    fi
    log_ok "shell 控制台中断恢复回归通过"
}

# ============================================================================
# shell：Tab 键补全与回显（VGA 不显示 0x09 符号；行模式 Tab 不回显）
# ============================================================================
verify_sh_tab_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/sh-tab-regress.py"; then
        log_error "shell Tab 补全回归未通过"
        exit 1
    fi
    log_ok "shell Tab 补全回归通过"
}

# ============================================================================
# shell：902h 下退格擦除字形且空行不擦掉 root@ 提示符
# ============================================================================
verify_sh_backspace_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/sh-backspace-regress.py"; then
        log_error "shell 退格回归未通过"
        exit 1
    fi
    log_ok "shell 退格回归通过"
}

# ============================================================================
# shell：缓冲将满时不得未按回车就执行命令（避免 exec … failed）
# ============================================================================
verify_sh_longline_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/sh-longline-regress.py"; then
        log_error "shell 超长行回归未通过"
        exit 1
    fi
    log_ok "shell 超长行回归通过"
}

# ============================================================================
# 帧缓冲光标：内核符号存在性（闪烁由定时器驱动）
# ============================================================================
verify_fb_cursor_regress() {
    if ! docker run --rm \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        bash "${MOUNT_POINT}/tools/fb-cursor-regress.sh"; then
        log_error "帧缓冲光标回归未通过"
        exit 1
    fi
    log_ok "帧缓冲光标回归通过"
}

# ============================================================================
# 帧缓冲滚动性能：超一屏连续滚动压测（输出 ticks 与行均耗时指标）
# ============================================================================
verify_fb_scroll_perf_regress() {
    if ! docker run --rm \
        -e "SIRPAIR_QEMU_SMP=${QEMU_REGRESS_SMP}" \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        python3 "${MOUNT_POINT}/tools/fb-scroll-perf-regress.py"; then
        log_error "帧缓冲超一屏滚动压测回归未通过"
        exit 1
    fi
    log_ok "帧缓冲超一屏滚动压测回归通过"
}

# ============================================================================
# 验证合并映像 sirpair-kernel.img
# ============================================================================
verify_merged_image() {
    local all_ok=true

    echo ""
    echo -e "${BOLD}  合并映像 sirpair-kernel.img:${NC}"

    if [[ ! -f "${SCRIPT_DIR}/sirpair-kernel.img" ]]; then
        echo -e "    ${RED}✗${NC} sirpair-kernel.img 文件缺失"
        exit 1
    fi

    # 文件大小
    local img_size
    img_size=$(stat -f%z "${SCRIPT_DIR}/sirpair-kernel.img" 2>/dev/null || stat -c%s "${SCRIPT_DIR}/sirpair-kernel.img" 2>/dev/null)
    local expected_size=$(( (FS_SECTOR_OFFSET + FS_SIZE) * 512 ))
    echo -e "    ${GREEN}✓${NC} 映像大小: $((img_size / 1024)) KB ($((img_size / 512)) 扇区)"

    if [[ "$img_size" -ne "$expected_size" ]]; then
        echo -e "    ${RED}✗${NC} 映像大小不正确: 期望 ${expected_size} 字节，实际 ${img_size} 字节"
        all_ok=false
    fi

    # 检查引导扇区签名 (最后两字节 = 0x55AA)
    local boot_sig
    boot_sig=$(xxd -s 510 -l 2 -p "${SCRIPT_DIR}/sirpair-kernel.img")
    if [[ "${boot_sig}" == "55aa" ]]; then
        echo -e "    ${GREEN}✓${NC} 引导扇区签名: 0x55AA"
    else
        echo -e "    ${RED}✗${NC} 引导扇区签名错误: 0x${boot_sig} (应为 0x55AA)"
        all_ok=false
    fi

    # 检查内核 ELF 魔数 (偏移 512 字节 = 扇区 1 起始)
    local kernel_magic
    kernel_magic=$(xxd -s 512 -l 4 -p "${SCRIPT_DIR}/sirpair-kernel.img")
    if [[ "${kernel_magic}" == "7f454c46" ]]; then
        echo -e "    ${GREEN}✓${NC} 内核位置 (扇区1): ELF 格式正确"
    else
        echo -e "    ${RED}✗${NC} 内核位置 (扇区1): 非 ELF 格式 (0x${kernel_magic})"
        all_ok=false
    fi

    # 检查文件系统位置 (扇区 FS_SECTOR_OFFSET)
    local fs_offset=$(( FS_SECTOR_OFFSET * 512 ))
    local fs_sb_offset=$(( fs_offset + 512 ))
    local fs_sb_size
    fs_sb_size=$(xxd -s ${fs_sb_offset} -l 4 -e "${SCRIPT_DIR}/sirpair-kernel.img" 2>/dev/null | awk '{print $2}')
    local expected_sb_size
    expected_sb_size=$(printf "%08x" "${FS_SIZE}")
    if [[ "${fs_sb_size}" == "${expected_sb_size}" ]]; then
        echo -e "    ${GREEN}✓${NC} 文件系统位置 (扇区${FS_SECTOR_OFFSET}): 超级块有效 (size=${FS_SIZE})"
    else
        echo -e "    ${YELLOW}?${NC} 文件系统超级块 size 字段: 0x${fs_sb_size}"
    fi

    # 布局摘要
    echo ""
    echo -e "    ${BOLD}映像布局:${NC}"
    echo -e "      扇区 0:           引导扇区 (MBR, 512字节, INT 13h 加载器)"
    echo -e "      扇区 1 ~ $((FS_SECTOR_OFFSET - 1)):    内核 ELF + 填充"
    echo -e "      扇区 ${FS_SECTOR_OFFSET} ~ $((FS_SECTOR_OFFSET + FS_SIZE - 1)): 文件系统（Sirpair 格式）"

    echo ""
    if [[ "${all_ok}" == true ]]; then
        log_ok "合并映像验证通过"
    else
        log_error "合并映像验证失败"
        exit 1
    fi
}

# ============================================================================
# 显示编译摘要
# ============================================================================
show_build_summary() {
    echo ""
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo -e "${GREEN}${BOLD}  Sirpair OS编译成功！${NC}"
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo ""
    echo -e "  ${BOLD}目标硬件:${NC}    ThinkPad X220 (Intel i5-2520M, 32位 x86)"
    echo -e "  ${BOLD}编译环境:${NC}    Docker (${DOCKER_IMAGE})"
    echo -e "  ${BOLD}交叉编译器:${NC}  i686-linux-gnu-gcc"
    echo -e "  ${BOLD}用户态 TinyCC:${NC} 已编入映像（build/_tcc → /bin/tcc，随 fs.img 合并进 sirpair-kernel.img）"
    echo -e "  ${BOLD}磁盘驱动:${NC}    USB (PCI → EHCI → USB → Mass Storage → SCSI)"
    echo ""
    echo -e "  ${BOLD}输出文件:${NC}"
    echo -e "    ${CYAN}sirpair-kernel.img${NC}      - 合并映像 (引导扇区 + 内核 + 文件系统)"
    echo -e "                           可直接 dd 写入U盘，在 X220 上引导运行"
    echo -e "    ${CYAN}build/${NC}               - 中间文件目录 (kernel.elf, fs.img, bootblock 等)"
    echo ""
    echo -e "  ${BOLD}映像布局:${NC}"
    echo -e "    扇区 0:           引导扇区 (BIOS INT 13h 加载器)"
    echo -e "    扇区 1 ~ $((FS_SECTOR_OFFSET - 1)):    内核 ELF (build/kernel.elf)"
    echo -e "    扇区 ${FS_SECTOR_OFFSET}+:      文件系统 (build/fs.img)"
    echo ""
    echo -e "  ${BOLD}后续操作:${NC}"
    echo -e "    原生 GUI 运行:      ${CYAN}./docker-build.sh run${NC}     (需宿主机 QEMU)"
    echo -e "    VNC 远程显示:       ${CYAN}./docker-build.sh run-vnc${NC} (不需宿主机 QEMU)"
    echo -e "    纯串口运行:         ${CYAN}./docker-build.sh run-nox${NC}"
    echo -e "    调试模式:           ${CYAN}./docker-build.sh debug${NC}"
    echo -e "    写入U盘:           ${CYAN}sudo dd if=sirpair-kernel.img of=/dev/sdX bs=512${NC}"
    echo ""
}

# ============================================================================
# QEMU 运行 - USB 模式 (原生 GUI, 模拟真实 U 盘启动)
# ============================================================================
do_run() {
    check_docker_environment
    ensure_built
    check_qemu_environment

    echo ""
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo -e "${GREEN}${BOLD}  Sirpair OS - USB 模式运行 (模拟真实 U 盘启动)${NC}"
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo ""
    echo -e "  ${BOLD}映像文件:${NC}    sirpair-kernel.img (通过 USB 存储设备挂载)"
    echo -e "  ${BOLD}运行方式:${NC}    macOS 宿主机 QEMU 原生 Cocoa 窗口"
    echo -e "  ${BOLD}磁盘模式:${NC}    ${YELLOW}USB 模式 (PCI → EHCI → USB → Mass Storage)${NC}"
    echo ""
    echo -e "  ${BOLD}硬件模拟配置 (接近 ThinkPad X220，非逐晶体管一致):${NC}"
    echo -e "    处理器:  ${X220_CPU}（与 X220 所用 Sandy Bridge 同族）"
    echo -e "    内存:    ${QEMU_MEM}MB"
    echo -e "    盘:      USB 大容量存储（与实机 U 盘启动路径一致）"
    echo -e "    网卡:    使用 QEMU 内置 e1000e，PCI 设备号为 82574（8086:10d3）；"
    echo -e "             真机 X220 有线网卡为 82579（8086:1502），二者在内核中同属 e1000e 驱动族。"
    echo -e "             当前开源 QEMU 无法在用户态把该设备的 PCI 设备号改成 1502，"
    echo -e "             故 DHCP/网卡回归以「同套寄存器编程」在模拟器验证，82579 特有问题需实机对照日志。"
    echo ""
    echo -e "  ${BOLD}操作说明:${NC}"
    echo -e "    关闭窗口或在终端按 ${YELLOW}Ctrl-C${NC} 退出"
    echo ""

    log_info "正在启动 QEMU (USB 模式)..."
    echo ""

    cd "${SCRIPT_DIR}"
    qemu-system-i386 \
        -cpu "${X220_CPU}" \
        -smp "${QEMU_SMP}" \
        -m "${QEMU_MEM}" \
        -display cocoa \
        -vga std \
        -serial mon:stdio \
        -usb \
        -device usb-ehci,id=ehci \
        -device usb-storage,bus=ehci.0,drive=usbdisk,bootindex=1 \
        -drive if=none,id=usbdisk,file=sirpair-kernel.img,format=raw \
        -device e1000e,netdev=net0 \
        -netdev user,id=net0 \
        -rtc base=localtime,clock=host
}

# ============================================================================
# QEMU 运行 - USB 模式 (纯串口)
# ============================================================================
do_run_nox() {
    check_docker_environment
    ensure_built

    echo ""
    log_step "启动 Sirpair OS (USB 模式, 纯串口)"
    echo ""
    echo -e "  ${BOLD}映像文件:${NC} sirpair-kernel.img (通过 USB 存储设备)"
    echo -e "  ${BOLD}磁盘模式:${NC} ${YELLOW}USB 模式 (PCI → EHCI → USB → Mass Storage)${NC}"
    echo -e "  ${BOLD}退出方式:${NC} 按 ${YELLOW}Ctrl-A X${NC}"
    echo ""

    docker run --rm $(docker_tty_flags) \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        qemu-system-i386 \
            -cpu "${X220_CPU}" \
            -smp "${QEMU_SMP}" \
            -m "${QEMU_MEM}" \
            -nographic \
            -usb \
            -device usb-ehci,id=ehci \
            -device usb-storage,bus=ehci.0,drive=usbdisk,bootindex=1 \
            -drive if=none,id=usbdisk,file=sirpair-kernel.img,format=raw \
            -device e1000e,netdev=net0 \
            -netdev user,id=net0 \
            -rtc base=localtime,clock=host
}

# ============================================================================
# QEMU 运行 - USB 模式 (Docker + VNC, 不依赖宿主机 QEMU)
# ============================================================================
do_run_vnc() {
    check_docker_environment
    ensure_built

    local VNC_PORT=5900

    echo ""
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo -e "${GREEN}${BOLD}  Sirpair OS - VNC 远程显示模式 (Docker 内运行 QEMU)${NC}"
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo ""
    echo -e "  ${BOLD}映像文件:${NC}    sirpair-kernel.img (通过 USB 存储设备挂载)"
    echo -e "  ${BOLD}运行方式:${NC}    Docker 容器内 QEMU + VNC 远程显示"
    echo -e "  ${BOLD}磁盘模式:${NC}    ${YELLOW}USB 模式 (PCI → EHCI → USB → Mass Storage)${NC}"
    echo ""
    echo -e "  ${BOLD}VNC 连接信息:${NC}"
    echo -e "    地址:    ${CYAN}vnc://localhost:${VNC_PORT}${NC}"
    echo -e "    macOS:   会自动打开屏幕共享应用"
    echo ""
    echo -e "  ${BOLD}操作说明:${NC}"
    echo -e "    在终端按 ${YELLOW}Ctrl-C${NC} 退出 QEMU"
    echo -e "    串口输出同时显示在终端中"
    echo ""

    log_info "正在启动 QEMU (VNC 模式, 端口 ${VNC_PORT})..."
    echo ""

    # macOS 上自动打开 VNC 客户端 (屏幕共享)
    if [[ "$(uname)" == "Darwin" ]]; then
        ( sleep 3 && open "vnc://localhost:${VNC_PORT}" 2>/dev/null ) &
        local VNC_OPENER_PID=$!
    fi

    docker run --rm $(docker_tty_flags) \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        -p "${VNC_PORT}:${VNC_PORT}" \
        "${DOCKER_IMAGE}" \
        qemu-system-i386 \
            -cpu "${X220_CPU}" \
            -smp "${QEMU_SMP}" \
            -m "${QEMU_MEM}" \
            -vnc :0 \
            -vga std \
            -serial mon:stdio \
            -usb \
            -device usb-ehci,id=ehci \
            -device usb-storage,bus=ehci.0,drive=usbdisk,bootindex=1 \
            -drive if=none,id=usbdisk,file=sirpair-kernel.img,format=raw \
            -device e1000e,netdev=net0 \
            -netdev user,id=net0 \
            -rtc base=localtime,clock=host

    # 清理 VNC 自动打开进程
    if [[ -n "${VNC_OPENER_PID:-}" ]]; then
        kill "${VNC_OPENER_PID}" 2>/dev/null || true
    fi
}

# ============================================================================
# 调试模式 (原生 GUI + GDB, USB 模式)
# ============================================================================
do_debug() {
    check_docker_environment
    ensure_built
    check_qemu_environment

    echo ""
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo -e "${GREEN}${BOLD}  Sirpair OS - 调试模式 (USB 模式 + GDB)${NC}"
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo ""
    echo -e "  ${BOLD}映像文件:${NC}    sirpair-kernel.img (通过 USB 存储设备)"
    echo -e "  ${BOLD}磁盘模式:${NC}    ${YELLOW}USB 模式${NC}"
    echo -e "  ${BOLD}GDB 连接:${NC}    ${CYAN}target remote localhost:${GDB_PORT}${NC}"
    echo -e "  ${BOLD}退出方式:${NC}    关闭窗口或按 ${YELLOW}Ctrl-C${NC}"
    echo ""

    log_info "正在启动 QEMU 调试模式..."
    echo ""

    cd "${SCRIPT_DIR}"
    qemu-system-i386 \
        -cpu "${X220_CPU}" \
        -smp "${QEMU_SMP}" \
        -m "${QEMU_MEM}" \
        -display cocoa \
        -vga std \
        -serial mon:stdio \
        -usb \
        -device usb-ehci,id=ehci \
        -device usb-storage,bus=ehci.0,drive=usbdisk,bootindex=1 \
        -drive if=none,id=usbdisk,file=sirpair-kernel.img,format=raw \
        -device e1000e,netdev=net0 \
        -netdev user,id=net0 \
        -rtc base=localtime,clock=host \
        -gdb tcp::${GDB_PORT} -S
}

# ============================================================================
# 进入 Docker 开发环境
# ============================================================================
do_shell() {
    check_docker_environment
    log_step "进入 Docker 开发环境"
    echo -e "  ${BOLD}镜像:${NC}    ${DOCKER_IMAGE}"
    echo -e "  ${BOLD}工作目录:${NC} ${MOUNT_POINT}"
    echo -e "  ${BOLD}退出方式:${NC} 输入 ${YELLOW}exit${NC}"
    echo ""
    docker run --rm $(docker_tty_flags) \
        -v "${SCRIPT_DIR}:${MOUNT_POINT}" \
        -w "${MOUNT_POINT}" \
        "${DOCKER_IMAGE}" \
        /bin/bash
}

# ============================================================================
# 显示帮助
# ============================================================================
show_help() {
    echo ""
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo -e "${GREEN}${BOLD}  Sirpair OS Docker 一键编译和运行工具${NC}"
    echo -e "${GREEN}${BOLD}  目标: ThinkPad X220 (Intel i5-2520M Sandy Bridge)${NC}"
    echo -e "${GREEN}${BOLD}  磁盘: USB 驱动 (已完全移除 IDE 支持)${NC}"
    echo -e "${GREEN}${BOLD}============================================================================${NC}"
    echo ""
    echo "用法: $0 [命令]"
    echo ""
    echo "命令:"
    echo -e "  ${BOLD}build${NC}           编译 Sirpair OS + 生成合并映像 sirpair-kernel.img ${YELLOW}(默认，不跑回归)${NC}"
    echo -e "  ${BOLD}test${NC}            快速自动化回归（关键链路）+ QEMU 冒烟 ${YELLOW}(推荐)${NC}"
    echo -e "  ${BOLD}test-full${NC}       全量自动化回归（最完整，耗时较长）+ QEMU 冒烟"
    echo -e "  ${BOLD}run${NC}             原生 GUI 窗口运行 (需宿主机 QEMU, brew install qemu)"
    echo -e "  ${BOLD}run-vnc${NC}         VNC 远程显示运行 (Docker 内 QEMU, 不需宿主机 QEMU)"
    echo -e "  ${BOLD}run-nox${NC}         纯串口运行 (Docker 内 QEMU, 无图形界面)"
    echo -e "  ${BOLD}smoke${NC}           仅 QEMU 冒烟（串口检测 init 启动 shell）"
    echo -e "  ${BOLD}clean${NC}           清理构建产物"
    echo -e "  ${BOLD}debug${NC}           调试模式 (GUI + GDB, 需宿主机 QEMU)"
    echo -e "  ${BOLD}shell${NC}           进入 Docker 开发环境"
    echo -e "  ${BOLD}help${NC}            显示此帮助信息"
    echo ""
    echo "磁盘驱动栈:"
    echo "  PCI 总线扫描 → EHCI USB 控制器 → USB 协议 → Mass Storage → SCSI → 块 I/O"
    echo "  已完全移除 IDE 硬盘支持，所有磁盘操作通过 USB 驱动完成"
    echo ""
    echo "映像说明:"
    echo "  编译产生的 sirpair-kernel.img 是一个合并映像，包含:"
    echo "    - 引导扇区 (使用 BIOS INT 13h，兼容 USB 启动)"
    echo "    - 内核 ELF (含 USB 驱动栈)"
    echo "    - 文件系统"
    echo ""
    echo "  该映像可以:"
    echo "    1. 在 QEMU 模拟器中通过 USB 存储设备启动运行"
    echo "    2. 用 dd 写入U盘后在 ThinkPad X220 真机上启动"
    echo ""
    echo "写入U盘:"
    echo "  sudo dd if=sirpair-kernel.img of=/dev/sdX bs=512"
    echo ""
}

# ============================================================================
# 主入口
# ============================================================================
case "${1:-build}" in
    build)
        do_build
        ;;
    test)
        do_test
        ;;
    test-full)
        do_test_full
        ;;
    run)
        do_run
        ;;
    run-vnc)
        do_run_vnc
        ;;
    run-nox)
        do_run_nox
        ;;
    smoke)
        do_smoke
        ;;
    clean)
        do_clean
        ;;
    debug)
        do_debug
        ;;
    shell)
        do_shell
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        log_error "未知命令: $1"
        show_help
        exit 1
        ;;
esac
