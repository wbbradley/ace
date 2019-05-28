#include "context.h"

#include "utils.h"

context_t make_context(location_t location, const char *format, ...) {
  va_list args;
  va_start(args, format);
  context_t context = context_t{location, string_formatv(format, args)};
  va_end(args);
  return context;
}
