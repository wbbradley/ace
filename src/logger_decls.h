#pragma once
#include <stdio.h>

#include "location.h"

enum log_level_t {
  log_info = 1,
  log_warning = 2,
  log_error = 4,
  log_panic = 8,
};

void log_enable(int log_level);
void logv(log_level_t level, const char *format, va_list args);
void logv_location(log_level_t level,
                   const location_t &location,
                   const char *format,
                   va_list args);
void log(const char *format, ...);
void log(log_level_t level, const char *format, ...);
void log_location(log_level_t level, const location_t &location, const char *format, ...);
void log_location(const location_t &location, const char *format, ...);
void panic_(const char *filename, int line, std::string msg);
void log_stack(log_level_t level);
void log_dump();
void write_fp(FILE *fp, const char *format, ...);

bool check_errno(const char *tag);
void print_stacktrace(FILE *p_out, unsigned int p_max_frames);
