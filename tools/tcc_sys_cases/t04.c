/* for 循环累加 */
int sum(int n)
{
  int i, s;

  s = 0;
  for(i = 0; i < n; i++)
    s += i;
  return s;
}
