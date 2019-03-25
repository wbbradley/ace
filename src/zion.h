#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <glob.h>
#include <locale>
#include <math.h>
#include <memory>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "builtins.h"
#include "logger_decls.h"
#include "ptr.h"
#include "zion_assert.h"

#define ZION 1
#define GLOBAL_SCOPE_NAME "std"

#define DEFAULT_INT_BITSIZE 64
#define DEFAULT_INT_SIGNED true
#define ZION_BITSIZE_STR "64"
#define ZION_TYPEID_BITSIZE_STR "16"

#define SCOPE_TK tk_dot
#define SCOPE_SEP_CHAR '.'
#define SCOPE_SEP "."

#define MATHY_SYMBOLS "!@#$%^&*()+-_=><.,/|[]`~\\"
