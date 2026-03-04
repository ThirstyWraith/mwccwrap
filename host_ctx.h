/*
 * host_ctx.h - Host context structures for mwccwrap
 *
 * This defines the CWPluginPrivateContext layout that the DLL expects,
 * plus our host-side state.
 */

#ifndef HOST_CTX_H
#define HOST_CTX_H

#include "cw_types.h"

/*
 * Memory handle implementation.
 * CWMemHandle is a pointer to one of these.
 */
typedef struct CWMemHandlePrivateStruct {
    void*  data;
    SInt32 size;
    int    locked;
} CWMemHandleImpl;

/*
 * Access path entry
 */
typedef struct HostAccessPath {
    char       path[MAX_PATH];
    Boolean    recursive;
} HostAccessPath;

enum {
    hostIncludeSearchProj = 0,
    hostIncludeSearchSource = 1,
    hostIncludeSearchExplicit = 2,
    hostIncludeSearchInclude = 3
};

typedef struct HostFileRecord {
    short fileID;
    Boolean isSystem;
    char  path[MAX_PATH];
} HostFileRecord;

typedef struct HostIncludeRecord {
    char path[MAX_PATH];
} HostIncludeRecord;

/* ============================================================
 * Preference panel structs
 *
 * These match the binary layout the DLL reads via
 * CWSecretGetNamedPreferences(). Adapted from the MWCC decomp
 * pref_structs.h (CW Pro era). We use #pragma pack(1) to match
 * the Win32 packing the DLL was compiled with.
 * ============================================================ */

#pragma pack(push, 1)

/*
 * "C/C++ Compiler" panel (332 bytes)
 */
typedef struct PFrontEndC {
    SInt16  version;                /* 0x00: current = 0x12 (18) */
    Boolean cplusplus;              /* 0x02 */
    Boolean checkprotos;            /* 0x03 */
    Boolean arm;                    /* 0x04 */
    Boolean trigraphs;              /* 0x05 */
    Boolean onlystdkeywords;        /* 0x06 */
    Boolean enumsalwaysint;         /* 0x07 */
    Boolean mpwpointerstyle;        /* 0x08 */
    unsigned char oldprefixname[32];/* 0x09: legacy, superseded by newprefixname in v10+ */
    Boolean ansistrict;             /* 0x29 */
    Boolean mpwcnewline;            /* 0x2A */
    Boolean wchar_type;             /* 0x2B */
    Boolean enableexceptions;       /* 0x2C */
    Boolean dontreusestrings;       /* 0x2D */
    Boolean poolstrings;            /* 0x2E */
    Boolean dontinline;             /* 0x2F */
    Boolean useRTTI;                /* 0x30 */
    Boolean multibyteaware;         /* 0x31 */
    Boolean unsignedchars;          /* 0x32 */
    Boolean autoinline;             /* 0x33 */
    Boolean booltruefalse;          /* 0x34 */
    Boolean direct_to_som;          /* 0x35 */
    Boolean som_env_check;          /* 0x36 */
    Boolean alwaysinline;           /* 0x37 */
    SInt16  inlinelevel;            /* 0x38 */
    Boolean ecplusplus;             /* 0x3A */
    Boolean objective_c;            /* 0x3B */
    Boolean defer_codegen;          /* 0x3C */
    /* --- fields below are ignored by PS1 DLLs (they copy <= 62 bytes) --- */
    Boolean templateparser;         /* 0x3D */
    Boolean c99;                    /* 0x3E */
    Boolean bottomupinline;         /* 0x3F */
    unsigned char prefixname[256];  /* 0x40: Pascal string (byte 0 = length) */
    UInt8   old_version;            /* 0x140 */
    Boolean warned_missing_cpp_panel;/* 0x141 */
    Boolean gcc_extensions;         /* 0x142 */
    Boolean instance_manager;       /* 0x143 */
    UInt8   ipa_mode;               /* 0x144 */
    UInt8   reserved_145[7];        /* 0x145 */
} PFrontEndC;

/*
 * "C/C++ Warnings" panel (34 bytes)
 */
typedef struct PWarningC {
    SInt16  version;                 /* 0x00 */
    Boolean warn_illpragma;          /* 0x02 */
    Boolean warn_emptydecl;          /* 0x03 */
    Boolean warn_possunwant;         /* 0x04 */
    Boolean warn_unusedvar;          /* 0x05 */
    Boolean warn_unusedarg;          /* 0x06 */
    Boolean warn_extracomma;         /* 0x07 */
    Boolean pedantic;                /* 0x08 */
    Boolean warningerrors;           /* 0x09 */
    Boolean warn_hidevirtual;        /* 0x0A */
    Boolean warn_implicitconv;       /* 0x0B */
    Boolean warn_notinlined;         /* 0x0C */
    Boolean warn_structclass;        /* 0x0D */
    Boolean warn_missingreturn;      /* 0x0E */
    Boolean warn_no_side_effect;     /* 0x0F */
    Boolean warn_resultnotused;      /* 0x10 */
    Boolean warn_padding;            /* 0x11 */
    Boolean warn_impl_i2f_conv;      /* 0x12 */
    Boolean warn_impl_f2i_conv;      /* 0x13 */
    Boolean warn_impl_s2u_conv;      /* 0x14 */
    Boolean warn_illtokenpasting;    /* 0x15 */
    Boolean warn_filenamecaps;       /* 0x16 */
    Boolean warn_filenamecapssystem; /* 0x17 */
    Boolean warn_undefmacro;         /* 0x18 */
    Boolean warn_ptrintconv;         /* 0x19 */
    UInt8   reserved_1A[8];          /* 0x1A-0x21 */
} PWarningC;

/*
 * "Global Optimizer" / "PS Global Optimizer" / "EPPC Global Optimizer"
 * panel (12 bytes)
 */
typedef struct PGlobalOptimizer {
    SInt16 version;                 /* 0x00 */
    UInt8  optimizationlevel;       /* 0x02: 0=off, 1-4=levels */
    UInt8  optfor;                  /* 0x03: 0=default, 1=speed, 2=space */
    UInt8  reserved[8];             /* 0x04-0x0B */
} PGlobalOptimizer;

/*
 * "PPC EABI CodeGen" panel (58 bytes)
 */
typedef struct PPCEABICodeGen {
    SInt16 version;                 /* 0x00 */
    char   structalignment;         /* 0x02 */
    UInt8  readonlystrings;         /* 0x03 */
    UInt8  pooldata;                /* 0x04 */
    UInt8  filler_05;               /* 0x05 */
    UInt8  profiler;                /* 0x06 */
    UInt8  filler_07;               /* 0x07 */
    UInt8  peephole;                /* 0x08 */
    UInt8  filler_09;               /* 0x09 */
    char   filler_0A;               /* 0x0A */
    char   scheduling;              /* 0x0B */
    UInt8  filler_0C;               /* 0x0C */
    UInt8  commonsect;              /* 0x0D */
    char   floatingpoint;           /* 0x0E */
    UInt8  use_lmw_stmw;            /* 0x0F */
    SInt16 processor;               /* 0x10 */
    char   function_align;          /* 0x12 */
    UInt8  fpcontract;              /* 0x13 */
    UInt8  altivec;                 /* 0x14 */
    UInt8  vrsave;                  /* 0x15 */
    UInt8  use_e500_fp;             /* 0x16 */
    UInt8  use_isel;                /* 0x17 */
    UInt8  use_fsel;                /* 0x18 */
    UInt8  volatileasm;             /* 0x19 */
    UInt8  strictfp;                /* 0x1A */
    UInt8  genfsel;                 /* 0x1B */
    char   processorname[16];       /* 0x1C */
    UInt8  orderedfpcmp;            /* 0x2C */
    UInt8  altivec_move_block;      /* 0x2D */
    UInt8  linkerpoolstrings;       /* 0x2E */
    UInt8  poolconst;               /* 0x2F */
    UInt8  vectors;                 /* 0x30 */
    UInt8  gen_vle;                 /* 0x31 */
    UInt8  use_e500v2_fp;           /* 0x32 */
    UInt8  ppc_asm_to_vle;          /* 0x33 */
    UInt8  reserved_34[5];          /* 0x34-0x38 */
    UInt8  reserved_39;             /* 0x39 */
} PPCEABICodeGen;
/* static_assert: sizeof == 58 (0x3A) */

/*
 * "PPC EABI Linker" panel (124 bytes)
 */
typedef struct PPCEABILinker {
    SInt16 version;                 /* 0x00 */
    UInt8  linksym;                 /* 0x02 */
    UInt8  symfullpath;             /* 0x03 */
    UInt8  linkmap;                 /* 0x04 */
    UInt8  nolinkwarnings;          /* 0x05 */
    UInt8  genSrecFile;             /* 0x06 */
    UInt8  linkunused;              /* 0x07 */
    UInt8  use_lcf;                 /* 0x08 */
    UInt8  use_codeaddr;            /* 0x09 */
    UInt8  use_dataaddr;            /* 0x0A */
    UInt8  use_sdataaddr;           /* 0x0B */
    UInt8  use_sdata2addr;          /* 0x0C */
    UInt8  use_stackaddr;           /* 0x0D */
    UInt8  use_heapaddr;            /* 0x0E */
    UInt8  genROMimage;             /* 0x0F */
    UInt32 codeaddr;                /* 0x10 */
    UInt32 dataaddr;                /* 0x14 */
    UInt32 smalldataaddr;           /* 0x18 */
    UInt32 smalldata2addr;          /* 0x1C */
    UInt32 stackaddr;               /* 0x20 */
    UInt32 rambuffer;               /* 0x24 */
    UInt32 romimage_addr;           /* 0x28 */
    SInt16 srecLength;              /* 0x2C */
    UInt8  srecEOL;                 /* 0x2E */
    UInt8  pad;                     /* 0x2F */
    char   mainname[64];            /* 0x30 */
    UInt32 heapaddr;                /* 0x70 */
    UInt8  linkmode;                /* 0x74 */
    UInt8  listdwarf;               /* 0x75 */
    UInt8  listclosure;             /* 0x76 */
    UInt8  sortSrec;                /* 0x77 */
    UInt8  gen_bin_file;            /* 0x78 */
    UInt8  reserved2[3];            /* 0x79-0x7B */
} PPCEABILinker;
/* static_assert: sizeof == 124 (0x7C) */

/*
 * "PPC EABI Project" panel (580 bytes)
 */
typedef struct PPCEABIProject {
    SInt16 version;                 /* 0x00 */
    SInt16 projtype;                /* 0x02 */
    char   old_outfile[32];         /* 0x04 */
    UInt32 heapsize;                /* 0x24 */
    UInt32 stacksize;               /* 0x28 */
    UInt8  bigendian;               /* 0x2C */
    UInt8  pad;                     /* 0x2D */
    SInt16 datathreshold;           /* 0x2E */
    SInt16 sdata2threshold;         /* 0x30 */
    SInt16 codeModel;               /* 0x32 */
    UInt8  filler1;                 /* 0x34 */
    UInt8  filler2;                 /* 0x35 */
    UInt8  filler3;                 /* 0x36 */
    UInt8  disable_extensions;      /* 0x37 */
    UInt8  deadstrip_partiallink;   /* 0x38 */
    UInt8  final_partiallink;       /* 0x39 */
    UInt8  resolved_partiallink;    /* 0x3A */
    char   long_outfile[256];       /* 0x3B */
    UInt8  abi_type;                /* 0x13B */
    UInt8  dwarf_version;           /* 0x13C */
    UInt8  tune_relocations;        /* 0x13D */
    UInt8  reserved_13E;            /* 0x13E */
    UInt8  reserved_13F[4];         /* 0x13F-0x142 */
    char   interpreter[256];        /* 0x143 */
} PPCEABIProject;
/* static_assert: sizeof == 580 (0x244) */

/*
 * "C/C++ Preprocessor" panel (32790 bytes)
 */
typedef struct PPreprocessor {
    SInt16 version;                 /* 0x00: current = 4 */
    UInt8  emit_line;               /* 0x02 */
    UInt8  emit_fullpath;           /* 0x03 */
    UInt8  keep_comments;           /* 0x04 */
    UInt8  unused1;                 /* 0x05 */
    UInt8  pch_uses_prefix_text;    /* 0x06 */
    UInt8  emit_pragmas;            /* 0x07 */
    UInt8  keep_whitespace;         /* 0x08 */
    UInt8  emit_file;               /* 0x09 */
    UInt8  multibyte_encoding;      /* 0x0A */
    UInt8  reserved_0B[11];         /* 0x0B-0x15 */
    char   prefix_text[32768];      /* 0x16-0x8015 */
} PPreprocessor;
/* static_assert: sizeof == 32790 (0x8016) */

/* ============================================================
 * MIPS preference panel structs
 * ============================================================ */

/* "MIPS CodeGen" panel (20 bytes) */
typedef struct PMIPSCodeGen {
    SInt16 version;             /* 0x00: ignored by DLL */
    UInt8  structalignment;     /* 0x02: struct alignment (bulk-copied, not individually read) */
    UInt8  tracebacktables;     /* 0x03: traceback tables (bulk-copied, not individually read) */
    SInt16 processor;           /* 0x04: processor type (overridden to 0x1000 for PSX in init) */
    SInt16 fpuType;             /* 0x06: FPU type: 0=none, 1=single, 2=double, 3=all */
    SInt16 isaLevel;            /* 0x08: MIPS ISA level (I/II/III/IV) */
    UInt8  multibyteAware;      /* 0x0A: multibyte string handling */
    UInt8  peephole;            /* 0x0B: peephole optimization enable */
    UInt8  reserved_0C;         /* 0x0C: unused */
    UInt8  useIntrinsics;       /* 0x0D: inline intrinsics for strcpy/memcpy/etc */
    UInt8  reserved_0E;         /* 0x0E: unused */
    UInt8  reserved_0F;         /* 0x0F: unused */
    UInt32 reserved_10;         /* 0x10: stored but never read */
} PMIPSCodeGen;

/* "MIPS Linker Panel" (340 bytes) */
typedef struct PMIPSLinker {
    SInt16 version;             /* 0x00 */
    UInt8  reserved_02;         /* 0x02 */
    UInt8  reserved_03;         /* 0x03 */
    UInt8  reserved_04;         /* 0x04 */
    UInt8  genOutput;           /* 0x05: controls linker output behavior */
    UInt8  reserved_06[334];    /* 0x06..0x153 */
} PMIPSLinker;

/* "MIPS Project" (60 bytes) */
typedef struct PMIPSProject {
    SInt16 version;             /* 0x00 */
    UInt8  reserved_02;         /* 0x02 */
    UInt8  reserved_03;         /* 0x03 */
    SInt16 projectSetting;      /* 0x04: project-level setting */
    UInt8  reserved_06[54];     /* 0x06..0x3B */
} PMIPSProject;

#pragma pack(pop)

/* Maximum size of the accumulated define/pragma text buffer */
#define DEFINE_TEXT_MAX  (64 * 1024)
/* Virtual prefix file name used for command-line defines/pragmas */
#define CMDLINE_DEFINES_VFILE "(command-line defines)"

/*
 * CWPluginPrivateContext - the context structure passed to the DLL.
 *
 * The DLL does not read this struct directly, so we store our host state here. 
 */
typedef struct CWPluginPrivateContext {
    SInt32    request;
    SInt32    apiVersion;
    SInt32    numFiles;
    SInt32    whichFile;

    /* Source file */
    char          sourceFile[MAX_PATH];
    char*         sourceText;
    SInt32        sourceTextSize;

    /* Output */
    char          outputFile[MAX_PATH];
    void*         objectData;
    SInt32        objectDataSize;
    CWObjectData  storedObject;
    int           objectStored;

    /* Include paths */
    HostAccessPath* userPaths;
    SInt32          userPathCount;
    HostAccessPath* systemPaths;
    SInt32          systemPathCount;

    /* Include search behavior */
    SInt16  includeSearchMode;   /* hostIncludeSearch* */
    Boolean noSysPath;           /* -nosyspath */
    Boolean useDefaultIncludes;  /* -stdinc / -defaults */
    Boolean searchPaths;         /* -search */
    Boolean gccIncludes;         /* -gccincludes set */
    Boolean usedDashIMinus;      /* -I- seen */

    /*
     * Typed preference structs.
     */
    PFrontEndC        prefsFrontEnd;
    PWarningC         prefsWarnings;
    PGlobalOptimizer  prefsOptimizer;

    /* MIPS-specific panels */
    PMIPSCodeGen      prefsMIPSCodeGen;
    PMIPSLinker       prefsMIPSLinker;
    PMIPSProject      prefsMIPSProject;
    UInt8             prefsMIPSCodeGenPanel[sizeof(PMIPSCodeGen)];
    UInt8             prefsMIPSLinkerPanel[sizeof(PMIPSLinker)];
    Boolean           prefsMIPSCodeGenR4Compat;

    /* PPC EABI-specific panels */
    PPCEABICodeGen    prefsPPCCodeGen;
    PPCEABILinker     prefsPPCLinker;
    PPCEABIProject    prefsPPCProject;
    PPreprocessor     prefsPreprocessor;

    /* Define/pragma text exposed as a virtual prefix file */
    char*  defineText;          /* accumulated #define/#undef/#pragma/#include lines */
    SInt32 defineTextLen;

    /* Flags from command line */
    int    preprocess;          /* -E flag */
    int    preprocessOnly;      /* -P flag (preprocess to file, no line markers) */
    int    disassemble;         /* -dis / -disassemble / -S */
    int    disassembleToFile;   /* -S */
    int    debugInfo;           /* -g flag */
    int    verbose;             /* -v flag */
    int    noWarnings;          /* -w off */
    int    warningsAreErrors;   /* -w err / -Werror */
    int    maxErrors;           /* -maxerrors N (0=unlimited) */
    int    maxWarnings;         /* -maxwarnings N (0=unlimited) */
    int    msgStyle;            /* 0=std, 1=gcc, 2=parseable */
    int    noFail;              /* -nofail (continue after per-file failures) */
    int    forceIncludeOnce;    /* -once / -notonce host-side compatibility */
    int    dependencyMode;      /* 0=off, 1=deps-only (-M/-MM/-make), 2=deps+compile (-MD/-MMD) */
    int    depsOnlyUserFiles;   /* -MM / -MMD */
    char   dependencyOutputFile[MAX_PATH]; /* -o makefile path for deps-only mode */

    /* Error tracking */
    int    numErrors;
    int    numWarnings;

    /* File ID counter for includes */
    short  nextFileID;
    HostFileRecord* fileRecords;
    SInt32          fileRecordCount;
    SInt32          fileRecordCap;
    char            lastIncludeDir[MAX_PATH];
    HostIncludeRecord* includeRecords;
    SInt32             includeRecordCount;
    SInt32             includeRecordCap;

    /* Preprocessed output text */
    char*  preprocessedText;
    SInt32 preprocessedTextSize;
} CWPluginPrivateContext;

#endif /* HOST_CTX_H */
