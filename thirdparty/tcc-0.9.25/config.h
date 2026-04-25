/* 供 Sirpair 用户态与宿主机 i386 工具链使用（configure 在部分平台无法运行时的等价配置） */
#ifndef TCC_CONFIG_H
#define TCC_CONFIG_H

#define CONFIG_SYSROOT ""
#define CONFIG_TCCDIR "/tcc"
#define GCC_MAJOR 4
#define HOST_I386 1
#define TCC_VERSION "0.9.25"

#endif
