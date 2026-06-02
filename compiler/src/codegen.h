#pragma once
#include "parser.h"

/* Supported compilation targets */
typedef enum {
    TARGET_X86_64_LINUX,   /* default */
    TARGET_X86_64_MACOS,
    TARGET_X86_64_WINDOWS,
    TARGET_AARCH64_LINUX,
    TARGET_AARCH64_MACOS,
} TargetTriple;

/*
 * Parse a target string like "x86_64-linux" into a TargetTriple enum value.
 * Returns 1 on success, 0 if the string is unrecognised.
 */
int target_parse(const char *s, TargetTriple *out);

/*
 * Return the canonical LLVM triple string for a TargetTriple.
 */
const char *target_llvm_triple(TargetTriple t);

/*
 * Return the LLVM datalayout string for a TargetTriple.
 */
const char *target_datalayout(TargetTriple t);

void codegen(Program *prog, const char *out_ll_path, TargetTriple target);
