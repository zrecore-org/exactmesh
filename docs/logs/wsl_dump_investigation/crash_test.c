// Throwaway program for the WSL2 core-dump investigation. Build (static): gcc -O0 -static -o crash_test crash_test.c
// Usage: crash_test <signal-mode> [pre-action]
//   signal-mode: segv | abort
//   pre-action : none | rlimit0 | rlimit1 | undumpable | rlimit0_undumpable   (done inside the process before crashing)
// It prints its RLIMIT_CORE (soft/hard) and PR_GET_DUMPABLE, then crashes. It allocates 64 MiB of touched memory first so that a
// full dump would be clearly larger than a tiny one (a dump of the whole process image would be >= 64 MiB).
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "segv";
    const char* pre = argc > 2 ? argv[2] : "none";
    if (!strcmp(pre, "rlimit0") || !strcmp(pre, "rlimit0_undumpable")) { struct rlimit r = {0, 0}; setrlimit(RLIMIT_CORE, &r); }
    if (!strcmp(pre, "rlimit1")) { struct rlimit r = {1, 1}; setrlimit(RLIMIT_CORE, &r); }
    if (!strcmp(pre, "undumpable") || !strcmp(pre, "rlimit0_undumpable")) prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    if (!strcmp(pre, "undumpable_exec")) {  // supervisor pattern: clear dumpable, then execve the "real" program (here: this binary again)
        prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
        printf("supervisor: dumpable=%d before exec\n", prctl(PR_GET_DUMPABLE, 0, 0, 0, 0)); fflush(stdout);
        char* args[] = {argv[0], (char*)mode, (char*)"none", NULL};
        execv("/proc/self/exe", args);
        return 99;
    }
    struct rlimit rl; getrlimit(RLIMIT_CORE, &rl);
    char* big = (char*)malloc(64u << 20);
    if (big) memset(big, 0x5a, 64u << 20);
    printf("pid=%d mode=%s pre=%s RLIMIT_CORE soft=%llu hard=%llu dumpable=%d\n", (int)getpid(), mode, pre,
           (unsigned long long)rl.rlim_cur, (unsigned long long)rl.rlim_max, prctl(PR_GET_DUMPABLE, 0, 0, 0, 0));
    fflush(stdout);
    if (!strcmp(mode, "abort")) abort();
    volatile int* p = 0; *p = 1;
    return 0;
}
