#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int p_p2c[2];
    int p_c2p[2];
    pipe(p_p2c);
    pipe(p_c2p);

    if(fork() == 0) {
        // child
        // p2c        
        int child_pid1 = getpid();
        int parent_pid1;
        close(p_p2c[1]);
        read(p_p2c[0], &parent_pid1, sizeof(parent_pid1));
        printf("%d: received ping from pid %d\n", child_pid1, parent_pid1);
        close(p_p2c[0]);
        // c2p
        close(p_c2p[0]);
        write(p_c2p[1], &child_pid1, sizeof(child_pid1));
        close(p_c2p[1]);
    } else {
        // parent
        // p2c
        int parent_pid0 = getpid();
        close(p_p2c[0]);
        write(p_p2c[1], &parent_pid0, sizeof(parent_pid0));
        close(p_p2c[1]);

        // c2p
        int child_pid0;
        close(p_c2p[1]);
        read(p_c2p[0], &child_pid0, sizeof(child_pid0));
        printf("%d: received pong from pid %d\n", parent_pid0, child_pid0);
        close(p_c2p[0]);
    }

    exit(0);
}