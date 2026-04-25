#ifndef SIRPAIR_MATH_H
#define SIRPAIR_MATH_H

/*
 * Lua 的 l_mathop(op) 使用 op##f 拼接；若 sin/pow 等为宏，参数会先被展开，
 * 导致 token 拼接错误并链接到 libm。此处用 static inline，使 l_mathop(sin) 得到 sinf。
 */
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VAL  (__builtin_huge_val())
#define INFINITY  (__builtin_inff())
#define NAN       (__builtin_nanf(""))

static inline float sinf(float x) { return __builtin_sinf(x); }
static inline float cosf(float x) { return __builtin_cosf(x); }
static inline float tanf(float x) { return __builtin_tanf(x); }
static inline float asinf(float x) { return __builtin_asinf(x); }
static inline float acosf(float x) { return __builtin_acosf(x); }
static inline float atan2f(float y, float x) { return __builtin_atan2f(y, x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline float ceilf(float x) { return __builtin_ceilf(x); }
static inline float fmodf(float x, float y) { return __builtin_fmodf(x, y); }
static inline float frexpf(float x, int *e) { return __builtin_frexpf(x, e); }
static inline float ldexpf(float x, int e) { return __builtin_ldexpf(x, e); }
static inline float modff(float x, float *ip) { return __builtin_modff(x, ip); }
static inline float sqrtf(float x) { return __builtin_sqrtf(x); }
static inline float powf(float x, float y) { return __builtin_powf(x, y); }
static inline float logf(float x) { return __builtin_logf(x); }
static inline float log10f(float x) { return __builtin_log10f(x); }
static inline float log2f(float x) { return __builtin_log2f(x); }
static inline float expf(float x) { return __builtin_expf(x); }
static inline float coshf(float x) { return __builtin_coshf(x); }
static inline float sinhf(float x) { return __builtin_sinhf(x); }
static inline float tanhf(float x) { return __builtin_tanhf(x); }

static inline double sin(double x) { return __builtin_sin(x); }
static inline double cos(double x) { return __builtin_cos(x); }
static inline double tan(double x) { return __builtin_tan(x); }
static inline double asin(double x) { return __builtin_asin(x); }
static inline double acos(double x) { return __builtin_acos(x); }
static inline double atan2(double y, double x) { return __builtin_atan2(y, x); }
static inline double fabs(double x) { return __builtin_fabs(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline double ceil(double x) { return __builtin_ceil(x); }
static inline double fmod(double x, double y) { return __builtin_fmod(x, y); }
static inline double frexp(double x, int *e) { return __builtin_frexp(x, e); }
static inline double ldexp(double x, int e) { return __builtin_ldexp(x, e); }
static inline double modf(double x, double *ip) { return __builtin_modf(x, ip); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline double pow(double x, double y) { return __builtin_pow(x, y); }
static inline double log(double x) { return __builtin_log(x); }
static inline double log10(double x) { return __builtin_log10(x); }
static inline double log2(double x) { return __builtin_log2(x); }
static inline double exp(double x) { return __builtin_exp(x); }
static inline double cosh(double x) { return __builtin_cosh(x); }
static inline double sinh(double x) { return __builtin_sinh(x); }
static inline double tanh(double x) { return __builtin_tanh(x); }

#endif
