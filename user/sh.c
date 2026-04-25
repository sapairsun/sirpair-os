// Shell.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "stat.h"
#include "fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static int
is_space_char(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v';
}

static void
copy_str(char *dst, int dstsz, char *src)
{
  int i;
  if(dstsz <= 0)
    return;
  for(i = 0; i < dstsz - 1 && src[i]; i++)
    dst[i] = src[i];
  dst[i] = 0;
}

static int
prefix_match(char *s, char *prefix)
{
  while(*prefix){
    if(*s == 0 || *s != *prefix)
      return 0;
    s++;
    prefix++;
  }
  return 1;
}

static int
name_exists(char names[][DIRSIZ+1], int n, char *name)
{
  int i;
  for(i = 0; i < n; i++){
    if(strcmp(names[i], name) == 0)
      return 1;
  }
  return 0;
}

static int
collect_matches_from_dir(char *dir, char *prefix, char names[][DIRSIZ+1], int maxn, int n)
{
  int fd;
  struct dirent de;
  char nm[DIRSIZ+1];
  int i;

  fd = open(dir, O_RDONLY);
  if(fd < 0)
    return n;

  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;
    if(n >= maxn)
      break;
    memmove(nm, de.name, DIRSIZ);
    nm[DIRSIZ] = 0;
    if(nm[0] == 0)
      continue;
    if(strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0)
      continue;
    if(!prefix_match(nm, prefix))
      continue;
    if(name_exists(names, n, nm))
      continue;
    for(i = 0; i < DIRSIZ + 1; i++)
      names[n][i] = nm[i];
    n++;
  }
  close(fd);
  return n;
}

/* 当前提示符在屏幕上的列宽（不含换行），用于 Tab 制表位与补全显示 */
static int sh_prompt_cols;

static void
print_prompt(void)
{
  char cwd[512];
  if(getcwd(cwd, sizeof(cwd)) < 0)
    strcpy(cwd, "/");
  printf(2, "root@%s# ", cwd);
  sh_prompt_cols = 5 + (int)strlen(cwd) + 2;
}

static void
emit_tab_spaces(int input_len)
{
  int pos, n, k;
  char seq[24];
  int si;
  int v2, ti;
  char tmpd[8];

  pos = sh_prompt_cols + input_len;
  n = 8 - (pos % 8);
  if(n == 0)
    n = 8;
  for(k = 0; k < n; k++)
    write(2, " ", 1);
  /* 内核 CSI 907：为补全插入的空格增加 raw 退格可擦除计数 */
  seq[0] = '\033';
  seq[1] = '[';
  seq[2] = '9';
  seq[3] = '0';
  seq[4] = '7';
  seq[5] = ';';
  si = 6;
  v2 = n;
  ti = 0;
  if(v2 == 0)
    tmpd[ti++] = '0';
  else{
    while(v2 > 0 && ti < (int)sizeof(tmpd)){
      tmpd[ti++] = '0' + (v2 % 10);
      v2 /= 10;
    }
  }
  while(ti > 0)
    seq[si++] = tmpd[--ti];
  seq[si++] = 'h';
  write(2, seq, si);
}

static void
refresh_line(char *buf, int len)
{
  int si, v2, ti;
  char seq[24];
  char tmpd[8];

  write(2, "\n", 1);
  print_prompt();
  if(len > 0)
    write(2, buf, len);
  /* CSI 908：补全列表刷新整行后同步 raw 退格预算（与当前 buf 长度一致） */
  seq[0] = '\033';
  seq[1] = '[';
  seq[2] = '9';
  seq[3] = '0';
  seq[4] = '8';
  seq[5] = ';';
  si = 6;
  v2 = len;
  if(v2 < 0)
    v2 = 0;
  ti = 0;
  if(v2 == 0)
    tmpd[ti++] = '0';
  else{
    while(v2 > 0 && ti < (int)sizeof(tmpd)){
      tmpd[ti++] = '0' + (v2 % 10);
      v2 /= 10;
    }
  }
  while(ti > 0)
    seq[si++] = tmpd[--ti];
  seq[si++] = 'h';
  write(2, seq, si);
}

static void
try_complete(char *buf, int *plen, int nbuf)
{
  int len, start, toklen, i, j, count, pref_len;
  int lcp, append_len, has_slash, is_first_token;
  char token[128], dir[128], prefix[DIRSIZ+1];
  char names[64][DIRSIZ+1];
  char append[DIRSIZ+2];
  char *last_slash;

  len = *plen;
  if(len <= 0){
    emit_tab_spaces(0);
    return;
  }

  start = len - 1;
  while(start >= 0 && !is_space_char(buf[start]))
    start--;
  start++;
  toklen = len - start;
  if(toklen <= 0 || toklen >= (int)sizeof(token))
    return;

  memmove(token, buf + start, toklen);
  token[toklen] = 0;

  is_first_token = 1;
  for(i = 0; i < start; i++){
    if(!is_space_char(buf[i])){
      is_first_token = 0;
      break;
    }
  }

  has_slash = 0;
  last_slash = 0;
  for(i = 0; i < toklen; i++){
    if(token[i] == '/'){
      has_slash = 1;
      last_slash = token + i;
    }
  }

  dir[0] = 0;
  prefix[0] = 0;
  if(has_slash){
    int dlen = last_slash - token;
    if(dlen == 0){
      dir[0] = '/';
      dir[1] = 0;
    } else if(dlen < (int)sizeof(dir)){
      memmove(dir, token, dlen);
      dir[dlen] = 0;
    } else {
      return;
    }
    copy_str(prefix, sizeof(prefix), last_slash + 1);
  } else {
    copy_str(dir, sizeof(dir), ".");
    copy_str(prefix, sizeof(prefix), token);
  }

  pref_len = strlen(prefix);
  count = 0;
  if(has_slash){
    count = collect_matches_from_dir(dir, prefix, names, ARRAY_SIZE(names), count);
  } else {
    if(is_first_token){
      count = collect_matches_from_dir("/bin", prefix, names, ARRAY_SIZE(names), count);
      count = collect_matches_from_dir(".", prefix, names, ARRAY_SIZE(names), count);
    } else {
      count = collect_matches_from_dir(".", prefix, names, ARRAY_SIZE(names), count);
    }
  }

  if(count <= 0){
    emit_tab_spaces(len);
    return;
  }

  lcp = pref_len;
  for(i = pref_len; i < DIRSIZ; i++){
    char c = names[0][i];
    if(c == 0)
      break;
    for(j = 1; j < count; j++){
      if(names[j][i] != c)
        goto done_lcp;
    }
    lcp++;
  }
done_lcp:

  append_len = lcp - pref_len;
  if(len + append_len >= nbuf){
    return;
  }

  if(append_len > 0){
    memmove(append, names[0] + pref_len, append_len);
    append[append_len] = 0;
    memmove(buf + len, append, append_len);
    len += append_len;
    buf[len] = 0;
    write(2, append, append_len);
  }

  if(count > 1){
    refresh_line(buf, len);
    for(i = 0; i < count; i++){
      if(i > 0)
        write(2, "  ", 2);
      write(2, names[i], strlen(names[i]));
    }
    refresh_line(buf, len);
    *plen = len;
    return;
  }

  if(append_len == 0 && count == 1)
    emit_tab_spaces(len);

  *plen = len;
}

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(0);
  
  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(0);
    if((uint)ecmd->argv[0] >= 0x80000000u || (uint)ecmd->argv[0] == 0xffffffffu){
      printf(2, "[sh dbg] invalid command pointer, reject exec pid=%d argv0=0x%x\n",
             getpid(), ecmd->argv[0]);
      exit(0);
    }
    printf(2, "[sh dbg] run command pid=%d argv0=0x%x\n", getpid(), ecmd->argv[0]);
    exec(ecmd->argv[0], ecmd->argv);
    // If exec failed and command has no slash, search /bin first.
    if(ecmd->argv[0][0] != '/'){
      char path[128];
      int i = 0;
      char *s = ecmd->argv[0];

      path[i++] = '/';
      path[i++] = 'b';
      path[i++] = 'i';
      path[i++] = 'n';
      path[i++] = '/';
      while(*s && i < (int)sizeof(path) - 1)
        path[i++] = *s++;
      path[i] = 0;
      exec(path, ecmd->argv);

      // Backward compatibility: fallback to "/cmd".
      path[0] = '/';
      s = ecmd->argv[0];
      i = 1;
      while(*s && i < (int)sizeof(path) - 1)
        path[i++] = *s++;
      path[i] = 0;
      exec(path, ecmd->argv);
    }
    printf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      printf(2, "open %s failed\n", rcmd->file);
      exit(0);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait();
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    {
      int pidr;

      pidr = fork1();
      if(pidr == 0){
        close(0);
        dup(p[0]);
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->right);
      }
      /* 管道右侧（如 more）占控制台；^C 须杀该进程，否则杀协调进程后 more 仍关回显 */
      setfgpid(pidr);
    }
    close(p[0]);
    close(p[1]);
    wait();
    wait();
    setfgpid(0);
    break;
    
  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0){
      /* 后台进程不得与 shell 共享控制台 stdin，否则 lua 等会抢 read(0)，前台无法再输入命令 */
      close(0);
      if(open("/null", O_RDONLY) != 0)
        printf(2, "sh: open /null failed\n");
      runcmd(bcmd->cmd);
    }
    break;
  }
  exit(0);
}

int
getcmd(char *buf, int nbuf)
{
  int i, cc;
  char c;

  print_prompt();
  memset(buf, 0, nbuf);
  i = 0;
  /*
   * 原先用 while(i+1<nbuf) 会在缓冲将满时直接结束，未等回车就把整行当命令执行，
   * 长串输入（如连按 a）会误触发 exec 并打印 failed。须读到换行或读失败才结束。
   * i < nbuf-2：为末尾的 '\n' 与 '\0' 各保留一字节。
   */
  for(;;){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    if(c == '\t'){
      if(i + 1 < nbuf)
        try_complete(buf, &i, nbuf);
      continue;
    }
    if(c == '\b' || c == 0x7f){
      if(i > 0){
        i--;
        buf[i] = 0;
      }
      continue;
    }
    if(c == '\n' || c == '\r'){
      if(i < nbuf - 1)
        buf[i++] = c;
      break;
    }
    if(i < nbuf - 2)
      buf[i++] = c;
  }
  buf[i] = 0;
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd, pid;
  
  // Assumes three file descriptors open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  /*
   * 须开启控制台「原始输入」模式（内核 ESC [ 902 h）：
   * 否则 consoleread 仅在换行时唤醒，Tab 无法及时送达 getcmd，补全无反应；
   * 开启后逐键唤醒，与 Tab 展开为空格回显（consputc）配合即可正常补全。
   */
  write(2, "\033[902h", 6);

  write(2, "\n", 1);
  write(2, "\033[32m", 5);
  printf(2, "----------------------------------------------------\n");
  printf(2, "-                                                  -\n");
  printf(2, "-              Welcome to Sirpair OS!              -\n");
  printf(2, "-                                                  -\n");
  printf(2, "----------------------------------------------------\n");
  write(2, "\033[0m", 4);
  write(2, "\n", 1);

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Clumsy but will have to do for now.
      // Chdir has no effect on the parent if run in the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        printf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    pid = fork1();
    if(pid == 0)
      runcmd(parsecmd(buf));
    setfgpid(pid);
    wait();
    setfgpid(0);
  }
  exit(0);
}

void
panic(char *s)
{
  printf(2, "%s\n", s);
  exit(0);
}

int
fork1(void)
{
  int pid;
  
  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;
  
  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;
  
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;
  
  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    printf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    if(peek(ps, es, ";)") || *(*ps) == 0){
      cmd = backcmd(cmd);
    } else {
      cmd = listcmd(backcmd(cmd), parseline(ps, es));
      return cmd;
    }
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_APPEND, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;
  
  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;
  
  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;
    
  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
