#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int p2c[2], c2p[2];
  int pid;

  if(pipe(p2c) < 0 || pipe(c2p) < 0){
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    fprintf(2, "pingpong: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    int mypid, ppid;

    close(p2c[1]);
    close(c2p[0]);

    if(read(p2c[0], &ppid, sizeof(ppid)) != sizeof(ppid)){
      fprintf(2, "pingpong: child read failed\n");
      exit(1);
    }
    close(p2c[0]);

    mypid = getpid();
    printf("%d: received ping from pid %d\n", mypid, ppid);

    if(write(c2p[1], &mypid, sizeof(mypid)) != sizeof(mypid)){
      fprintf(2, "pingpong: child write failed\n");
      exit(1);
    }
    close(c2p[1]);
    exit(0);
  }

  {
    int mypid, cpid;

    close(p2c[0]);
    close(c2p[1]);

    mypid = getpid();
    if(write(p2c[1], &mypid, sizeof(mypid)) != sizeof(mypid)){
      fprintf(2, "pingpong: parent write failed\n");
      exit(1);
    }
    close(p2c[1]);

    if(read(c2p[0], &cpid, sizeof(cpid)) != sizeof(cpid)){
      fprintf(2, "pingpong: parent read failed\n");
      exit(1);
    }
    close(c2p[0]);

    printf("%d: received pong from pid %d\n", mypid, cpid);
    wait(0);
    exit(0);
  }
}
