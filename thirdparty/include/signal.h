#pragma once
#include_next <signal.h>

sighandler_t signal(int sig, sighandler_t handler);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int sig);
int raise(int sig);
int sigsuspend(const sigset_t *mask);
