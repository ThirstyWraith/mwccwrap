/*
 * mwccwrap.c - Command-line host for CodeWarrior compiler DLLs
 *
 * Behavior and flags intended to mirror the official MWCC command-line tools as closely as possible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <windows.h>
#include <winver.h>
#include "host_ctx.h"

#define MWCCWRAP_VERSION "1.0"
#define MAX_INCLUDE_PATHS 64
#define MAX_SOURCE_FILES 256

static const char* const kKnownCompilerDllNames[] = {
    "cc_mips.dll",
    "ppc_eabi.dll"
};

#define KNOWN_COMPILER_DLL_COUNT ((int)(sizeof(kKnownCompilerDllNames) / sizeof(kKnownCompilerDllNames[0])))

/* Plugin entry point type */
typedef short (__stdcall *PluginMainFunc)(CWPluginContext context);

/* PluginLib string table init */
typedef void (__cdecl *MWCC_InitStringTableFunc)(HMODULE);

/* PluginLib handle */
static HMODULE g_registered_pluginlib_module = NULL;
static int g_registered_pluginlib_version = 0;

__declspec(dllexport) void __cdecl MWCC_RegisterPluginLib(HMODULE module, int version) {
    g_registered_pluginlib_module = module;
    g_registered_pluginlib_version = version;
}

static void copy_cstr(char* dst, size_t dst_size, const char* src);

typedef struct CompilerVersionInfo {
    char product_name[128];
    char file_description[128];
    char company_name[128];
    char legal_copyright[160];
    char product_version[64];
    char file_version[64];
    char special_build[128];
    DWORD version_ms;
    DWORD version_ls;
    int has_fixed_version;
    DWORD link_timestamp;
    int has_link_timestamp;
} CompilerVersionInfo;

/* ============================================================
 * Help & Version
 * ============================================================ */

static void print_help(void) {
    fprintf(stderr,
        "mwccwrap v" MWCCWRAP_VERSION " - CodeWarrior compiler DLL wrapper\n"
        "\n"
        "Usage: mwccwrap [options] [-o output] input1.c [input2.c ...]\n"
        "\n"
        "General:\n"
        "  -help                  Display this help\n"
        "  -version               Display version information from compiler DLL\n"
        "  -v, -verbose           Verbose output (show plugin callbacks)\n"
        "  -c, -nolink            Compile only (implicit, no linker)\n"
        "  -dll <path>            Compiler DLL path/name\n"
        "                         (default: auto-search known compiler DLLs)\n"
        "                         Known names: cc_mips.dll, ppc_eabi.dll\n"
        "  -msgstyle std|gcc|parseable  Message format style\n"
        "  -maxerrors <N>         Stop after N errors (0=unlimited)\n"
        "  -maxwarnings <N>       Stop after N warnings (0=unlimited)\n"
        "  -nofail                Continue compiling later files after failures\n"
        "\n"
        "Preprocessing/Input:\n"
        "  -o <file>              Output file (default: input.o)\n"
        "  -E                     Preprocess only (to stdout)\n"
        "  -EP                    Preprocess, strip #line directives\n"
        "  -P                     Preprocess to file\n"
        "  -M / -MM / -make       Emit Makefile dependencies (no object code)\n"
        "  -MD / -MMD             Emit dependencies to .d file and compile object\n"
        "  -dis, -disassemble     Disassemble to stdout\n"
        "  -S                     Disassemble to file\n"
        "  -D <name>[=<value>]    Define preprocessor macro\n"
        "  -U <name>              Undefine preprocessor macro\n"
        "  -I <path>              Add user include path\n"
        "  -i- / -I-              Switch subsequent -I to system paths\n"
        "  -ir <path>             Add recursive include path\n"
        "  -prefix <file>         Prefix file onto all source files\n"
        "  -include <file>        Same as -prefix\n"
        "  -pragma \"<text>\"       Inject #pragma directive\n"
        "  -nosyspath             Treat <> includes like \"\" includes\n"
        "  -gccincludes           GCC-style include semantics\n"
        "  -cwd proj|source|explicit|include\n"
        "                         #include search semantics\n"
        "  -stdinc / -nostdinc    Enable/disable %%MWCIncludes%% defaults\n"
        "  -defaults / -nodefaults  Alias for [no]stdinc\n"
        "  -search                Search access paths for source file args\n"
        "\n"
        "C/C++ Language:\n"
        "  -lang c|c++|ec++       Source language\n"
        "  -dialect c|c++         Source language (alias)\n"
        "  -char signed|unsigned  Default char signedness (default: signed)\n"
        "  -enum min|int          Enum sizing (default: int)\n"
        "  -inline on|smart|off|none|auto|noauto|all|deferred|level=<N>\n"
        "                         Inlining control (default: on)\n"
        "  -bool on|off           Enable bool/true/false (default: on)\n"
        "  -Cpp_exceptions on|off Enable C++ exceptions (default: on)\n"
        "  -RTTI on|off           Enable runtime type info (default: on)\n"
        "  -ARM on|off            ARM conformance checking (default: off)\n"
        "  -ansi off|on|relaxed|strict  ANSI conformance level\n"
        "  -strict on|off         Strict ANSI checking (default: off)\n"
        "  -trigraphs on|off      Enable trigraphs (default: off)\n"
        "  -stdkeywords on|off    Restrict to standard keywords (default: off)\n"
        "  -wchar_t on|off        wchar_t as built-in type (default: off)\n"
        "  -r, -requireprotos     Require function prototypes\n"
        "  -str[ings] [no]reuse|[no]pool|[no]readonly\n"
        "                         String constant handling\n"
        "  -multibyte[aware]      Enable multibyte character support\n"
        "  -once                  Prevent repeated header processing\n"
        "  -relax_pointers        Relax pointer type checking\n"
        "\n"
        "Warnings:\n"
        "  -w off                 Disable all warnings\n"
        "  -w on                  Enable default warnings\n"
        "  -w all                 Enable all warnings, require prototypes\n"
        "  -w [no]error           Treat warnings as errors\n"
        "  -w [no]pragmas         Illegal #pragmas\n"
        "  -w [no]empty           Empty declarations\n"
        "  -w [no]possible        Possible unwanted effects\n"
        "  -w [no]unusedarg       Unused arguments\n"
        "  -w [no]unusedvar       Unused variables\n"
        "  -w [no]unused          All unused (arg+var)\n"
        "  -w [no]extracomma      Extra commas\n"
        "  -w [no]pedantic        Pedantic error checking\n"
        "  -w [no]hidevirtual     Hidden virtual functions\n"
        "  -w [no]implicit        Implicit arithmetic conversions\n"
        "  -w [no]notinlined      Inline functions not inlined\n"
        "  -w [no]largeargs       Large args to unprototyped functions\n"
        "  -w [no]structclass     Inconsistent struct/class usage\n"
        "  -Wall / -Werror        GCC-compatible aliases\n"
        "\n"
        "MIPS Backend:\n"
        "  -fp off|single         Floating-point options (default: single)\n"
        "  -profile               Enable calls to profiler\n"
        "\n"
        "Optimizer:\n"
        "  -O0                    Same as -opt off\n"
        "  -O1                    Same as -opt level=1\n"
        "  -O2, -O                Same as -opt level=2\n"
        "  -O3                    Same as -opt level=3\n"
        "  -O4                    Same as -opt level=4\n"
        "  -Os                    Same as -opt space\n"
        "  -Op                    Same as -opt speed\n"
        "  -opt off|on|all|full   Optimization level\n"
        "  -opt speed|space       Optimization target\n"
        "  -opt level=<N>         Set optimization level (0-4)\n"
        "  -opt [no]intrinsics    Inline intrinsic functions\n"
        "  -opt [no]peephole      Peephole optimization\n"
        "\n"
        "Debug:\n"
        "  -g                     Generate debug info (same as -sym full)\n"
        "  -sym off|on|full       Debug symbol control\n"
        "\n"
    );
}

static void print_wrapper_version(void) {
    fprintf(stderr,
        "mwccwrap v" MWCCWRAP_VERSION " - CodeWarrior compiler DLL wrapper\n"
        "Use -version to query version metadata from the selected compiler DLL.\n"
    );
}

static int read_pe_link_timestamp(const char* path, DWORD* out_timestamp) {
    FILE* f = fopen(path, "rb");
    IMAGE_DOS_HEADER dos;
    DWORD nt_sig;
    IMAGE_FILE_HEADER file_hdr;

    if (!out_timestamp) return 0;
    *out_timestamp = 0;
    if (!f) return 0;

    if (fread(&dos, 1, sizeof(dos), f) != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        fclose(f);
        return 0;
    }
    if (fseek(f, dos.e_lfanew, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    if (fread(&nt_sig, 1, sizeof(nt_sig), f) != sizeof(nt_sig) || nt_sig != IMAGE_NT_SIGNATURE) {
        fclose(f);
        return 0;
    }
    if (fread(&file_hdr, 1, sizeof(file_hdr), f) != sizeof(file_hdr)) {
        fclose(f);
        return 0;
    }

    *out_timestamp = file_hdr.TimeDateStamp;
    fclose(f);
    return 1;
}

static int query_version_string(const void* version_blob,
                                WORD lang,
                                WORD codepage,
                                const char* key,
                                char* out,
                                size_t out_size)
{
    char query[128];
    LPSTR value = NULL;
    UINT value_len = 0;

    if (!version_blob || !key || !out || out_size == 0) return 0;
    out[0] = '\0';

    snprintf(query, sizeof(query), "\\StringFileInfo\\%04x%04x\\%s",
             (unsigned int)lang, (unsigned int)codepage, key);

    if (!VerQueryValueA((LPVOID)version_blob, query, (LPVOID*)&value, &value_len) ||
        !value || value_len == 0 || value[0] == '\0')
    {
        return 0;
    }

    copy_cstr(out, out_size, value);
    return 1;
}

static int get_compiler_version_info(const char* dll_path, CompilerVersionInfo* out_info) {
    DWORD dummy = 0;
    DWORD info_size;
    void* info_blob = NULL;
    VS_FIXEDFILEINFO* ffi = NULL;
    UINT ffi_len = 0;
    struct LangCodePage {
        WORD lang;
        WORD codepage;
    };
    struct LangCodePage* translations = NULL;
    UINT translations_len = 0;
    struct LangCodePage probes[4];
    int probe_count = 0;
    int found_any = 0;

    if (!out_info) return 0;
    memset(out_info, 0, sizeof(*out_info));
    if (!dll_path || !dll_path[0]) return 0;

    info_size = GetFileVersionInfoSizeA(dll_path, &dummy);
    if (info_size == 0) return 0;

    info_blob = malloc(info_size);
    if (!info_blob) return 0;
    if (!GetFileVersionInfoA(dll_path, 0, info_size, info_blob)) {
        free(info_blob);
        return 0;
    }

    if (VerQueryValueA(info_blob, "\\", (LPVOID*)&ffi, &ffi_len) &&
        ffi && ffi_len >= sizeof(VS_FIXEDFILEINFO) &&
        ffi->dwSignature == 0xFEEF04BD)
    {
        out_info->version_ms = ffi->dwFileVersionMS;
        out_info->version_ls = ffi->dwFileVersionLS;
        out_info->has_fixed_version = 1;
        found_any = 1;
    }

    if (VerQueryValueA(info_blob, "\\VarFileInfo\\Translation", (LPVOID*)&translations, &translations_len) &&
        translations && translations_len >= sizeof(*translations))
    {
        probes[probe_count++] = translations[0];
    }

    probes[probe_count++] = (struct LangCodePage){0x0409, 0x04B0};
    probes[probe_count++] = (struct LangCodePage){0x0409, 0x04E4};
    probes[probe_count++] = (struct LangCodePage){0x0409, 0x0000};

    for (int i = 0; i < probe_count; i++) {
        if (!out_info->product_name[0] &&
            query_version_string(info_blob, probes[i].lang, probes[i].codepage,
                                 "ProductName",
                                 out_info->product_name, sizeof(out_info->product_name))) {
            found_any = 1;
        }
        if (!out_info->file_description[0] &&
            query_version_string(info_blob, probes[i].lang, probes[i].codepage,
                                 "FileDescription",
                                 out_info->file_description, sizeof(out_info->file_description))) {
            found_any = 1;
        }
        if (!out_info->company_name[0] &&
            query_version_string(info_blob, probes[i].lang, probes[i].codepage,
                                 "CompanyName",
                                 out_info->company_name, sizeof(out_info->company_name))) {
            found_any = 1;
        }
        if (!out_info->legal_copyright[0] &&
            query_version_string(info_blob, probes[i].lang, probes[i].codepage,
                                 "LegalCopyright",
                                 out_info->legal_copyright, sizeof(out_info->legal_copyright))) {
            found_any = 1;
        }
        if (!out_info->product_version[0] &&
            query_version_string(info_blob, probes[i].lang, probes[i].codepage,
                                 "ProductVersion",
                                 out_info->product_version, sizeof(out_info->product_version))) {
            found_any = 1;
        }
        if (!out_info->file_version[0] &&
            query_version_string(info_blob, probes[i].lang, probes[i].codepage,
                                 "FileVersion",
                                 out_info->file_version, sizeof(out_info->file_version))) {
            found_any = 1;
        }
        if (!out_info->special_build[0] &&
            query_version_string(info_blob, probes[i].lang, probes[i].codepage,
                                 "SpecialBuild",
                                 out_info->special_build, sizeof(out_info->special_build))) {
            found_any = 1;
        }
    }

    if (read_pe_link_timestamp(dll_path, &out_info->link_timestamp) &&
        out_info->link_timestamp != 0)
    {
        out_info->has_link_timestamp = 1;
        found_any = 1;
    }

    free(info_blob);
    return found_any;
}

static int should_enable_mips_r4_compat(const CompilerVersionInfo* info) {
    unsigned int major, minor, patch, build;

    if (!info || !info->has_fixed_version) return 0;

    major = (unsigned int)HIWORD(info->version_ms);
    minor = (unsigned int)LOWORD(info->version_ms);
    patch = (unsigned int)HIWORD(info->version_ls);
    build = (unsigned int)LOWORD(info->version_ls);

    if (!(major == 2 && minor == 44 && patch == 14 && build == 0)) return 0;

    if (info->file_description[0] &&
        _stricmp(info->file_description, "MIPS Compiler for PlayStation") != 0)
    {
        return 0;
    }

    return 1;
}

static void format_version_line(const CompilerVersionInfo* info, char* out, size_t out_size) {
    unsigned int major, minor, patch, build;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!info) return;

    if (info->has_fixed_version) {
        major = (unsigned int)HIWORD(info->version_ms);
        minor = (unsigned int)LOWORD(info->version_ms);
        patch = (unsigned int)HIWORD(info->version_ls);
        build = (unsigned int)LOWORD(info->version_ls);
        snprintf(out, out_size, "Version %u.%u.%u build %u", major, minor, patch, build);
        return;
    }

    if (info->product_version[0]) {
        snprintf(out, out_size, "Version %s", info->product_version);
    } else if (info->file_version[0]) {
        snprintf(out, out_size, "Version %s", info->file_version);
    }
}

static void format_timestamp_line(const CompilerVersionInfo* info, char* out, size_t out_size) {
    time_t ts;
    struct tm* utc_tm;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!info || !info->has_link_timestamp) return;

    ts = (time_t)info->link_timestamp;
    utc_tm = gmtime(&ts);
    if (!utc_tm) return;
    if (strftime(out, out_size, "%b %d %Y %H:%M:%S UTC", utc_tm) == 0) {
        out[0] = '\0';
    }
}

static void print_ascii_line(const char* text) {
    char buf[320];
    size_t j = 0;

    if (!text) return;
    for (size_t i = 0; text[i] && j + 1 < sizeof(buf); i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == 0xA9) {
            if (j + 3 < sizeof(buf)) {
                buf[j++] = '(';
                buf[j++] = 'c';
                buf[j++] = ')';
            }
        } else if (ch >= 32 && ch <= 126) {
            buf[j++] = (char)ch;
        } else {
            buf[j++] = '?';
        }
    }
    buf[j] = '\0';
    fprintf(stderr, "%s\n", buf);
}

static void print_dynamic_version(const char* dll_path, const CompilerVersionInfo* info) {
    char version_line[96];
    char timestamp_line[96];
    const char* title = NULL;

    if (info) {
        if (info->file_description[0]) title = info->file_description;
        else if (info->product_name[0]) title = info->product_name;
    }

    if (title) print_ascii_line(title);
    else fprintf(stderr, "Metrowerks C/C++ Compiler\n");

    if (info && info->legal_copyright[0]) print_ascii_line(info->legal_copyright);

    format_version_line(info, version_line, sizeof(version_line));
    if (version_line[0]) fprintf(stderr, "%s\n", version_line);

    if (info && info->special_build[0]) fprintf(stderr, "Special Build: %s\n", info->special_build);

    format_timestamp_line(info, timestamp_line, sizeof(timestamp_line));
    if (timestamp_line[0]) fprintf(stderr, "Runtime Built: %s\n", timestamp_line);

    if (dll_path && dll_path[0]) fprintf(stderr, "Compiler DLL: %s\n", dll_path);
}

static const char* basename_from_path(const char* path) {
    const char* slash;
    const char* bslash;
    const char* base;

    if (!path || !path[0]) return "mwccwrap.exe";
    slash = strrchr(path, '/');
    bslash = strrchr(path, '\\');
    base = slash;
    if (!base || (bslash && bslash > base)) base = bslash;
    return base ? (base + 1) : path;
}

/* ============================================================
 * Define/Pragma text buffer helpers
 * ============================================================ */

static void append_define_text(CWPluginContext ctx, const char* text) {
    if (!ctx->defineText) {
        ctx->defineText = (char*)calloc(1, DEFINE_TEXT_MAX);
        if (!ctx->defineText) return;
        ctx->defineTextLen = 0;
    }
    int len = (int)strlen(text);
    if (ctx->defineTextLen + len + 1 >= DEFINE_TEXT_MAX) {
        fprintf(stderr, "warning: define text buffer full, ignoring: %s", text);
        return;
    }
    memcpy(ctx->defineText + ctx->defineTextLen, text, len);
    ctx->defineTextLen += len;
    ctx->defineText[ctx->defineTextLen] = '\0';
}

/* -D name or -D name=value */
static void add_define(CWPluginContext ctx, const char* arg) {
    char buf[1024];
    const char* eq = strchr(arg, '=');
    if (eq) {
        snprintf(buf, sizeof(buf), "#define %.*s %s\n", (int)(eq - arg), arg, eq + 1);
    } else {
        snprintf(buf, sizeof(buf), "#define %s 1\n", arg);
    }
    append_define_text(ctx, buf);
}

/* -U name */
static void add_undef(CWPluginContext ctx, const char* name) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "#undef %s\n", name);
    append_define_text(ctx, buf);
}

/* -pragma "text" */
static void add_pragma(CWPluginContext ctx, const char* text) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "#pragma %s\n", text);
    append_define_text(ctx, buf);
}

/* -prefix / -include */
static void add_prefix_include(CWPluginContext ctx, const char* filename) {
    char buf[MAX_PATH + 32];
    size_t len;
    int copy_len;

    if (!filename || !filename[0]) return;

    len = strlen(filename);
    copy_len = (len > (size_t)(MAX_PATH - 1)) ? (MAX_PATH - 1) : (int)len;
    if (filename[0] == '<' && len > 1 && filename[len - 1] == '>') {
        snprintf(buf, sizeof(buf), "#include %.*s\n", copy_len, filename);
    } else {
        snprintf(buf, sizeof(buf), "#include \"%.*s\"\n", copy_len, filename);
    }
    append_define_text(ctx, buf);
}

static void set_oldprefixname(PFrontEndC* fe, const char* name) {
    size_t len;

    memset(fe->oldprefixname, 0, sizeof(fe->oldprefixname));
    if (!name || !name[0]) return;

    /*
     * MWCC stores oldprefixname as a Str31 (Pascal string).
     */
    len = strlen(name);
    if (len > 31) len = 31;
    fe->oldprefixname[0] = (unsigned char)len;
    memcpy(fe->oldprefixname + 1, name, len);
}

/* ============================================================
 * Warning flag helpers
 * ============================================================ */

static void set_all_warnings(PWarningC* w, Boolean val) {
    w->warn_illpragma    = val;
    w->warn_emptydecl    = val;
    w->warn_possunwant   = val;
    w->warn_unusedvar    = val;
    w->warn_unusedarg    = val;
    w->warn_extracomma   = val;
    w->pedantic          = val;
    w->warn_hidevirtual  = val;
    w->warn_implicitconv = val;
    w->warn_notinlined   = val;
    w->warn_structclass  = val;
    w->warn_missingreturn = val;
    w->warn_no_side_effect = val;
    w->warn_resultnotused = val;
    w->warn_padding = val;
    w->warn_impl_i2f_conv = val;
    w->warn_impl_f2i_conv = val;
    w->warn_impl_s2u_conv = val;
    w->warn_illtokenpasting = val;
    w->warn_filenamecaps = val;
    w->warn_filenamecapssystem = val;
    w->warn_undefmacro = val;
    w->warn_ptrintconv = val;
}

/* ============================================================
 * Inline parsing helper
 * ============================================================ */

static int parse_onoff(const char* val) {
    if (strcmp(val, "on") == 0)  return 1;
    if (strcmp(val, "off") == 0) return 0;
    return -1;
}

static void copy_cstr(char* dst, size_t dst_size, const char* src) {
    size_t i = 0;
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

typedef struct PendingIncludePath {
    char path[MAX_PATH];
    Boolean system;
    Boolean recursive;
} PendingIncludePath;

static void add_include_path(PendingIncludePath include_paths[],
                             int* num_includes,
                             const char* path,
                             Boolean system,
                             Boolean recursive)
{
    if (*num_includes >= MAX_INCLUDE_PATHS || !path || !path[0]) return;
    copy_cstr(include_paths[*num_includes].path, MAX_PATH, path);
    include_paths[*num_includes].system = system;
    include_paths[*num_includes].recursive = recursive;
    (*num_includes)++;
}

static void move_pending_system_paths_to_user(PendingIncludePath include_paths[],
                                              int num_includes)
{
    for (int i = 0; i < num_includes; i++) {
        if (include_paths[i].system) {
            include_paths[i].system = FALSE;
        }
    }
}

static void move_pending_user_paths_to_system(PendingIncludePath include_paths[],
                                              int num_includes)
{
    for (int i = 0; i < num_includes; i++) {
        include_paths[i].system = TRUE;
    }
}

static int parse_ppc_processor(const char* name, SInt16* out_value) {
    struct ProcessorNameMap {
        const char* name;
        SInt16 value;
    };
    static const struct ProcessorNameMap kMap[] = {
        {"401", 0x00}, {"403", 0x01}, {"505", 0x02}, {"509", 0x03},
        {"555", 0x04}, {"556", 0x19}, {"565", 0x1A},
        {"601", 0x05}, {"602", 0x06}, {"603", 0x07}, {"603e", 0x08},
        {"604", 0x09}, {"604e", 0x0A},
        {"740", 0x0B}, {"750", 0x0C}, {"7400", 0x15}, {"7450", 0x18},
        {"801", 0x0D}, {"821", 0x0E}, {"823", 0x0F},
        {"8240", 0x12}, {"8260", 0x13},
        {"850", 0x10}, {"860", 0x11},
        {"gekko", 0x16}, {"e500", 0x17},
        {"generic", 0x14}
    };
    if (!name || !out_value) return 0;
    for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) {
        if (_stricmp(name, kMap[i].name) == 0) {
            *out_value = kMap[i].value;
            return 1;
        }
    }
    return 0;
}

static int set_ppc_alignment(PPCEABICodeGen* ppc, const char* value) {
    if (!ppc || !value) return 0;
    /* MWCC AlignMode enum:
     * 0=mac68k, 1=mac68k4byte, 2=powerpc, 3=1-byte, 4=2-byte,
     * 5=4-byte, 6=8-byte, 7=16-byte, 8=packed.
     */
    if (_stricmp(value, "power") == 0 || _stricmp(value, "powerpc") == 0 ||
        _stricmp(value, "ppc") == 0) {
        ppc->structalignment = 2;
        return 1;
    }
    if (_stricmp(value, "mac68k") == 0) {
        ppc->structalignment = 0;
        return 1;
    }
    if (_stricmp(value, "mac68k4byte") == 0) {
        ppc->structalignment = 1;
        return 1;
    }
    if (strcmp(value, "1") == 0 || _stricmp(value, "1byte") == 0) {
        ppc->structalignment = 3;
        return 1;
    }
    if (strcmp(value, "2") == 0 || _stricmp(value, "2byte") == 0) {
        ppc->structalignment = 4;
        return 1;
    }
    if (strcmp(value, "4") == 0 || _stricmp(value, "4byte") == 0) {
        ppc->structalignment = 5;
        return 1;
    }
    if (strcmp(value, "8") == 0 || _stricmp(value, "8byte") == 0) {
        ppc->structalignment = 6;
        return 1;
    }
    if (strcmp(value, "16") == 0 || _stricmp(value, "16byte") == 0) {
        ppc->structalignment = 7;
        return 1;
    }
    if (_stricmp(value, "packed") == 0) {
        ppc->structalignment = 8;
        return 1;
    }
    if (_stricmp(value, "array") == 0 || _stricmp(value, "arraymembers") == 0) {
        /* Stored in parser-side options on newer compilers; no direct panel bit in PBackEnd. */
        return 1;
    }
    return 0;
}

static int apply_ppc_string_option(PFrontEndC* fe, PPCEABICodeGen* ppc, const char* token) {
    if (!fe || !ppc || !token || !token[0]) return 0;

    if (_stricmp(token, "reuse") == 0) {
        fe->dontreusestrings = 0;
        return 1;
    }
    if (_stricmp(token, "noreuse") == 0) {
        fe->dontreusestrings = 1;
        return 1;
    }
    if (_stricmp(token, "pool") == 0) {
        fe->poolstrings = 1;
        return 1;
    }
    if (_stricmp(token, "nopool") == 0) {
        fe->poolstrings = 0;
        return 1;
    }
    if (_stricmp(token, "readonly") == 0) {
        ppc->readonlystrings = 1;
        return 1;
    }
    if (_stricmp(token, "noreadonly") == 0) {
        ppc->readonlystrings = 0;
        return 1;
    }
    return 0;
}

static void apply_dash_o_token(const char* token,
                               PGlobalOptimizer* opt,
                               PMIPSCodeGen* mips,
                               PPCEABICodeGen* ppc,
                               int* handled)
{
    if (!token || !token[0]) return;

    if (strcmp(token, "0") == 0) {
        opt->optimizationlevel = 0;
        return;
    }
    if (strcmp(token, "1") == 0) {
        opt->optimizationlevel = 1;
        return;
    }
    if (strcmp(token, "2") == 0) {
        opt->optimizationlevel = 2;
        mips->peephole = 1;
        ppc->peephole = 1;
        return;
    }
    if (strcmp(token, "3") == 0) {
        opt->optimizationlevel = 3;
        mips->peephole = 1;
        ppc->peephole = 1;
        return;
    }
    if (strcmp(token, "4") == 0) {
        opt->optimizationlevel = 4;
        mips->peephole = 1;
        ppc->peephole = 1;
        ppc->scheduling = 1;
        return;
    }
    if (strcmp(token, "p") == 0) {
        opt->optfor = 2;
        return;
    }
    if (strcmp(token, "s") == 0) {
        opt->optfor = 1;
        return;
    }
    if (handled) *handled = 0;
}

static int parse_dash_o_option(const char* arg,
                               PGlobalOptimizer* opt,
                               PMIPSCodeGen* mips,
                               PPCEABICodeGen* ppc)
{
    const char* spec;
    int handled = 1;

    if (!arg || strncmp(arg, "-O", 2) != 0) return 0;
    spec = arg + 2;

    if (!spec[0]) {
        apply_dash_o_token("2", opt, mips, ppc, &handled);
        return handled;
    }

    if (strchr(spec, ',')) {
        char tmp[64];
        size_t len = strlen(spec);
        if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
        memcpy(tmp, spec, len);
        tmp[len] = '\0';

        char* tok = strtok(tmp, ",");
        while (tok) {
            if (tok[0]) {
                apply_dash_o_token(tok, opt, mips, ppc, &handled);
            }
            tok = strtok(NULL, ",");
        }
        return handled;
    }

    apply_dash_o_token(spec, opt, mips, ppc, &handled);
    return handled;
}

static int apply_opt_keyword(const char* token,
                             PGlobalOptimizer* opt,
                             PMIPSCodeGen* mips,
                             PPCEABICodeGen* ppc)
{
    if (!token || !token[0]) return 0;

    if (strcmp(token, "off") == 0 || strcmp(token, "none") == 0) {
        opt->optimizationlevel = 0;
        return 1;
    }
    if (strcmp(token, "on") == 0) {
        opt->optimizationlevel = 2;
        mips->peephole = 1;
        ppc->peephole = 1;
        return 1;
    }
    if (strcmp(token, "all") == 0 || strcmp(token, "full") == 0) {
        opt->optimizationlevel = 4;
        opt->optfor = 1;
        mips->useIntrinsics = 1;
        mips->peephole = 1;
        ppc->peephole = 1;
        ppc->scheduling = 1;
        return 1;
    }
    if (strcmp(token, "speed") == 0) {
        opt->optfor = 2;
        return 1;
    }
    if (strcmp(token, "space") == 0 || strcmp(token, "size") == 0) {
        opt->optfor = 1;
        return 1;
    }
    if (strncmp(token, "level=", 6) == 0 || strncmp(token, "l=", 2) == 0) {
        const char* num = strchr(token, '=') + 1;
        int lv = atoi(num);
        if (lv >= 0 && lv <= 4) {
            opt->optimizationlevel = (UInt8)lv;
            return 1;
        }
        return 0;
    }
    if (strcmp(token, "intrinsics") == 0) {
        mips->useIntrinsics = 1;
        return 1;
    }
    if (strcmp(token, "nointrinsics") == 0) {
        mips->useIntrinsics = 0;
        return 1;
    }
    if (strcmp(token, "peephole") == 0 || strcmp(token, "peep") == 0) {
        mips->peephole = 1;
        ppc->peephole = 1;
        return 1;
    }
    if (strcmp(token, "nopeephole") == 0 || strcmp(token, "nopeep") == 0) {
        mips->peephole = 0;
        ppc->peephole = 0;
        return 1;
    }
    if (strcmp(token, "schedule") == 0) {
        ppc->scheduling = 1;
        return 1;
    }
    if (strcmp(token, "noschedule") == 0) {
        ppc->scheduling = 0;
        return 1;
    }
    return 0;
}

static int is_drive_prefix(const char* token, int token_len, char next_ch) {
    return token_len == 1 &&
           isalpha((unsigned char)token[0]) &&
           (next_ch == '\\' || next_ch == '/');
}

static const char* find_default_include_env(const CWPluginContext ctx, const char** matched_name) {
    static const char* env_names_c[] = {
        "MWCIncludes"
    };
    static const char* env_names_cpp[] = {
        "MWCppIncludes",
        "MWCIncludes"
    };
    const char** env_names = env_names_c;
    size_t env_count = sizeof(env_names_c) / sizeof(env_names_c[0]);
    const char* last_name = NULL;

    if (ctx && ctx->prefsFrontEnd.cplusplus) {
        env_names = env_names_cpp;
        env_count = sizeof(env_names_cpp) / sizeof(env_names_cpp[0]);
    }

    for (size_t i = 0; i < env_count; i++) {
        const char* name = env_names[i];
        const char* val = getenv(name);
        last_name = name;
        if (val) {
            if (matched_name) *matched_name = name;
            return val;
        }
    }

    if (matched_name) *matched_name = last_name;
    return NULL;
}

/*
 * MWCC uses AddAccessPathList(..., ':', ',', ...), while Windows users often
 * provide semicolon-separated lists. Accept ':', ';', and ',' separators.
 * A leading '+' on an entry means recursive access path.
 */
static void add_default_include_paths(CWPluginContext ctx,
                                      PendingIncludePath include_paths[],
                                      int* num_includes)
{
    const char* env_name = NULL;
    const char* env = find_default_include_env(ctx, &env_name);
    char token[MAX_PATH];
    int t = 0;

    if (!env) {
        if (env_name && env_name[0]) {
            fprintf(stderr, "warning: Environment variable '%s' not found\n", env_name);
        }
        return;
    }
    if (!env[0]) return;

    for (int i = 0;; i++) {
        char ch = env[i];
        char next = env[i + 1];
        int sep = 0;

        if (ch == '\0' || ch == ';' || ch == ',') {
            sep = 1;
        } else if (ch == ':') {
            sep = !is_drive_prefix(token, t, next);
        }

        if (sep) {
            token[t] = '\0';
            if (t > 0) {
                const char* path = token;
                Boolean recursive = FALSE;
                if (token[0] == '+') {
                    recursive = TRUE;
                    path++;
                }
                add_include_path(include_paths, num_includes, path, TRUE, recursive);
            }
            t = 0;
            if (ch == '\0') break;
            continue;
        }

        if (t + 1 < MAX_PATH) {
            token[t++] = ch;
        }
    }
}

/* ============================================================
 * File I/O helpers
 * ============================================================ */

static char* read_file(const char* path, SInt32* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    *out_size = (SInt32)size;
    return buf;
}

static int file_exists(const char* path) {
    DWORD attrs;
    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static int join_path(char* out, size_t out_size, const char* dir, const char* leaf) {
    int n;
    if (!out || out_size == 0 || !dir || !leaf) return 0;
    n = snprintf(out, out_size, "%s/%s", dir, leaf);
    return n > 0 && (size_t)n < out_size;
}

static int find_file_in_dir_recursive(const char* dir, const char* filename,
                                      char* outpath, size_t outsize, int depth)
{
    char candidate[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA ffd;
    HANDLE h;

    if (depth > 32) return 0;

    if (join_path(candidate, sizeof(candidate), dir, filename) && file_exists(candidate)) {
        copy_cstr(outpath, outsize, candidate);
        return 1;
    }

    if (!join_path(pattern, sizeof(pattern), dir, "*")) return 0;
    h = FindFirstFileA(pattern, &ffd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        char child[MAX_PATH];
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
        if (!join_path(child, sizeof(child), dir, ffd.cFileName)) continue;
        if (find_file_in_dir_recursive(child, filename, outpath, outsize, depth + 1)) {
            FindClose(h);
            return 1;
        }
    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    return 0;
}

static int find_file_in_access_paths(HostAccessPath* paths, SInt32 count,
                                     const char* filename, char* outpath, size_t outsize)
{
    for (SInt32 i = 0; i < count; i++) {
        if (paths[i].recursive) {
            if (find_file_in_dir_recursive(paths[i].path, filename, outpath, outsize, 0)) {
                return 1;
            }
        } else {
            char candidate[MAX_PATH];
            if (join_path(candidate, sizeof(candidate), paths[i].path, filename) &&
                file_exists(candidate))
            {
                copy_cstr(outpath, outsize, candidate);
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Convert line endings to \r (Mac convention).
 * cc_mips.dll is a Mac-heritage compiler that uses \r for line endings.
 * Without this conversion, \n is treated as whitespace and preprocessor
 * output loses line structure.
 *
 * Converts in-place: \r\n -> \r, \n -> \r (buffer can only shrink).
 * Returns the new size.
 */
static SInt32 convert_line_endings_to_cr(char* buf, SInt32 size) {
    SInt32 out = 0;
    for (SInt32 i = 0; i < size; i++) {
        if (buf[i] == '\r' && i + 1 < size && buf[i + 1] == '\n') {
            buf[out++] = '\r';
            i++; /* skip the \n */
        } else if (buf[i] == '\n') {
            buf[out++] = '\r';
        } else {
            buf[out++] = buf[i];
        }
    }
    buf[out] = '\0';
    return out;
}

static void write_object_file(CWPluginContext ctx) {
    if (!ctx->objectStored || !ctx->objectData || ctx->objectDataSize <= 0) {
        fprintf(stderr, "No object data to write.\n");
        return;
    }

    const char* outpath = ctx->outputFile[0] ? ctx->outputFile : "output.o";
    FILE* f = fopen(outpath, "wb");
    if (!f) {
        fprintf(stderr, "Cannot open output file: %s\n", outpath);
        return;
    }

    fwrite(ctx->objectData, 1, ctx->objectDataSize, f);
    fclose(f);

    if (ctx->verbose) {
        fprintf(stderr, "Wrote %d bytes to %s\n", ctx->objectDataSize, outpath);
    }
}

static int is_directory_path(const char* path) {
    DWORD attrs;
    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static const char* get_path_leaf(const char* path) {
    const char* slash = NULL;
    const char* backslash = NULL;
    if (!path) return "";
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (!slash && !backslash) return path;
    if (!slash) return backslash + 1;
    if (!backslash) return slash + 1;
    return (slash > backslash ? slash : backslash) + 1;
}

static void replace_extension(const char* path, const char* ext, char* out, size_t out_size) {
    const char* dot;
    const char* slash;
    const char* backslash;
    const char* sep;
    size_t stem_len;
    int n;

    if (!out || out_size == 0) return;
    if (!path || !path[0]) {
        out[0] = '\0';
        return;
    }

    dot = strrchr(path, '.');
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (slash && backslash) sep = (slash > backslash) ? slash : backslash;
    else if (slash) sep = slash;
    else sep = backslash;

    if (dot && (!sep || dot > sep)) {
        stem_len = (size_t)(dot - path);
    } else {
        stem_len = strlen(path);
    }

    n = snprintf(out, out_size, "%.*s%s", (int)stem_len, path, ext);
    if (n < 0 || (size_t)n >= out_size) {
        out[out_size - 1] = '\0';
    }
}

static void write_make_escaped(FILE* f, const char* path) {
    if (!f || !path) return;
    for (const unsigned char* p = (const unsigned char*)path; *p; p++) {
        if (*p == ' ') fputc('\\', f);
        fputc((int)*p, f);
    }
}

static int emit_dependency_rule(CWPluginContext ctx,
                                const char* source_path,
                                const char* object_path,
                                const char* output_path,
                                int append)
{
    FILE* out = stdout;
    HostFileRecord* deps = NULL;
    int dep_count = 0;
    int visible_deps = 0;
    int ret = 1;

    if (!ctx || !source_path || !source_path[0] || !object_path || !object_path[0]) {
        return 1;
    }

    if (output_path && output_path[0]) {
        out = fopen(output_path, append ? "ab" : "wb");
        if (!out) {
            fprintf(stderr, "Cannot open dependency output file: %s\n", output_path);
            return 0;
        }
    }

    if (ctx->fileRecordCount > 0) {
        deps = (HostFileRecord*)calloc((size_t)ctx->fileRecordCount, sizeof(HostFileRecord));
        if (!deps) {
            fprintf(stderr, "Out of memory while building dependency list\n");
            ret = 0;
            goto done;
        }
    }

    for (SInt32 i = 0; i < ctx->fileRecordCount; i++) {
        const HostFileRecord* rec = &ctx->fileRecords[i];
        int existing = -1;

        if (!rec->path[0]) continue;
        if (rec->fileID == 0) continue; /* main source tracked separately */
        if (strcmp(rec->path, source_path) == 0) continue;

        for (int j = 0; j < dep_count; j++) {
            if (strcmp(deps[j].path, rec->path) == 0) {
                existing = j;
                break;
            }
        }

        if (existing >= 0) {
            if (deps[existing].isSystem && !rec->isSystem) {
                deps[existing].isSystem = FALSE;
            }
            continue;
        }

        deps[dep_count].isSystem = rec->isSystem;
        copy_cstr(deps[dep_count].path, sizeof(deps[dep_count].path), rec->path);
        dep_count++;
    }

    for (int i = 0; i < dep_count; i++) {
        if (ctx->depsOnlyUserFiles && deps[i].isSystem) continue;
        visible_deps++;
    }

    write_make_escaped(out, object_path);
    fputs(": ", out);
    write_make_escaped(out, source_path);
    if (visible_deps > 0) fputs(" \\\n", out);
    else fputc('\n', out);

    for (int i = 0; i < dep_count; i++) {
        if (ctx->depsOnlyUserFiles && deps[i].isSystem) continue;
        visible_deps--;
        fputc('\t', out);
        write_make_escaped(out, deps[i].path);
        if (visible_deps > 0) fputs(" \\\n", out);
        else fputc('\n', out);
    }

done:
    free(deps);
    if (out != stdout) fclose(out);
    return ret;
}

/* ============================================================
 * Command-line define/pragma prefix handling
 * ============================================================ */

static void finalize_cmdline_prefix(CWPluginContext ctx) {
    if (!ctx->defineText || ctx->defineTextLen <= 0) {
        set_oldprefixname(&ctx->prefsFrontEnd, NULL);
        return;
    }

    /* Match compiler line-ending expectations for virtual prefix text. */
    ctx->defineTextLen = convert_line_endings_to_cr(ctx->defineText, ctx->defineTextLen);
    set_oldprefixname(&ctx->prefsFrontEnd, CMDLINE_DEFINES_VFILE);
}

/* ============================================================
 * Initialize preference defaults
 * ============================================================ */

static void init_pref_defaults(CWPluginContext ctx) {
    /* Shared front-end defaults. */
    memset(&ctx->prefsFrontEnd, 0, sizeof(PFrontEndC));
    ctx->prefsFrontEnd.version = 0x12;          /* struct version 18 (PPC era) */
    ctx->prefsFrontEnd.enumsalwaysint = 0;      /* -enum min (default) */
    ctx->prefsFrontEnd.wchar_type = 1;          /* -wchar_t on (default=1 on PPC) */
    ctx->prefsFrontEnd.enableexceptions = 1;    /* -Cpp_exceptions on */
    ctx->prefsFrontEnd.useRTTI = 1;             /* -RTTI on */
    ctx->prefsFrontEnd.booltruefalse = 1;       /* -bool on */
    ctx->prefsFrontEnd.inlinelevel = 8;         /* default inline depth */

    /* Shared warnings defaults */
    memset(&ctx->prefsWarnings, 0, sizeof(PWarningC));
    ctx->prefsWarnings.version = 7;

    /* Shared optimizer defaults */
    memset(&ctx->prefsOptimizer, 0, sizeof(PGlobalOptimizer));

    /* ---- MIPS-specific panels ---- */
    memset(&ctx->prefsMIPSCodeGen, 0, sizeof(PMIPSCodeGen));
    ctx->prefsMIPSCodeGen.fpuType = 1;          /* -fp single (default) */

    memset(&ctx->prefsMIPSLinker, 0, sizeof(PMIPSLinker));
    memset(&ctx->prefsMIPSProject, 0, sizeof(PMIPSProject));

    /* ---- PPC EABI-specific panels ---- */
    memset(&ctx->prefsPPCCodeGen, 0, sizeof(PPCEABICodeGen));
    ctx->prefsPPCCodeGen.version = 0x0C;        /* version 12 (enables processorname) */
    ctx->prefsPPCCodeGen.pooldata = 1;          /* -pooldata on (default) */
    ctx->prefsPPCCodeGen.floatingpoint = 1;     /* -fp soft (default) */
    ctx->prefsPPCCodeGen.processor = 0x14;      /* generic PPC */
    copy_cstr(ctx->prefsPPCCodeGen.processorname,
              sizeof(ctx->prefsPPCCodeGen.processorname), "generic");

    memset(&ctx->prefsPPCLinker, 0, sizeof(PPCEABILinker));
    memset(&ctx->prefsPPCProject, 0, sizeof(PPCEABIProject));
    ctx->prefsPPCProject.bigendian = 1;         /* -big (default) */
    ctx->prefsPPCProject.datathreshold = 8;     /* -sdata(threshold) default */
    ctx->prefsPPCProject.sdata2threshold = 8;   /* -sdata2(threshold) default */
    ctx->prefsPPCProject.codeModel = 0;         /* -model absolute (default) */

    memset(&ctx->prefsPreprocessor, 0, sizeof(PPreprocessor));
    ctx->prefsPreprocessor.version = 4;
}

static HMODULE load_compiler_dll(const char* requested_dll,
                                 char* loaded_dll_name,
                                 size_t loaded_dll_name_size,
                                 int verbose)
{
    HMODULE hDll;
    DWORD last_error = ERROR_MOD_NOT_FOUND;

    if (loaded_dll_name && loaded_dll_name_size > 0) {
        loaded_dll_name[0] = '\0';
    }

    if (requested_dll && requested_dll[0]) {
        if (verbose) fprintf(stderr, "Loading compiler DLL: %s\n", requested_dll);
        hDll = LoadLibraryA(requested_dll);
        if (!hDll) {
            return NULL;
        }
        if (loaded_dll_name && loaded_dll_name_size > 0) {
            copy_cstr(loaded_dll_name, loaded_dll_name_size, requested_dll);
        }
        return hDll;
    }

    for (int i = 0; i < KNOWN_COMPILER_DLL_COUNT; i++) {
        const char* candidate = kKnownCompilerDllNames[i];
        if (verbose) fprintf(stderr, "Trying compiler DLL: %s\n", candidate);
        hDll = LoadLibraryA(candidate);
        if (hDll) {
            if (loaded_dll_name && loaded_dll_name_size > 0) {
                copy_cstr(loaded_dll_name, loaded_dll_name_size, candidate);
            }
            return hDll;
        }
        last_error = GetLastError();
    }

    SetLastError(last_error);
    return NULL;
}

/* ============================================================
 * Argument parser
 * ============================================================ */

/* Helper: consume next arg or error */
#define NEXT_ARG(var) do { \
    if (i + 1 >= argc) { \
        fprintf(stderr, "Error: %s requires an argument\n", argv[i]); \
        return 1; \
    } \
    var = argv[++i]; \
} while(0)

static int parse_args(int argc, char* argv[], CWPluginContext ctx,
                      const char** requested_dll,
                      int* show_version_only,
                      const char* source_files[], int* num_sources,
                      const char** output_file,
                      PendingIncludePath include_paths[], int* num_includes,
                      int* system_path_mode)
{
    PFrontEndC* fe = &ctx->prefsFrontEnd;
    PWarningC* warn = &ctx->prefsWarnings;
    PGlobalOptimizer* opt = &ctx->prefsOptimizer;
    PMIPSCodeGen* cg = &ctx->prefsMIPSCodeGen;
    PPCEABICodeGen* ppc = &ctx->prefsPPCCodeGen;
    const char* arg_val;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        /* Not a flag - source file */
        if (arg[0] != '-') {
            if (*num_sources >= MAX_SOURCE_FILES) {
                fprintf(stderr, "Error: too many input files (max %d)\n", MAX_SOURCE_FILES);
                return 1;
            }
            source_files[*num_sources] = arg;
            (*num_sources)++;
            continue;
        }

        /* --- General --- */
        if (strcmp(arg, "-help") == 0 || strcmp(arg, "--help") == 0 ||
            strcmp(arg, "-h") == 0) {
            print_help();
            exit(0);
        }
        else if (strcmp(arg, "-version") == 0 || strcmp(arg, "--version") == 0) {
            *show_version_only = 1;
        }
        else if (strcmp(arg, "-v") == 0 || strcmp(arg, "-verbose") == 0) {
            ctx->verbose = 1;
        }
        else if (strcmp(arg, "-c") == 0 || strcmp(arg, "-nolink") == 0) {
            /* implicit - we never link */
        }
        else if (strcmp(arg, "-dll") == 0) {
            NEXT_ARG(*requested_dll);
        }
        else if (strcmp(arg, "-msgstyle") == 0) {
            NEXT_ARG(arg_val);
            if (strcmp(arg_val, "std") == 0)       ctx->msgStyle = 0;
            else if (strcmp(arg_val, "gcc") == 0)   ctx->msgStyle = 1;
            else if (strcmp(arg_val, "parseable") == 0) ctx->msgStyle = 2;
            else fprintf(stderr, "warning: unknown -msgstyle: %s\n", arg_val);
        }
        else if (strcmp(arg, "-maxerrors") == 0) {
            NEXT_ARG(arg_val);
            ctx->maxErrors = atoi(arg_val);
        }
        else if (strcmp(arg, "-maxwarnings") == 0) {
            NEXT_ARG(arg_val);
            ctx->maxWarnings = atoi(arg_val);
        }
        else if (strcmp(arg, "-nofail") == 0) {
            ctx->noFail = 1;
        }

        /* --- Output --- */
        else if (strcmp(arg, "-o") == 0) {
            NEXT_ARG(*output_file);
        }

        /* --- Preprocessing --- */
        else if (strcmp(arg, "-E") == 0) {
            ctx->preprocess = 1;
        }
        else if (strcmp(arg, "-M") == 0 || strcmp(arg, "-make") == 0) {
            ctx->dependencyMode = 1;
            ctx->depsOnlyUserFiles = 0;
        }
        else if (strcmp(arg, "-MM") == 0) {
            ctx->dependencyMode = 1;
            ctx->depsOnlyUserFiles = 1;
        }
        else if (strcmp(arg, "-MD") == 0) {
            ctx->dependencyMode = 2;
            ctx->depsOnlyUserFiles = 0;
        }
        else if (strcmp(arg, "-MMD") == 0) {
            ctx->dependencyMode = 2;
            ctx->depsOnlyUserFiles = 1;
        }
        else if (strcmp(arg, "-P") == 0 || strcmp(arg, "-preprocess") == 0) {
            ctx->preprocess = 1;
            ctx->preprocessOnly = 1;
        }
        else if (strcmp(arg, "-EP") == 0) {
            ctx->preprocess = 1;
        }
        else if (strcmp(arg, "-dis") == 0 || strcmp(arg, "-disassemble") == 0) {
            ctx->disassemble = 1;
        }
        else if (strcmp(arg, "-S") == 0) {
            ctx->disassemble = 1;
            ctx->disassembleToFile = 1;
        }

        /* -D name or -Dname or -D name=val or -Dname=val */
        else if (strcmp(arg, "-D") == 0) {
            NEXT_ARG(arg_val);
            add_define(ctx, arg_val);
        }
        else if (strncmp(arg, "-D", 2) == 0 && arg[2]) {
            add_define(ctx, arg + 2);
        }

        /* -U name or -Uname */
        else if (strcmp(arg, "-U") == 0) {
            NEXT_ARG(arg_val);
            add_undef(ctx, arg_val);
        }
        else if (strncmp(arg, "-U", 2) == 0 && arg[2]) {
            add_undef(ctx, arg + 2);
        }

        /* -pragma "text" */
        else if (strcmp(arg, "-pragma") == 0) {
            NEXT_ARG(arg_val);
            add_pragma(ctx, arg_val);
        }

        /* Include paths */
        else if (strcmp(arg, "-I-") == 0 || strcmp(arg, "-i-") == 0) {
            if (ctx->gccIncludes && !ctx->usedDashIMinus) {
                move_pending_system_paths_to_user(include_paths, *num_includes);
            }
            ctx->usedDashIMinus = TRUE;
            ctx->includeSearchMode = hostIncludeSearchExplicit;
            *system_path_mode = 1;
        }
        else if (strcmp(arg, "-ir") == 0) {
            NEXT_ARG(arg_val);
            add_include_path(include_paths, num_includes, arg_val,
                             (Boolean)*system_path_mode, TRUE);
        }
        else if (strncmp(arg, "-ir", 3) == 0 && arg[3]) {
            add_include_path(include_paths, num_includes, arg + 3,
                             (Boolean)*system_path_mode, TRUE);
        }
        else if (strcmp(arg, "-I") == 0) {
            NEXT_ARG(arg_val);
            add_include_path(include_paths, num_includes, arg_val,
                             (Boolean)*system_path_mode, FALSE);
        }
        else if (strcmp(arg, "-i") == 0) {
            NEXT_ARG(arg_val);
            add_include_path(include_paths, num_includes, arg_val,
                             (Boolean)*system_path_mode, FALSE);
        }
        else if (strncmp(arg, "-I", 2) == 0 && arg[2]) {
            add_include_path(include_paths, num_includes, arg + 2,
                             (Boolean)*system_path_mode, FALSE);
        }

        /* -include / -prefix */
        else if (strcmp(arg, "-include") == 0 || strcmp(arg, "-prefix") == 0) {
            NEXT_ARG(arg_val);
            add_prefix_include(ctx, arg_val);
        }

        /* -nosyspath */
        else if (strcmp(arg, "-nosyspath") == 0) {
            ctx->noSysPath = TRUE;
        }
        else if (strcmp(arg, "-stdinc") == 0 || strcmp(arg, "-defaults") == 0) {
            ctx->useDefaultIncludes = TRUE;
        }
        else if (strcmp(arg, "-nostdinc") == 0 || strcmp(arg, "-nodefaults") == 0) {
            ctx->useDefaultIncludes = FALSE;
        }
        else if (strcmp(arg, "-search") == 0) {
            ctx->searchPaths = TRUE;
        }

        /* --- C/C++ Language (PFrontEndC) --- */
        else if (strcmp(arg, "-lang") == 0 || strcmp(arg, "-dialect") == 0 ||
                 strncmp(arg, "-lang=", 6) == 0 || strncmp(arg, "-dialect=", 9) == 0) {
            if (arg[5] == '=') {
                arg_val = arg + 6;
            } else if (arg[8] == '=') {
                arg_val = arg + 9;
            } else {
                NEXT_ARG(arg_val);
            }
            if (strcmp(arg_val, "c") == 0) {
                fe->cplusplus = 0;
                fe->ecplusplus = 0;
            } else if (strcmp(arg_val, "c++") == 0) {
                fe->cplusplus = 1;
                fe->ecplusplus = 0;
            } else if (strcmp(arg_val, "ec++") == 0) {
                fe->cplusplus = 1;
                fe->ecplusplus = 1;
            } else {
                fprintf(stderr, "warning: unknown language: %s\n", arg_val);
            }
        }
        else if (strcmp(arg, "-char") == 0) {
            NEXT_ARG(arg_val);
            if (strcmp(arg_val, "signed") == 0)        fe->unsignedchars = 0;
            else if (strcmp(arg_val, "unsigned") == 0)  fe->unsignedchars = 1;
            else fprintf(stderr, "warning: unknown -char value: %s\n", arg_val);
        }
        else if (strcmp(arg, "-enum") == 0) {
            NEXT_ARG(arg_val);
            if (strcmp(arg_val, "min") == 0)       fe->enumsalwaysint = 0;
            else if (strcmp(arg_val, "int") == 0)  fe->enumsalwaysint = 1;
            else fprintf(stderr, "warning: unknown -enum value: %s\n", arg_val);
        }
        else if (strcmp(arg, "-inline") == 0) {
            NEXT_ARG(arg_val);
            if (strcmp(arg_val, "off") == 0 || strcmp(arg_val, "none") == 0) {
                fe->dontinline = 1;
                fe->autoinline = 0;
                fe->alwaysinline = 0;
            } else if (strcmp(arg_val, "on") == 0 || strcmp(arg_val, "smart") == 0) {
                fe->dontinline = 0;
                fe->autoinline = 0;
                fe->alwaysinline = 0;
            } else if (strcmp(arg_val, "auto") == 0) {
                fe->dontinline = 0;
                fe->autoinline = 1;
            } else if (strcmp(arg_val, "noauto") == 0) {
                fe->autoinline = 0;
            } else if (strcmp(arg_val, "all") == 0) {
                fe->dontinline = 0;
                fe->autoinline = 1;
                fe->alwaysinline = 1;
            } else if (strcmp(arg_val, "deferred") == 0) {
                fe->dontinline = 0;
                fe->defer_codegen = 1;
            } else if (strncmp(arg_val, "level=", 6) == 0) {
                fe->inlinelevel = (SInt16)atoi(arg_val + 6);
                fe->dontinline = 0;
            } else {
                fprintf(stderr, "warning: unknown -inline value: %s\n", arg_val);
            }
        }
        else if (strcmp(arg, "-bool") == 0) {
            NEXT_ARG(arg_val);
            int v = parse_onoff(arg_val);
            if (v >= 0) fe->booltruefalse = (Boolean)v;
        }
        else if (strcmp(arg, "-Cpp_exceptions") == 0) {
            NEXT_ARG(arg_val);
            int v = parse_onoff(arg_val);
            if (v >= 0) fe->enableexceptions = (Boolean)v;
        }
        else if (strcmp(arg, "-RTTI") == 0) {
            NEXT_ARG(arg_val);
            int v = parse_onoff(arg_val);
            if (v >= 0) fe->useRTTI = (Boolean)v;
        }
        else if (strcmp(arg, "-ansi") == 0) {
            NEXT_ARG(arg_val);
            if (strcmp(arg_val, "off") == 0) {
                /* -stdkeywords on, -enum min, -strict off */
                fe->onlystdkeywords = 1;
                fe->enumsalwaysint = 0;
                fe->ansistrict = 0;
            } else if (strcmp(arg_val, "on") == 0 || strcmp(arg_val, "relaxed") == 0) {
                /* -stdkeywords off, -enum min, -strict on */
                fe->onlystdkeywords = 0;
                fe->enumsalwaysint = 0;
                fe->ansistrict = 1;
            } else if (strcmp(arg_val, "strict") == 0) {
                /* -stdkeywords off, -enum int, -strict on */
                fe->onlystdkeywords = 0;
                fe->enumsalwaysint = 1;
                fe->ansistrict = 1;
            } else {
                fprintf(stderr, "warning: unknown -ansi value: %s\n", arg_val);
            }
        }
        else if (strcmp(arg, "-strict") == 0) {
            NEXT_ARG(arg_val);
            int v = parse_onoff(arg_val);
            if (v >= 0) fe->ansistrict = (Boolean)v;
        }
        else if (strcmp(arg, "-trigraphs") == 0) {
            NEXT_ARG(arg_val);
            int v = parse_onoff(arg_val);
            if (v >= 0) fe->trigraphs = (Boolean)v;
        }
        else if (strcmp(arg, "-stdkeywords") == 0) {
            NEXT_ARG(arg_val);
            int v = parse_onoff(arg_val);
            if (v >= 0) fe->onlystdkeywords = (Boolean)v;
        }
        else if (strcmp(arg, "-wchar_t") == 0) {
            NEXT_ARG(arg_val);
            int v = parse_onoff(arg_val);
            if (v >= 0) fe->wchar_type = (Boolean)v;
        }
        else if (strcmp(arg, "-r") == 0 || strcmp(arg, "-requireprotos") == 0) {
            fe->checkprotos = 1;
        }
        else if (strcmp(arg, "-ARM") == 0) {
            NEXT_ARG(arg_val);
            int v = parse_onoff(arg_val);
            if (v >= 0) fe->arm = (Boolean)v;
        }
        else if (strcmp(arg, "-str") == 0 || strcmp(arg, "-strings") == 0) {
            NEXT_ARG(arg_val);
            if (strchr(arg_val, ',')) {
                char tmp[128];
                size_t len = strlen(arg_val);
                if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
                memcpy(tmp, arg_val, len);
                tmp[len] = '\0';
                for (char* tok = strtok(tmp, ",");
                     tok;
                     tok = strtok(NULL, ","))
                {
                    if (!apply_ppc_string_option(fe, ppc, tok)) {
                        fprintf(stderr, "warning: unknown -str value: %s\n", tok);
                    }
                }
            } else if (!apply_ppc_string_option(fe, ppc, arg_val)) {
                fprintf(stderr, "warning: unknown -str value: %s\n", arg_val);
            }
        }
        else if (strcmp(arg, "-rostr") == 0 || strcmp(arg, "-readonlystrings") == 0) {
            /* Deprecated alias for -str readonly. Does not imply [no]reuse. */
            ppc->readonlystrings = 1;
        }
        else if (strcmp(arg, "-multibyte") == 0 || strcmp(arg, "-multibyteaware") == 0) {
            fe->multibyteaware = 1;
        }
        else if (strcmp(arg, "-once") == 0) {
            ctx->forceIncludeOnce = 1;
            add_pragma(ctx, "once on");
        }
        else if (strcmp(arg, "-notonce") == 0) {
            ctx->forceIncludeOnce = 0;
            add_pragma(ctx, "once off");
        }
        else if (strcmp(arg, "-relax_pointers") == 0) {
            fe->mpwpointerstyle = 1;
        }

        /* --- Warnings --- */
        else if (strcmp(arg, "-w") == 0) {
            NEXT_ARG(arg_val);
            if (strcmp(arg_val, "off") == 0) {
                ctx->noWarnings = 1;
                set_all_warnings(warn, 0);
            } else if (strcmp(arg_val, "on") == 0) {
                ctx->noWarnings = 0;
                /* enable common warnings */
                warn->warn_illpragma  = 1;
                warn->warn_possunwant = 1;
            } else if (strcmp(arg_val, "all") == 0) {
                ctx->noWarnings = 0;
                set_all_warnings(warn, 1);
                fe->checkprotos = 1;
            } else if (strcmp(arg_val, "error") == 0 || strcmp(arg_val, "err") == 0 ||
                       strcmp(arg_val, "iserror") == 0 || strcmp(arg_val, "iserr") == 0) {
                ctx->warningsAreErrors = 1;
                warn->warningerrors = 1;
            } else if (strcmp(arg_val, "noerror") == 0 || strcmp(arg_val, "noerr") == 0 ||
                       strcmp(arg_val, "noiserror") == 0 || strcmp(arg_val, "noiserr") == 0) {
                ctx->warningsAreErrors = 0;
                warn->warningerrors = 0;
            }
            /* individual warnings (with mwccps2-compatible aliases) */
            else if (strcmp(arg_val, "pragmas") == 0 || strcmp(arg_val, "illpragmas") == 0)
                warn->warn_illpragma = 1;
            else if (strcmp(arg_val, "nopragmas") == 0 || strcmp(arg_val, "noillpragmas") == 0)
                warn->warn_illpragma = 0;
            else if (strcmp(arg_val, "empty") == 0 || strcmp(arg_val, "emptydecl") == 0)
                warn->warn_emptydecl = 1;
            else if (strcmp(arg_val, "noempty") == 0 || strcmp(arg_val, "noemptydecl") == 0)
                warn->warn_emptydecl = 0;
            else if (strcmp(arg_val, "possible") == 0 || strcmp(arg_val, "unwanted") == 0)
                warn->warn_possunwant = 1;
            else if (strcmp(arg_val, "nopossible") == 0 || strcmp(arg_val, "nounwanted") == 0)
                warn->warn_possunwant = 0;
            else if (strcmp(arg_val, "unusedarg") == 0)    warn->warn_unusedarg = 1;
            else if (strcmp(arg_val, "nounusedarg") == 0)  warn->warn_unusedarg = 0;
            else if (strcmp(arg_val, "unusedvar") == 0)    warn->warn_unusedvar = 1;
            else if (strcmp(arg_val, "nounusedvar") == 0)  warn->warn_unusedvar = 0;
            else if (strcmp(arg_val, "unused") == 0) {
                warn->warn_unusedarg = 1;
                warn->warn_unusedvar = 1;
            }
            else if (strcmp(arg_val, "nounused") == 0) {
                warn->warn_unusedarg = 0;
                warn->warn_unusedvar = 0;
            }
            else if (strcmp(arg_val, "extracomma") == 0 || strcmp(arg_val, "comma") == 0)
                warn->warn_extracomma = 1;
            else if (strcmp(arg_val, "noextracomma") == 0 || strcmp(arg_val, "nocomma") == 0)
                warn->warn_extracomma = 0;
            else if (strcmp(arg_val, "pedantic") == 0 || strcmp(arg_val, "extended") == 0)
                warn->pedantic = 1;
            else if (strcmp(arg_val, "nopedantic") == 0 || strcmp(arg_val, "noextended") == 0)
                warn->pedantic = 0;
            else if (strcmp(arg_val, "hidevirtual") == 0 || strcmp(arg_val, "hidden") == 0 ||
                     strcmp(arg_val, "hiddenvirtual") == 0)
                warn->warn_hidevirtual = 1;
            else if (strcmp(arg_val, "nohidevirtual") == 0 || strcmp(arg_val, "nohidden") == 0 ||
                     strcmp(arg_val, "nohiddenvirtual") == 0)
                warn->warn_hidevirtual = 0;
            else if (strcmp(arg_val, "implicit") == 0 || strcmp(arg_val, "implicitconv") == 0)
                warn->warn_implicitconv = 1;
            else if (strcmp(arg_val, "noimplicit") == 0 || strcmp(arg_val, "noimplicitconv") == 0)
                warn->warn_implicitconv = 0;
            else if (strcmp(arg_val, "notinlined") == 0)   warn->warn_notinlined = 1;
            else if (strcmp(arg_val, "nonotinlined") == 0)  warn->warn_notinlined = 0;
            else if (strcmp(arg_val, "largeargs") == 0 || strcmp(arg_val, "nolargeargs") == 0)
                { /* PExtraWarningC - not in base PWarningC struct */ }
            else if (strcmp(arg_val, "structclass") == 0)  warn->warn_structclass = 1;
            else if (strcmp(arg_val, "nostructclass") == 0) warn->warn_structclass = 0;
            else if (strcmp(arg_val, "padding") == 0 || strcmp(arg_val, "nopadding") == 0)
                { /* PExtraWarningC - not in base PWarningC struct */ }
            else if (strcmp(arg_val, "notused") == 0 || strcmp(arg_val, "nonotused") == 0)
                { /* PExtraWarningC - not in base PWarningC struct */ }
            else if (strcmp(arg_val, "unusedexpr") == 0 || strcmp(arg_val, "nounusedexpr") == 0)
                { /* PExtraWarningC - not in base PWarningC struct */ }
            else if (strcmp(arg_val, "cmdline") == 0 || strcmp(arg_val, "nocmdline") == 0)
                { /* command-line parser warnings - handled by driver */ }
            else {
                /* Try numeric warning level */
                int wl = atoi(arg_val);
                if (wl > 0 && wl <= 3) {
                    ctx->noWarnings = 0;
                    warn->warn_illpragma = 1;
                    warn->warn_possunwant = 1;
                    if (wl >= 2) {
                        warn->warn_unusedvar = 1;
                        warn->warn_unusedarg = 1;
                        warn->warn_extracomma = 1;
                    }
                    if (wl >= 3) {
                        set_all_warnings(warn, 1);
                    }
                } else {
                    fprintf(stderr, "warning: unknown -w value: %s\n", arg_val);
                }
            }
        }
        /* GCC-compat warning aliases */
        else if (strcmp(arg, "-Wall") == 0) {
            ctx->noWarnings = 0;
            set_all_warnings(warn, 1);
        }
        else if (strcmp(arg, "-Werror") == 0) {
            ctx->warningsAreErrors = 1;
            warn->warningerrors = 1;
        }
        else if (strcmp(arg, "-Wmost") == 0) {
            ctx->noWarnings = 0;
            warn->warn_illpragma = 1;
            warn->warn_possunwant = 1;
            warn->warn_unusedvar = 1;
            warn->warn_unusedarg = 1;
        }
        else if (strcmp(arg, "-Wunused") == 0) {
            warn->warn_unusedvar = 1;
            warn->warn_unusedarg = 1;
        }
        else if (strcmp(arg, "-Wno-unused") == 0) {
            warn->warn_unusedvar = 0;
            warn->warn_unusedarg = 0;
        }

        /* --- Optimizer --- */
        else if (arg[0] == '-' && arg[1] == 'O') {
            if (!parse_dash_o_option(arg, opt, cg, ppc)) {
                fprintf(stderr, "warning: unknown -O option: %s\n", arg);
            }
        }
        else if (strcmp(arg, "-opt") == 0) {
            NEXT_ARG(arg_val);
            if (strchr(arg_val, ',')) {
                char tmp[128];
                size_t len = strlen(arg_val);
                if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
                memcpy(tmp, arg_val, len);
                tmp[len] = '\0';
                for (char* tok = strtok(tmp, ",");
                     tok;
                     tok = strtok(NULL, ","))
                {
                    if (!apply_opt_keyword(tok, opt, cg, ppc)) {
                        fprintf(stderr, "warning: unknown -opt value: %s\n", tok);
                    }
                }
            } else if (!apply_opt_keyword(arg_val, opt, cg, ppc)) {
                fprintf(stderr, "warning: unknown -opt value: %s\n", arg_val);
            }
        }

        /* --- MIPS Backend --- */
        else if (strcmp(arg, "-fp") == 0) {
            NEXT_ARG(arg_val);
            if (_stricmp(arg_val, "off") == 0 || _stricmp(arg_val, "none") == 0) {
                cg->fpuType = 0;
                ppc->floatingpoint = 0;
            } else if (_stricmp(arg_val, "single") == 0) {
                cg->fpuType = 1;
            } else if (_stricmp(arg_val, "soft") == 0 || _stricmp(arg_val, "software") == 0) {
                ppc->floatingpoint = 1;
            } else if (_stricmp(arg_val, "hard") == 0 || _stricmp(arg_val, "hardware") == 0) {
                ppc->floatingpoint = 2;
            } else if (_stricmp(arg_val, "fmadd") == 0) {
                ppc->floatingpoint = 2;
                ppc->fpcontract = 1;
            } else {
                fprintf(stderr, "warning: unknown -fp value: %s\n", arg_val);
            }
        }
        else if (strcmp(arg, "-fp_contract") == 0 || strcmp(arg, "-maf") == 0) {
            NEXT_ARG(arg_val);
            {
                int v = parse_onoff(arg_val);
                if (v >= 0) {
                    ppc->fpcontract = (UInt8)v;
                } else {
                    fprintf(stderr, "warning: unknown -fp_contract value: %s\n", arg_val);
                }
            }
        }
        else if (strcmp(arg, "-common") == 0) {
            NEXT_ARG(arg_val);
            {
                int v = parse_onoff(arg_val);
                if (v >= 0) {
                    ppc->commonsect = (UInt8)v;
                } else {
                    fprintf(stderr, "warning: unknown -common value: %s\n", arg_val);
                }
            }
        }
        else if (strcmp(arg, "-big") == 0) {
            ctx->prefsPPCProject.bigendian = 1;
        }
        else if (strcmp(arg, "-little") == 0) {
            ctx->prefsPPCProject.bigendian = 0;
        }
        else if (strcmp(arg, "-sdatathreshold") == 0 ||
                 strcmp(arg, "-sdata") == 0 ||
                 strcmp(arg, "-sdatathreshold") == 0) {
            NEXT_ARG(arg_val);
            ctx->prefsPPCProject.datathreshold = (SInt16)atoi(arg_val);
        }
        else if (strcmp(arg, "-sdata2") == 0 ||
                 strcmp(arg, "-sdata2threshold") == 0) {
            NEXT_ARG(arg_val);
            ctx->prefsPPCProject.sdata2threshold = (SInt16)atoi(arg_val);
        }
        else if (strcmp(arg, "-model") == 0) {
            NEXT_ARG(arg_val);
            if (_stricmp(arg_val, "absolute") == 0) {
                ctx->prefsPPCProject.codeModel = 0;
            } else if (_stricmp(arg_val, "other") == 0) {
                ctx->prefsPPCProject.codeModel = 1;
            } else {
                fprintf(stderr, "warning: unknown -model value: %s\n", arg_val);
            }
        }
        else if (strcmp(arg, "-use_lmw_stmw") == 0) {
            NEXT_ARG(arg_val);
            {
                int v = parse_onoff(arg_val);
                if (v >= 0) {
                    ppc->use_lmw_stmw = (UInt8)v;
                } else {
                    fprintf(stderr, "warning: unknown -use_lmw_stmw value: %s\n", arg_val);
                }
            }
        }
        else if (strcmp(arg, "-align") == 0) {
            NEXT_ARG(arg_val);
            if (strchr(arg_val, ',')) {
                char tmp[128];
                size_t len = strlen(arg_val);
                if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
                memcpy(tmp, arg_val, len);
                tmp[len] = '\0';
                for (char* tok = strtok(tmp, ",");
                     tok;
                     tok = strtok(NULL, ","))
                {
                    if (!set_ppc_alignment(ppc, tok)) {
                        fprintf(stderr, "warning: unknown -align value: %s\n", tok);
                    }
                }
            } else if (!set_ppc_alignment(ppc, arg_val)) {
                fprintf(stderr, "warning: unknown -align value: %s\n", arg_val);
            }
        }
        else if (strcmp(arg, "-proc") == 0 || strcmp(arg, "-processor") == 0 ||
                 strncmp(arg, "-proc=", 6) == 0 || strncmp(arg, "-processor=", 11) == 0) {
            SInt16 proc_val = 0;
            if (arg[5] == '=') {
                arg_val = arg + 6;
            } else if (arg[10] == '=') {
                arg_val = arg + 11;
            } else {
                NEXT_ARG(arg_val);
            }
            if (parse_ppc_processor(arg_val, &proc_val)) {
                ppc->processor = proc_val;
                copy_cstr(ppc->processorname, sizeof(ppc->processorname), arg_val);
            } else {
                fprintf(stderr, "warning: unknown processor: %s\n", arg_val);
            }
        }
        else if (strcmp(arg, "-profile") == 0) {
            /* profiler - stored but cc_mips.dll may not use it for PS1 */
            ppc->profiler = 1;
        }
        else if (strcmp(arg, "-farcall") == 0) {
            NEXT_ARG(arg_val);
            /* far call - stored but mainly relevant for PS2/larger address spaces */
        }

        /* --- Include search semantics --- */
        else if (strcmp(arg, "-cwd") == 0) {
            NEXT_ARG(arg_val);
            if (strcmp(arg_val, "proj") == 0) {
                ctx->includeSearchMode = hostIncludeSearchProj;
            } else if (strcmp(arg_val, "source") == 0) {
                ctx->includeSearchMode = hostIncludeSearchSource;
            } else if (strcmp(arg_val, "explicit") == 0) {
                ctx->includeSearchMode = hostIncludeSearchExplicit;
            } else if (strcmp(arg_val, "include") == 0) {
                ctx->includeSearchMode = hostIncludeSearchInclude;
            } else {
                fprintf(stderr, "warning: unknown -cwd mode: %s\n", arg_val);
            }
        }
        else if (strcmp(arg, "-gccincludes") == 0 || strcmp(arg, "-gccinc") == 0) {
            ctx->gccIncludes = TRUE;
            ctx->includeSearchMode = hostIncludeSearchInclude;
            if (!ctx->usedDashIMinus) {
                move_pending_user_paths_to_system(include_paths, *num_includes);
            }
            *system_path_mode = 1;
        }

        /* --- Debug --- */
        else if (strcmp(arg, "-g") == 0) {
            ctx->debugInfo = 1;
        }
        else if (strcmp(arg, "-sym") == 0) {
            NEXT_ARG(arg_val);
            if (strcmp(arg_val, "off") == 0)       ctx->debugInfo = 0;
            else if (strcmp(arg_val, "on") == 0)   ctx->debugInfo = 1;
            else if (strcmp(arg_val, "full") == 0)  ctx->debugInfo = 1;
        }

        /* --- Unknown flag --- */
        else {
            if (ctx->verbose) {
                fprintf(stderr, "Ignoring unknown flag: %s\n", arg);
            }
        }
    }

    return 0;
}

/* ============================================================
 * main
 * ============================================================ */

int main(int argc, char* argv[]) {
    const char* requested_dll = NULL;
    const char* output_file = NULL;
    const char* source_files[MAX_SOURCE_FILES];
    int num_sources = 0;
    int had_compile_failure = 0;
    int show_version_only = 0;
    int plugin_api_version = 12;
    CompilerVersionInfo active_version_info;
    int has_active_version_info = 0;
    char loaded_dll_name[MAX_PATH];
    char loaded_dll_path[MAX_PATH];

    /* Temporary storage for include paths */
    PendingIncludePath include_paths[MAX_INCLUDE_PATHS];
    int num_includes = 0;
    int system_path_mode = 0;  /* 0=user paths, 1=system paths after -I- */

    /* Initialize plugin context */
    CWPluginPrivateContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    init_pref_defaults(&ctx);
    ctx.includeSearchMode = hostIncludeSearchProj;
    ctx.useDefaultIncludes = TRUE;
    ctx.nextFileID = 1;

    /* Parse arguments */
    if (parse_args(argc, argv, &ctx, &requested_dll, &show_version_only,
                   source_files, &num_sources, &output_file,
                   include_paths, &num_includes, &system_path_mode) != 0) {
        return 1;
    }

    if (show_version_only) {
        CompilerVersionInfo version_info;
        int has_version_info = 0;
        HMODULE hVersionDll = load_compiler_dll(requested_dll, loaded_dll_name, sizeof(loaded_dll_name), ctx.verbose);

        if (!hVersionDll) {
            if (requested_dll && requested_dll[0]) {
                fprintf(stderr, "Failed to load compiler DLL %s (error %lu)\n",
                        requested_dll, GetLastError());
                return 1;
            }
            print_wrapper_version();
            return 0;
        }

        loaded_dll_path[0] = '\0';
        if (GetModuleFileNameA(hVersionDll, loaded_dll_path, sizeof(loaded_dll_path)) == 0) {
            copy_cstr(loaded_dll_path, sizeof(loaded_dll_path), loaded_dll_name);
        }
        has_version_info = get_compiler_version_info(loaded_dll_path, &version_info);
        print_dynamic_version(loaded_dll_path, has_version_info ? &version_info : NULL);
        FreeLibrary(hVersionDll);
        return 0;
    }

    if (num_sources <= 0) {
        CompilerVersionInfo version_info;
        int has_version_info = 0;
        const char* prog_name = basename_from_path(argv[0]);
        HMODULE hVersionDll = load_compiler_dll(requested_dll, loaded_dll_name, sizeof(loaded_dll_name), ctx.verbose);

        if (hVersionDll) {
            loaded_dll_path[0] = '\0';
            if (GetModuleFileNameA(hVersionDll, loaded_dll_path, sizeof(loaded_dll_path)) == 0) {
                copy_cstr(loaded_dll_path, sizeof(loaded_dll_path), loaded_dll_name);
            }
            has_version_info = get_compiler_version_info(loaded_dll_path, &version_info);
            print_dynamic_version(loaded_dll_path, has_version_info ? &version_info : NULL);
            FreeLibrary(hVersionDll);
        } else {
            print_wrapper_version();
        }

        fprintf(stderr, "\nUsage: %s [options] [-o output] input1.c [input2.c ...]\n", prog_name);
        fprintf(stderr, "\nPlease enter '%s -help' for information about options.\n", prog_name);
        return 1;
    }

    if (ctx.dependencyMode == 1 && !ctx.preprocess && !ctx.disassemble && output_file) {
        copy_cstr(ctx.dependencyOutputFile, sizeof(ctx.dependencyOutputFile), output_file);
        output_file = NULL;
    }

    if (output_file && num_sources > 1 &&
        (ctx.disassembleToFile || (!ctx.preprocess && ctx.dependencyMode != 1)) &&
        !is_directory_path(output_file))
    {
        fprintf(stderr,
            "Error: when multiple input files are specified, -o must name an existing directory: %s\n",
            output_file);
        return 1;
    }

    if (ctx.dependencyMode == 1 && ctx.dependencyOutputFile[0]) {
        FILE* depf = fopen(ctx.dependencyOutputFile, "wb");
        if (!depf) {
            fprintf(stderr, "Cannot open dependency output file: %s\n", ctx.dependencyOutputFile);
            return 1;
        }
        fclose(depf);
    }

    if (ctx.useDefaultIncludes) {
        add_default_include_paths(&ctx, include_paths, &num_includes);
    }

    /* Set up include paths */
    if (num_includes > 0) {
        int user_count = 0;
        int system_count = 0;

        for (int i = 0; i < num_includes; i++) {
            if (include_paths[i].system) system_count++;
            else user_count++;
        }

        if (user_count > 0) {
            ctx.userPaths = (HostAccessPath*)calloc(user_count, sizeof(HostAccessPath));
            if (!ctx.userPaths) {
                fprintf(stderr, "Out of memory allocating user include paths\n");
                return 1;
            }
            ctx.userPathCount = user_count;
        }
        if (system_count > 0) {
            ctx.systemPaths = (HostAccessPath*)calloc(system_count, sizeof(HostAccessPath));
            if (!ctx.systemPaths) {
                fprintf(stderr, "Out of memory allocating system include paths\n");
                return 1;
            }
            ctx.systemPathCount = system_count;
        }

        {
            int u = 0;
            int s = 0;
            for (int i = 0; i < num_includes; i++) {
                HostAccessPath* dst;
                if (include_paths[i].system) {
                    dst = &ctx.systemPaths[s++];
                } else {
                    dst = &ctx.userPaths[u++];
                }
                copy_cstr(dst->path, MAX_PATH, include_paths[i].path);
                dst->recursive = include_paths[i].recursive;
            }
        }
    }

    /* Expose command-line defines/pragmas as MWCC-style virtual prefix file. */
    finalize_cmdline_prefix(&ctx);

    /* Load compiler DLL (explicit via -dll/-compiler-dll, else known-name probe). */
    g_registered_pluginlib_module = NULL;
    g_registered_pluginlib_version = 0;
    HMODULE hDll = load_compiler_dll(requested_dll, loaded_dll_name, sizeof(loaded_dll_name), ctx.verbose);
    if (!hDll) {
        if (requested_dll && requested_dll[0]) {
            fprintf(stderr, "Failed to load compiler DLL %s (error %lu)\n",
                    requested_dll, GetLastError());
        } else {
            fprintf(stderr, "Failed to load compiler DLL (error %lu)\n", GetLastError());
            fprintf(stderr, "Tried:");
            for (int i = 0; i < KNOWN_COMPILER_DLL_COUNT; i++) {
                fprintf(stderr, "%s%s", (i == 0) ? " " : ", ", kKnownCompilerDllNames[i]);
            }
            fprintf(stderr, "\n");
        }
        return 1;
    }
    if (ctx.verbose) {
        fprintf(stderr, "Loaded compiler DLL: %s\n",
                loaded_dll_name[0] ? loaded_dll_name : "<unknown>");
    }

    loaded_dll_path[0] = '\0';
    if (GetModuleFileNameA(hDll, loaded_dll_path, sizeof(loaded_dll_path)) != 0) {
        copy_cstr(loaded_dll_name, sizeof(loaded_dll_name), loaded_dll_path);
    }

    has_active_version_info = get_compiler_version_info(loaded_dll_path, &active_version_info);
    ctx.prefsMIPSCodeGenR4Compat =
        has_active_version_info ? should_enable_mips_r4_compat(&active_version_info) : FALSE;
    if (ctx.verbose && ctx.prefsMIPSCodeGenR4Compat) {
        fprintf(stderr, "Enabled prefsMIPSCodeGenR4Compat based on compiler version metadata.\n");
    }

    if (g_registered_pluginlib_version == 2) {
        plugin_api_version = 8;
    }
    if (ctx.verbose) {
        if (g_registered_pluginlib_version != 0) {
            fprintf(stderr, "Registered compiler import: PluginLib%d.dll\n", g_registered_pluginlib_version);
        } else {
            fprintf(stderr, "Registered compiler import: <none>\n");
        }
        fprintf(stderr, "Using Plugin API version: %d\n", plugin_api_version);
    }

    if (!g_registered_pluginlib_module || g_registered_pluginlib_version == 0) {
        fprintf(stderr, "Failed to resolve active PluginLib via registration\n");
        FreeLibrary(hDll);
        return 1;
    }

    /* Extract string table from compiler DLL's Mac resource fork. */
    if (g_registered_pluginlib_module) {
        MWCC_InitStringTableFunc initFunc =
            (MWCC_InitStringTableFunc)GetProcAddress(g_registered_pluginlib_module, "MWCC_InitStringTable");
        if (initFunc) {
            initFunc(hDll);
        } else if (ctx.verbose) {
            fprintf(stderr, "Warning: MWCC_InitStringTable not found in PluginLib\n");
        }
    } else if (ctx.verbose) {
        fprintf(stderr, "Warning: PluginLib not loaded\n");
    }

    PluginMainFunc plugin_main = (PluginMainFunc)GetProcAddress(hDll, "main");
    if (!plugin_main) {
        fprintf(stderr, "Failed to find 'main' export in %s\n",
                loaded_dll_name[0] ? loaded_dll_name : "<compiler DLL>");
        FreeLibrary(hDll);
        return 1;
    }

    if (ctx.verbose) fprintf(stderr, "Found plugin main at %p\n", (void*)plugin_main);

    ctx.apiVersion = plugin_api_version;
    ctx.numFiles = num_sources;

    /* Source and output fields are assigned per-file in the compile loop. */
    // ctx.precompile = FALSE;
    // ctx.autoprecompile = FALSE;
    if (ctx.dependencyMode == 1 && !ctx.preprocess && !ctx.disassemble) {
        ctx.preprocess = 2;
    } else {
        ctx.preprocess = ctx.preprocess ? TRUE : FALSE;
    }

    short result;

    /* === reqInitialize === */
    if (ctx.verbose) fprintf(stderr, "\n=== reqInitialize ===\n");
    ctx.request = reqInitialize;
    result = plugin_main(&ctx);
    if (ctx.verbose) fprintf(stderr, "reqInitialize returned: %d\n", result);
    if (result != 0) {
        fprintf(stderr, "Plugin initialization failed (result=%d)\n", result);
        goto cleanup;
    }

    {
        int compile_failed = 0;

        for (int src_idx = 0; src_idx < num_sources; src_idx++) {
            const char* source_file = source_files[src_idx];
            const char* active_source = source_file;
            const char* active_ext = ctx.disassembleToFile ? ".s" : ".o";
            char resolved_source_file[MAX_PATH];
            char per_file_output[MAX_PATH];
            char per_file_object[MAX_PATH];
            int file_failed = 0;
            SInt32 errors_before_compile;

            /* Reset per-file plugin state (MWCC-style compile loop over source list). */
            if (ctx.sourceText) {
                free(ctx.sourceText);
                ctx.sourceText = NULL;
            }
            if (ctx.objectData) {
                free(ctx.objectData);
                ctx.objectData = NULL;
            }
            ctx.sourceTextSize = 0;
            ctx.objectDataSize = 0;
            ctx.objectStored = 0;
            ctx.preprocessedTextSize = 0;
            ctx.nextFileID = 1;
            ctx.fileRecordCount = 0;
            ctx.lastIncludeDir[0] = '\0';
            ctx.includeRecordCount = 0;

            resolved_source_file[0] = '\0';
            if (ctx.searchPaths && !file_exists(source_file)) {
                if (!find_file_in_access_paths(ctx.userPaths, ctx.userPathCount, source_file,
                                               resolved_source_file, sizeof(resolved_source_file)))
                {
                    find_file_in_access_paths(ctx.systemPaths, ctx.systemPathCount, source_file,
                                              resolved_source_file, sizeof(resolved_source_file));
                }
                if (resolved_source_file[0]) {
                    active_source = resolved_source_file;
                }
            }

            ctx.whichFile = src_idx;
            copy_cstr(ctx.sourceFile, MAX_PATH, active_source);

            ctx.sourceText = read_file(active_source, &ctx.sourceTextSize);
            if (!ctx.sourceText) {
                fprintf(stderr, "Cannot read source file: %s\n", active_source);
                ctx.numErrors++;
                file_failed = 1;
            } else {
                ctx.sourceTextSize = convert_line_endings_to_cr(ctx.sourceText, ctx.sourceTextSize);
            }

            if (!file_failed) {
                if (output_file) {
                    int output_is_dir = is_directory_path(output_file);
                    if (output_is_dir ||
                        (num_sources > 1 &&
                         (ctx.disassembleToFile || (!ctx.preprocess && ctx.dependencyMode != 1))))
                    {
                        const char* leaf = get_path_leaf(active_source);
                        char leaf_out[MAX_PATH];
                        char leaf_obj[MAX_PATH];
                        replace_extension(leaf, ".o", leaf_obj, sizeof(leaf_obj));
                        replace_extension(leaf, active_ext, leaf_out, sizeof(leaf_out));
                        if (!join_path(per_file_object, sizeof(per_file_object), output_file, leaf_obj)) {
                            fprintf(stderr, "Output path too long: %s/%s\n", output_file, leaf_obj);
                            ctx.numErrors++;
                            file_failed = 1;
                        } else if (!(ctx.dependencyMode == 1 && !ctx.preprocess && !ctx.disassemble) &&
                                   !join_path(per_file_output, sizeof(per_file_output), output_file, leaf_out))
                        {
                            fprintf(stderr, "Output path too long: %s/%s\n", output_file, leaf_out);
                            ctx.numErrors++;
                            file_failed = 1;
                        }
                    } else {
                        copy_cstr(per_file_object, MAX_PATH, output_file);
                        copy_cstr(per_file_output, MAX_PATH, output_file);
                    }
                } else {
                    replace_extension(active_source, ".o", per_file_object, sizeof(per_file_object));
                    if (ctx.disassembleToFile) {
                        replace_extension(active_source, ".s", per_file_output, sizeof(per_file_output));
                    } else if (!ctx.preprocess) {
                        replace_extension(active_source, ".o", per_file_output, sizeof(per_file_output));
                    } else {
                        per_file_output[0] = '\0';
                    }
                }

                if (!file_failed && ctx.dependencyMode == 1 && !ctx.preprocess && !ctx.disassemble) {
                    per_file_output[0] = '\0';
                }
            }

            if (file_failed) {
                compile_failed = 1;
                if (!ctx.noFail) break;
                continue;
            }

            copy_cstr(ctx.outputFile, MAX_PATH, per_file_output);

            errors_before_compile = ctx.numErrors;

            if (ctx.verbose) {
                fprintf(stderr, "\n=== reqCompile (%d/%d): %s ===\n",
                        src_idx + 1, num_sources, active_source);
            }
            ctx.request = reqCompile;
            result = plugin_main(&ctx);
            if (ctx.verbose) fprintf(stderr, "reqCompile returned: %d\n", result);

            if (ctx.disassemble) {
                if (result == 0) {
                    if (ctx.disassembleToFile) {
                        const char* disasm_path = ctx.outputFile[0] ? ctx.outputFile : "output.s";
                        if (!freopen(disasm_path, "wb", stdout)) {
                            fprintf(stderr, "Cannot open disassembly output file: %s\n", disasm_path);
                            ctx.numErrors++;
                        }
                    }

                    if (ctx.numErrors == errors_before_compile) {
                        if (ctx.verbose) fprintf(stderr, "=== reqCompDisassemble ===\n");
                        ctx.request = reqCompDisassemble;
                        result = plugin_main(&ctx);
                        if (ctx.verbose) fprintf(stderr, "reqCompDisassemble returned: %d\n", result);
                        if (result != 0 && result != cwErrRequestFailed) {
                            fprintf(stderr, "Disassembly failed (result=%d)\n", result);
                        }
                        if (result == 0 && ctx.preprocessedTextSize <= 0) {
                            fprintf(stderr, "Disassembly produced no output (compiler plugin does not support reqCompDisassemble).\n");
                            ctx.numErrors++;
                        }
                    }
                } else if (result != cwErrRequestFailed) {
                    fprintf(stderr, "Compilation failed (result=%d)\n", result);
                }
            } else if (result != 0 && result != cwErrRequestFailed) {
                fprintf(stderr, "Compilation failed (result=%d)\n", result);
            }

            if (ctx.objectStored && !ctx.preprocess && !ctx.disassemble && ctx.dependencyMode != 1) {
                write_object_file(&ctx);
            }

            if (ctx.dependencyMode != 0 && !file_failed) {
                char per_file_dep[MAX_PATH];
                const char* dep_output_path = NULL;
                int dep_append = 0;

                if (ctx.dependencyMode == 1) {
                    dep_output_path = ctx.dependencyOutputFile[0] ? ctx.dependencyOutputFile : NULL;
                    dep_append = ctx.dependencyOutputFile[0] ? 1 : 0;
                } else {
                    replace_extension(per_file_object, ".d", per_file_dep, sizeof(per_file_dep));
                    dep_output_path = per_file_dep;
                }

                if (!emit_dependency_rule(&ctx, active_source, per_file_object, dep_output_path, dep_append)) {
                    ctx.numErrors++;
                }
            }

            if (ctx.numErrors > errors_before_compile || (result != 0 && result != cwErrRequestFailed)) {
                compile_failed = 1;
                if (!ctx.noFail) break;
            }
        }

        if (compile_failed) had_compile_failure = 1;
    }

    /* === reqTerminate === */
    if (ctx.verbose) fprintf(stderr, "\n=== reqTerminate ===\n");
    ctx.request = reqTerminate;
    result = plugin_main(&ctx);
    if (ctx.verbose) fprintf(stderr, "reqTerminate returned: %d\n", result);

    /* Summary */
    if (ctx.numErrors > 0) {
        fprintf(stderr, "%d error(s), %d warning(s)\n", ctx.numErrors, ctx.numWarnings);
    } else if (ctx.numWarnings > 0) {
        fprintf(stderr, "%d warning(s)\n", ctx.numWarnings);
    }

cleanup:
    FreeLibrary(hDll);
    free(ctx.sourceText);
    if (ctx.objectData) free(ctx.objectData);
    if (ctx.userPaths) free(ctx.userPaths);
    if (ctx.systemPaths) free(ctx.systemPaths);
    if (ctx.fileRecords) free(ctx.fileRecords);
    if (ctx.includeRecords) free(ctx.includeRecords);
    if (ctx.defineText) free(ctx.defineText);

    return (ctx.numErrors > 0 || had_compile_failure) ? 1 : 0;
}
