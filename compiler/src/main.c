#include "lexer.h"
#include "parser.h"
#include "typechecker.h"
#include "codegen.h"
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open '%s'\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc((size_t)(sz + 1));
    if (!buf) { fprintf(stderr, "OOM\n"); exit(1); }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

static void usage(void) {
    fprintf(stderr,
        "Usage: overhaul [--target <triple>] [--emit-ir] <file.oh> [-o <output>]\n"
        "\n"
        "Flags:\n"
        "  --emit-ir          Keep the LLVM IR (.ll) file alongside the binary\n"
        "  --target <triple>  Cross-compile for the specified target\n"
        "\n"
        "Supported targets:\n"
        "  x86_64-linux    (default) — x86-64 Linux\n"
        "  x86_64-macos              — x86-64 macOS\n"
        "  x86_64-windows            — x86-64 Windows (MSVC ABI)\n"
        "  aarch64-linux             — AArch64 Linux (ARM64)\n"
        "  aarch64-macos             — Apple Silicon (ARM64 macOS)\n"
        "\n"
        "Cross-compilation note: aarch64 and Windows targets require a matching\n"
        "cross-toolchain and sysroot.  Pass --sysroot=<path> via CFLAGS or install\n"
        "the appropriate clang cross-compilation packages.\n"
    );
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    /* Parse flags */
    TargetTriple target = TARGET_X86_64_LINUX;
    const char *inputs[64];     /* one or more .oh source files, compiled as a unit */
    int         input_count = 0;
    const char *out_arg   = NULL;
    int emit_ir = 0;  /* --emit-ir: keep the .ll file next to the binary */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --target requires an argument\n");
                usage();
                return 1;
            }
            i++;
            if (!target_parse(argv[i], &target)) {
                fprintf(stderr, "error: unknown target '%s'\n", argv[i]);
                usage();
                return 1;
            }
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            emit_ir = 1;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -o requires an argument\n");
                usage();
                return 1;
            }
            i++;
            out_arg = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown flag '%s'\n", argv[i]);
            usage();
            return 1;
        } else {
            /* Positional. A non-.oh positional with no -o yet is the output
             * (legacy `overhaul f.oh out` form). Everything else is an input
             * source file — multiple files are compiled together as one unit,
             * which is how a shared stdlib is linked (no per-program re-emit). */
            size_t L = strlen(argv[i]);
            int is_oh = (L > 3 && strcmp(argv[i] + L - 3, ".oh") == 0);
            if (!is_oh && !out_arg && input_count > 0) {
                out_arg = argv[i];
            } else {
                if (input_count >= 64) { fprintf(stderr, "error: too many inputs\n"); return 1; }
                inputs[input_count++] = argv[i];
            }
        }
    }

    if (input_count == 0) {
        fprintf(stderr, "error: no source file specified\n");
        usage();
        return 1;
    }
    const char *src_path = inputs[input_count - 1]; /* last input names the output */

    /* Determine output path */
    char out_path[4096];
    if (out_arg) {
        snprintf(out_path, sizeof(out_path), "%s", out_arg);
    } else {
        /* Strip .oh extension */
        size_t len = strlen(src_path);
        if (len > 3 && strcmp(src_path + len - 3, ".oh") == 0) {
            snprintf(out_path, sizeof(out_path), "%.*s", (int)(len - 3), src_path);
        } else {
            snprintf(out_path, sizeof(out_path), "a.out");
        }
    }

    /* LLVM IR intermediate file path */
    char ll_path[4096];
    snprintf(ll_path, sizeof(ll_path), "%s.ll", out_path);

    /* Read and concatenate all input sources into one whole-program unit
     * (stdlib files first, app last). Newline-joined so terminators hold. */
    char *src;
    if (input_count == 1) {
        src = read_file(inputs[0]);
    } else {
        size_t total = 0;
        char *parts[64];
        for (int i = 0; i < input_count; i++) { parts[i] = read_file(inputs[i]); total += strlen(parts[i]) + 1; }
        src = (char*)malloc(total + 1);
        src[0] = '\0';
        for (int i = 0; i < input_count; i++) { strcat(src, parts[i]); strcat(src, "\n"); free(parts[i]); }
    }

    /* Arena: 16MB */
    Arena a = arena_new(16 * 1024 * 1024);

    /* Lex */
    TokenArray tokens = lex(src, &a);

    /* Parse */
    Program prog = parse(tokens, &a);

    /* Type check */
    typecheck(&prog, &a);

    /* Codegen -> LLVM IR with target metadata */
    codegen(&prog, ll_path, target);

    /*
     * Build the clang invocation.
     *
     * Native (x86_64-linux):
     *   clang -O3 -fno-plt -target x86_64-unknown-linux-gnu <file.ll> -o <out>
     *
     * Cross targets (non-native arch or OS):
     *   clang -O3 --target=<llvm-triple> <file.ll> -o <out>
     *
     * Note: cross-compilation requires a matching sysroot and cross-compilation
     * toolchain to be installed (e.g. clang with AArch64 or Windows cross libs).
     * If the toolchain is absent, clang will emit a descriptive error.
     * See docs/decisions.md for details.
     */
    const char *llvm_triple = target_llvm_triple(target);
    char cmd[8192];

    if (target == TARGET_X86_64_LINUX) {
        /* Native Linux x86-64: use -march=native so clang emits the host CPU's
         * widest SIMD (AVX2/AVX-512). Without it clang targets baseline x86-64
         * = SSE2 (128-bit) only, which is ~4x narrower and loses to -O3
         * -march=native C on vectorisable loops. -fno-plt for PLT-free calls. */
        snprintf(cmd, sizeof(cmd),
                 "clang -O3 -march=native -fno-plt -target %s \"%s\" -o \"%s\"",
                 llvm_triple, ll_path, out_path);
    } else {
        /* Cross targets: use --target= (clang driver spelling for cross) */
        snprintf(cmd, sizeof(cmd),
                 "clang -O3 --target=%s \"%s\" -o \"%s\"",
                 llvm_triple, ll_path, out_path);
    }

    int r = system(cmd);

    /* Remove intermediate .ll file unless --emit-ir was requested */
    if (!emit_ir) {
        remove(ll_path);
    } else {
        fprintf(stderr, "IR written to: %s\n", ll_path);
    }

    free(src);
    arena_free(&a);

    if (r != 0) {
        fprintf(stderr, "Compilation failed (target: %s)\n", llvm_triple);
        if (target == TARGET_AARCH64_LINUX || target == TARGET_AARCH64_MACOS ||
            target == TARGET_X86_64_WINDOWS) {
            fprintf(stderr,
                "Cross-compilation requires the clang cross-compilation toolchain\n"
                "and a matching sysroot.  Install the relevant cross packages and\n"
                "set CFLAGS=--sysroot=<path> as needed.  See docs/decisions.md.\n");
        }
        return 1;
    }
    return 0;
}
