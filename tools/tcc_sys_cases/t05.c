/* 结构体与指针 */
struct S {
  int x;
  int y;
};

int sz(struct S *p)
{
  return p->x + p->y;
}
