/*
 * i686 交叉 libm.a 中部分发行版不提供 ldexp 的强符号，而 TCC 解析浮点字面量会引用 ldexp。
 */
double
ldexp(double x, int e)
{
  return __builtin_ldexp(x, e);
}
