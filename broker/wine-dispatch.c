/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Prefix-scoped Wine launch broker. Linux, no interpreter dependency.
 * Wire format: bounded native-endian local records; never compatible with
 * the Python prototype. Only same-uid peers and identical Flatpak profiles
 * and runner paths may share an endpoint. */
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#define MAGIC 0x57425232u
#define LIMIT (2u * 1024u * 1024u)
#define MAX_STRINGS 65536u
#define IDLE_SECONDS 15
extern char **environ;
static char bindir[PATH_MAX], serverlock[PATH_MAX], profile[65536];
static int dirfd, logfd;
static volatile sig_atomic_t pending_signal;
struct header { uint32_t magic, bytes, argc, envc, server; };
struct reply { uint32_t magic; int32_t status; };

static void die(const char *s) { fprintf(stderr, "wine-dispatch: %s: %s\n", s, strerror(errno)); exit(125); }
/* The owner and its children are elected by whichever launcher came first: an ignored or blocked signal there (a background shell ignores SIGINT) must not
 * reach later launches. Dispositions survive exec, so reset every one and unblock all; the proxy still forwards the caller's signals explicitly. */
static void default_signals(void)
{
    sigset_t none; sigemptyset(&none); sigprocmask(SIG_SETMASK,&none,NULL);
    for (int sig=1; sig<NSIG; sig++) if (sig!=SIGKILL && sig!=SIGSTOP) signal(sig,SIG_DFL);
}
static int full(int fd, void *p, size_t n, int writing)
{
    while (n) {
        ssize_t r = writing ? send(fd, p, n, MSG_NOSIGNAL) : read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p = (char *)p + r; n -= r;
    }
    return 0;
}
static int auth(int fd)
{
    struct ucred c; socklen_t n = sizeof(c);
    return !getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &c, &n) && c.uid == getuid();
}
static struct sockaddr_un address(void)
{
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof(a.sun_path), "/proc/self/fd/%d/socket-c2", dirfd);
    return a;
}
static int connect_broker(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un a = address();
    if (fd < 0) die("socket");
    if (connect(fd, (void *)&a, sizeof(a)) || !auth(fd)) { close(fd); return -1; }
    return fd;
}
static int server_running(void)
{
    int fd = open(serverlock, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct flock f = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    int busy = fcntl(fd, F_GETLK, &f) == -1 || f.l_type != F_UNLCK;
    close(fd); return busy;
}
static void read_profile(void)
{
    FILE *f = fopen("/.flatpak-info", "r");
    if (!f) { strcpy(profile, "host\n"); return; }
    char *line = NULL; size_t cap = 0, used = 0;
    while (getline(&line, &cap, f) != -1) {
        if (!strncmp(line, "instance-id=", 12) || !strncmp(line, "instance-path=", 14)) continue;
        size_t n = strlen(line);
        if (used + n >= sizeof(profile)) { errno = E2BIG; die("Flatpak profile"); }
        memcpy(profile + used, line, n); used += n;
    }
    if (ferror(f)) die("read Flatpak profile");
    free(line); fclose(f);
}
static int secure_dir(int parent, const char *name)
{
    if (mkdirat(parent, name, 0700) && errno != EEXIST) die("mkdir broker directory");
    int fd = openat(parent, name, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) die("open broker directory");
    if (st.st_uid != getuid() || (st.st_mode & 077)) { errno = EPERM; die("unsafe broker directory"); }
    return fd;
}
static void reply(int fd, int status)
{
    struct reply r = {MAGIC, status}; full(fd, &r, sizeof(r), 1);
}
static void close_extra(void)
{
    close_range(3, ~0u, 0);
}
static char *take(char **p, char *end)
{
    if (*p >= end) return NULL;
    char *s = *p, *nul = memchr(s, 0, end - s);
    if (!nul) return NULL;
    *p = nul + 1; return s;
}
static void worker(int fd)
{
    /* A forked worker owns one child and one proxy connection. There are no
     * threads and no post-fork libc lock hazards. */
    char control[CMSG_SPACE(3 * sizeof(int))] = {0};
    struct header h;
    struct iovec iov = {&h, sizeof(h)};
    struct msghdr msg = {.msg_iov=&iov, .msg_iovlen=1, .msg_control=control, .msg_controllen=sizeof(control)};
    int fds[3] = {-1,-1,-1}, count = 0;
    ssize_t n = recvmsg(fd, &msg, MSG_WAITALL | MSG_CMSG_CLOEXEC);
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg,c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
        size_t k = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        int *v = (int *)CMSG_DATA(c);
        for (size_t j=0; j<k; j++) { if (count<3) fds[count++]=v[j]; else close(v[j]); }
    }
    if (n != sizeof(h) || msg.msg_flags & (MSG_CTRUNC|MSG_TRUNC) || count != 3 || h.magic != MAGIC ||
        !h.bytes || h.bytes > LIMIT || !h.argc || h.argc > MAX_STRINGS || h.envc > MAX_STRINGS || h.server > 1) _exit(125);
    char *data = malloc(h.bytes), **argv = calloc(h.argc+1,sizeof(char *)), **env = calloc(h.envc+1,sizeof(char *));
    if (!data || !argv || !env || full(fd,data,h.bytes,0)) _exit(125);
    char *p=data, *end=data+h.bytes;
    char *runner=take(&p,end), *policy=take(&p,end), *cwd=take(&p,end);
    if (!runner || !policy || !cwd || strcmp(runner,bindir) || strcmp(policy,profile)) {
        dprintf(logfd,"refused incompatible runner or sandbox profile\n"); reply(fd,-1); _exit(125);
    }
    for (unsigned i=0;i<h.argc;i++) if (!(argv[i]=take(&p,end))) _exit(125);
    for (unsigned i=0;i<h.envc;i++) if (!(env[i]=take(&p,end))) _exit(125);
    if (p!=end) _exit(125);
    char target[PATH_MAX];
    if (snprintf(target,sizeof(target),"%s/%s",bindir,h.server?"wineserver-real":"wine-real") >= (int)sizeof(target)) _exit(125);
    pid_t pid=fork();
    if (pid<0) { reply(fd,-1); _exit(125); }
    if (!pid) {
        default_signals();
        if (chdir(cwd)) { dprintf(fds[2],"wine-dispatch: inaccessible cwd: %s\n",strerror(errno)); _exit(127); }
        for(int i=0;i<3;i++) if(dup2(fds[i],i)<0) _exit(127);
        close_extra(); execve(target,argv,env);
        dprintf(2,"wine-dispatch: exec: %s\n",strerror(errno)); _exit(127);
    }
    for(int i=0;i<3;i++) close(fds[i]);
    free(argv); free(env); free(data);
    dprintf(logfd,"run pid=%d\n",pid); reply(fd,0);
    int status, connected=1;
    while(waitpid(pid,&status,WNOHANG)==0) {
        struct pollfd pollfd={fd,connected?POLLIN:0,0};
        if (!connected) { poll(NULL,0,50); continue; }
        if(poll(&pollfd,1,50)>0) {
            int sig;
            ssize_t r=recv(fd,&sig,sizeof(sig),MSG_DONTWAIT);
            if(r==sizeof(sig) && (sig==SIGINT||sig==SIGTERM||sig==SIGHUP)) kill(pid,sig);
            else if(r<=0) { connected=0; }
        }
    }
    dprintf(logfd,"done pid=%d status=%d\n",pid,status);
    reply(fd,status); _exit(0);
}
static void broker(int ready, int election)
{
    prctl(PR_SET_NAME,"wine-broker",0,0,0);
    default_signals();
    signal(SIGPIPE,SIG_IGN); signal(SIGHUP,SIG_IGN);
    int null=open("/dev/null",O_RDWR);
    for(int i=0;i<3;i++) dup2(null,i);
    close(null);
    /* Do not retain unrelated launcher pipes, bus sockets or document FDs. */
    int keep[3]={dirfd,ready,election};
    for(int i=0;i<3;i++) for(int j=i+1;j<3;j++)
        if(keep[j]<keep[i]) {int tmp=keep[i];keep[i]=keep[j];keep[j]=tmp;}
    unsigned first=3;
    for(int i=0;i<3;i++) {
        if((unsigned)keep[i]>first) close_range(first,keep[i]-1,0);
        first=keep[i]+1;
    }
    close_range(first,~0u,0);
    logfd=openat(dirfd,"broker-c2.log",O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
    if(logfd<0) _exit(125);
    int srv=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    struct sockaddr_un a=address(); unlinkat(dirfd,"socket-c2",0);
    if(bind(srv,(void *)&a,sizeof(a)) || listen(srv,32)) _exit(125);
    dprintf(logfd,"broker up pid=%d\n",getpid());
    if(write(ready,"1",1)!=1) _exit(125);
    close(ready); close(election);
    unsigned active=0; time_t idle=time(NULL);
    for(;;) {
        while(waitpid(-1,NULL,WNOHANG)>0) { if(active) active--; }
        if(active || server_running()) idle=time(NULL);
        if(time(NULL)-idle>=IDLE_SECONDS) {
            int lk=openat(dirfd,"lock-c2",O_RDWR|O_CLOEXEC|O_NOFOLLOW);
            if(lk>=0 && !flock(lk,LOCK_EX|LOCK_NB)) {
                struct pollfd pfd={srv,POLLIN,0};
                if(!active && !server_running() && poll(&pfd,1,0)==0) {
                    unlinkat(dirfd,"socket-c2",0); dprintf(logfd,"idle exit\n"); _exit(0);
                }
                flock(lk,LOCK_UN);
            }
            if(lk>=0)close(lk);
        }
        struct pollfd pfd={srv,POLLIN,0};
        if(poll(&pfd,1,250)<=0)continue;
        int fd=accept4(srv,NULL,NULL,SOCK_CLOEXEC);
        if(fd<0)continue;
        if(!auth(fd)) {close(fd);continue;}
        struct timeval timeout={5,0}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        pid_t pid=fork();
        if(!pid) {close(srv);close(dirfd); worker(fd);}
        if(pid>0) {active++;idle=time(NULL);}
        close(fd);
    }
}
static void on_signal(int sig) {pending_signal=sig;}
int main(int argc,char **argv)
{
    prctl(PR_SET_NAME,"wine-proxy",0,0,0);
    umask(077);
    char self[PATH_MAX], prefix[PATH_MAX], *tool=strrchr(argv[0],'/'); tool=tool?tool+1:argv[0];
    int server=!strcmp(tool,"wineserver");
    ssize_t len=readlink("/proc/self/exe",self,sizeof(self)-1);
    if(len<0 || len==(ssize_t)sizeof(self)-1)die("executable path");
    self[len]=0; char *slash=strrchr(self,'/'); *slash=0; strcpy(bindir,self);
    const char *setting=getenv("WINEPREFIX");
    if(!setting || !*setting) {const char *home=getenv("HOME"); if(!home){errno=ENOENT;die("HOME");}
        if(snprintf(prefix,sizeof(prefix),"%s/.wine",home)>=(int)sizeof(prefix)){errno=ENAMETOOLONG;die("prefix");}}
    else {if(strlen(setting)>=sizeof(prefix)){errno=ENAMETOOLONG;die("prefix");} strcpy(prefix,setting);}
    /* Wine normally creates the prefix; a broker needs its identity first. */
    if(mkdir(prefix,0700) && errno!=EEXIST)die("create prefix");
    int pfx=open(prefix,O_DIRECTORY|O_CLOEXEC); struct stat st;
    if(pfx<0 || fstat(pfx,&st))die("prefix");
    snprintf(serverlock,sizeof(serverlock),"/tmp/.wine-%u/server-%lx-%lx/lock",getuid(),(unsigned long)st.st_dev,(unsigned long)st.st_ino);
    dirfd=secure_dir(pfx,".wine-wayland-broker");close(pfx);
    read_profile();
    int lk=openat(dirfd,"lock-c2",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lk<0 || flock(lk,LOCK_EX))die("election lock");
    int fd=connect_broker();
    if(fd<0) {
        /* Never attach a new owner to an unbrokered live server. */
        if(server_running()){errno=EBUSY;die("unbrokered wineserver is running; close prefix applications first");}
        int pipefd[2]; if(pipe2(pipefd,O_CLOEXEC))die("ready pipe");
        pid_t pid=fork(); if(pid<0)die("fork broker");
        if(!pid) {close(pipefd[0]); if(setsid()<0)_exit(125); pid=fork(); if(pid<0)_exit(125);if(pid)_exit(0);broker(pipefd[1],lk);}
        close(pipefd[1]); waitpid(pid,NULL,0);
        struct pollfd pfd={pipefd[0],POLLIN,0};char ok=0;
        if(poll(&pfd,1,5000)<=0 || read(pipefd[0],&ok,1)!=1 || ok!='1'){errno=EIO;die("broker readiness");}
        close(pipefd[0]);fd=connect_broker();if(fd<0)die("connect new broker");
    }
    /* Send records and stdio while election is locked: idle shutdown cannot
     * race the interval between discovery and the request being queued. */
    size_t bytes=strlen(bindir)+strlen(profile)+2;
    char *cwd=getcwd(NULL,0); if(!cwd)die("cwd"); bytes+=strlen(cwd)+1;
    unsigned envc=0;
    for(int i=0;i<argc;i++)bytes+=strlen(argv[i])+1;
    while(environ[envc]){bytes+=strlen(environ[envc])+1;envc++;}
    if(bytes>LIMIT || (unsigned)argc>MAX_STRINGS || envc>MAX_STRINGS){errno=E2BIG;die("request");}
    char *data=malloc(bytes), *out=data;if(!data)die("allocation");
#define APPEND(s) do{size_t n=strlen(s)+1;memcpy(out,s,n);out+=n;}while(0)
    APPEND(bindir); APPEND(profile); APPEND(cwd);
    for(int i=0;i<argc;i++)APPEND(argv[i]);
    for(unsigned i=0;i<envc;i++)APPEND(environ[i]);
    struct header h={MAGIC,bytes,argc,envc,server};
    char control[CMSG_SPACE(3*sizeof(int))]={0};struct iovec iov={&h,sizeof(h)};
    struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
    struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(3*sizeof(int));
    int stdio[3]={0,1,2};memcpy(CMSG_DATA(c),stdio,sizeof(stdio));
    struct sigaction sa={.sa_handler=on_signal};sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);sigaction(SIGHUP,&sa,NULL);
    ssize_t sent=sendmsg(fd,&msg,MSG_NOSIGNAL);
    if(sent<=0 || ((size_t)sent<sizeof(h) && full(fd,(char *)&h+sent,sizeof(h)-sent,1)) || full(fd,data,bytes,1))die("send request");
    free(data);free(cwd);
    flock(lk,LOCK_UN);close(lk);
    struct reply r;
    if(full(fd,&r,sizeof(r),0) || r.magic!=MAGIC || r.status<0) {errno=EPERM;die("broker refused request (runner or sandbox profile mismatch)");}
    for(;;) {
        if(pending_signal){int sig=pending_signal;pending_signal=0;full(fd,&sig,sizeof(sig),1);}
        struct pollfd pfd={fd,POLLIN,0};
        if(poll(&pfd,1,-1)<0){if(errno==EINTR)continue;die("wait for result");}
        if(full(fd,&r,sizeof(r),0) || r.magic!=MAGIC){errno=EPIPE;die("broker vanished during request");}
        break;
    }
    if(WIFSIGNALED(r.status)){int sig=WTERMSIG(r.status);signal(sig,SIG_DFL);raise(sig);return 128+sig;}
    return WIFEXITED(r.status)?WEXITSTATUS(r.status):125;
}
