#include "context.h"

#include "utils.h"

Context make_context(Location location, const char *format, ...) {
  va_list args;
  va_start(args, format);
  Context context = Context{location, string_formatv(format, args)};
  va_end(args);
  return context;
}
