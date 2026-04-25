/* Lua 构建使用 -Iuser/lua/include；与内核共用 include/errno.h，避免重复 guard 截断宏。 */
#ifndef SIRPAIR_LUA_ERRNO_WRAPPER_H
#define SIRPAIR_LUA_ERRNO_WRAPPER_H
#include "../../../include/errno.h"
#endif
