/* Host test stand-in for the NDK's android/log.h: log lines go to stderr. */
#pragma once
#include <stdio.h>
#define ANDROID_LOG_INFO 4
#define __android_log_print(prio, tag, ...) (fprintf(stderr, "[%s] ", tag), fprintf(stderr, __VA_ARGS__), fputc('\n', stderr), 0)
