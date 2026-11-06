// rssrun: run a command, print "RSSRUN wall_s maxrss_kB" to stderr (child's own peak RSS).
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
int main(int c, char** v) {
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    pid_t p = fork();
    if (p == 0) { execvp(v[1], v + 1); _exit(127); }
    int st; struct rusage ru; wait4(p, &st, 0, &ru);
    clock_gettime(CLOCK_MONOTONIC, &b);
    fprintf(stderr, "RSSRUN %.4f %ld %.4f\n", (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9, ru.ru_maxrss, ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
}
