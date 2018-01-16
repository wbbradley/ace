#pragma once
#include <algorithm> 
#include <cctype>
#include <locale>
#include "llvm_zion.h"
#include "logger_decls.h"
#include "ptr.h"
#include "assert.h"
#include "builtins.h"

#define ZION 1
#define GLOBAL_SCOPE_NAME "std"

#define DEFAULT_INT_BITSIZE 64
#define ZION_BITSIZE_STR "64"
#define getZionIntTy getInt64Ty
#define getZionInt getInt64

