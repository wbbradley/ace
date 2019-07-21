#pragma once
#include <stdio.h>

#include "location.h"

enum LogLevel {
  log_info = 1,
  log_warning = 2,
  log_error = 4,
  log_panic = 8,
};

struct Location;

void log_enable(int log_level);
void logv(LogLevel level, const char *format, va_list args);
void logv_location(LogLevel level,
                   const Location &location,
                   const char *format,
                   va_list args);
void log(const char *format, ...);
void log(LogLevel level, const char *format, ...);
void log_location(LogLevel level,
                  const Location &location,
                  const char *format,
                  ...);
void log_location(const Location &location, const char *format, ...);
void panic_(const char *filename, int line, std::string msg);
void log_stack(LogLevel level);
void log_dump();
void write_fp(FILE *fp, const char *format, ...);

bool check_errno(const char *tag);
void print_stacktrace(FILE *p_out, unsigned int p_max_frames);
