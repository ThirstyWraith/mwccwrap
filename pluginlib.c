/*
 * pluginlib.c - PluginLib shim for CodeWarrior compiler DLLs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <windows.h>
#include "cw_types.h"
#include "host_ctx.h"

CWPluginContext g_context = NULL;

#define LOG(fmt, ...) do { if (g_context && g_context->verbose) { fprintf(stderr, "[PluginLib" STRINGIFY(PLUGINLIB_VER) "] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } } while(0)
#define STUB(name) LOG("STUB: %s called", name)

/* Forward declarations */
CW_CALLBACK CWGetMemHandleSize(CWPluginContext context, CWMemHandle handle, SInt32* size);
CW_CALLBACK CWLockMemHandle(CWPluginContext context, CWMemHandle handle, Boolean moveHi, void** ptr);
CW_CALLBACK CWUnlockMemHandle(CWPluginContext context, CWMemHandle handle);

/*
 * Convert line endings to \r (Mac convention) in-place.
 * Matches MWCC FixTextHandle(): \n -> \r, \r\n -> \r\n (unchanged).
 * Buffer can only shrink (standalone \n replaced by \r, same length;
 * \r\n pairs are left alone). Returns new size.
 */
static SInt32 fix_text_line_endings(char* buf, SInt32 size) {
    SInt32 out = 0;
    for (SInt32 i = 0; i < size; i++) {
        if (buf[i] == '\r') {
            buf[out++] = '\r';
            if (i + 1 < size && buf[i + 1] == '\n') {
                buf[out++] = '\n';
                i++; /* skip the \n of \r\n pair */
            }
        } else if (buf[i] == '\n') {
            buf[out++] = '\r'; /* convert standalone \n to \r */
        } else {
            buf[out++] = buf[i];
        }
    }
    buf[out] = '\0';
    return out;
}

/*
 * Read a file into a malloc'd buffer and convert line endings to \r.
 * Returns NULL on failure. Sets *out_size to the converted size.
 */
static char* read_and_fix_file(const char* path, SInt32* out_size) {
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

    *out_size = fix_text_line_endings(buf, (SInt32)size);
    return buf;
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

static void cwfilespec_from_cpath(CWFileSpec* spec, const char* path) {
    if (!spec) return;
    if (!path) path = "";
#if PLUGINLIB_VER == 2
    size_t len;
    memset(spec, 0, sizeof(*spec));
    len = strlen(path);
    if (len > 255) len = 255;
    spec->name[0] = (UInt8)len;
    if (len > 0) {
        memcpy(spec->name + 1, path, len);
    }
#else
    copy_cstr(spec->path, MAX_PATH, path);
#endif
}

static void cwfilespec_to_cpath(const CWFileSpec* spec, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!spec) return;
#if PLUGINLIB_VER == 2
    size_t len = spec->name[0];
    if (len > 255) len = 255;
    if (len + 1 > out_size) len = out_size - 1;
    if (len > 0) {
        memcpy(out, spec->name + 1, len);
    }
    out[len] = '\0';
#else
    copy_cstr(out, out_size, spec->path);
#endif
}

static void cw_filename_arg_to_cpath(const char* filename, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!filename) return;

#if PLUGINLIB_VER == 2
    {
        UInt8 len = (UInt8)filename[0];
        if (len > 0 && len < out_size && memchr(filename + 1, '\0', len) == NULL) {
            memcpy(out, filename + 1, len);
            out[len] = '\0';
            return;
        }
    }
#endif

    copy_cstr(out, out_size, filename);
}

static int join_path(char* out, size_t out_size, const char* dir, const char* leaf) {
    int n;
    if (!out || out_size == 0 || !dir || !leaf) return 0;
    n = snprintf(out, out_size, "%s/%s", dir, leaf);
    return n > 0 && (size_t)n < out_size;
}

static void get_directory(const char* filepath, char* dir, size_t dirsize) {
    char* sep;
    copy_cstr(dir, dirsize, filepath);
    sep = strrchr(dir, '\\');
    if (!sep) sep = strrchr(dir, '/');
    if (sep) *(sep + 1) = '\0';
    else copy_cstr(dir, dirsize, ".");
}

static int is_full_path(const char* path) {
    if (!path || !path[0]) return 0;
    if ((isalpha((unsigned char)path[0]) && path[1] == ':') ||
        path[0] == '\\' || path[0] == '/')
        return 1;
    return 0;
}

static int ensure_file_record_capacity(CWPluginContext ctx) {
    HostFileRecord* recs;
    SInt32 new_cap;
    if (ctx->fileRecordCount < ctx->fileRecordCap) return 1;
    new_cap = (ctx->fileRecordCap > 0) ? (ctx->fileRecordCap * 2) : 64;
    recs = (HostFileRecord*)realloc(ctx->fileRecords, (size_t)new_cap * sizeof(HostFileRecord));
    if (!recs) return 0;
    ctx->fileRecords = recs;
    ctx->fileRecordCap = new_cap;
    return 1;
}

static void record_file_id(CWPluginContext ctx, short file_id, const char* path, Boolean is_system) {
    for (SInt32 i = 0; i < ctx->fileRecordCount; i++) {
        if (ctx->fileRecords[i].fileID == file_id) {
            copy_cstr(ctx->fileRecords[i].path, MAX_PATH, path);
            ctx->fileRecords[i].isSystem = is_system;
            return;
        }
    }
    if (!ensure_file_record_capacity(ctx)) return;
    ctx->fileRecords[ctx->fileRecordCount].fileID = file_id;
    ctx->fileRecords[ctx->fileRecordCount].isSystem = is_system;
    copy_cstr(ctx->fileRecords[ctx->fileRecordCount].path, MAX_PATH, path);
    ctx->fileRecordCount++;
}

static const char* lookup_file_id_path(const CWPluginContext ctx, short file_id) {
    for (SInt32 i = 0; i < ctx->fileRecordCount; i++) {
        if (ctx->fileRecords[i].fileID == file_id)
            return ctx->fileRecords[i].path;
    }
    return NULL;
}

static void normalize_include_path(const char* path, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!path || !path[0]) return;

    copy_cstr(out, out_size, path);

    for (size_t i = 0; out[i]; i++) {
        if (out[i] == '\\') out[i] = '/';
        out[i] = (char)tolower((unsigned char)out[i]);
    }
}

static int ensure_include_record_capacity(CWPluginContext ctx) {
    HostIncludeRecord* recs;
    SInt32 new_cap;
    if (ctx->includeRecordCount < ctx->includeRecordCap) return 1;
    new_cap = (ctx->includeRecordCap > 0) ? (ctx->includeRecordCap * 2) : 64;
    recs = (HostIncludeRecord*)realloc(ctx->includeRecords, (size_t)new_cap * sizeof(HostIncludeRecord));
    if (!recs) return 0;
    ctx->includeRecords = recs;
    ctx->includeRecordCap = new_cap;
    return 1;
}

static Boolean was_include_loaded(const CWPluginContext ctx, const char* path) {
    char normalized[MAX_PATH];
    if (!ctx || !path || !path[0]) return FALSE;
    normalize_include_path(path, normalized, sizeof(normalized));
    if (!normalized[0]) return FALSE;

    for (SInt32 i = 0; i < ctx->includeRecordCount; i++) {
        if (strcmp(ctx->includeRecords[i].path, normalized) == 0)
            return TRUE;
    }
    return FALSE;
}

static void mark_include_loaded(CWPluginContext ctx, const char* path) {
    char normalized[MAX_PATH];
    if (!ctx || !path || !path[0]) return;
    normalize_include_path(path, normalized, sizeof(normalized));
    if (!normalized[0]) return;

    for (SInt32 i = 0; i < ctx->includeRecordCount; i++) {
        if (strcmp(ctx->includeRecords[i].path, normalized) == 0)
            return;
    }

    if (!ensure_include_record_capacity(ctx)) return;
    copy_cstr(ctx->includeRecords[ctx->includeRecordCount].path, MAX_PATH, normalized);
    ctx->includeRecordCount++;
}

static CWResult load_file_for_include(const char* path, CWFileInfo* fileinfo,
                                      CWPluginContext ctx, Boolean suppressload, Boolean is_system)
{
    SInt32 size;
    char* buf = NULL;
    Boolean already_included;

    already_included = was_include_loaded(ctx, path);
    if (!suppressload) {
        if (already_included && ctx->forceIncludeOnce) {
            /* Match mwccps2 -once behavior even when cc_mips.dll doesn't
             * honor #pragma once on/off toggles from the frontend. */
            buf = (char*)malloc(1);
            if (!buf) return cwErrOutOfMemory;
            buf[0] = '\0';
            fileinfo->filedata = buf;
            fileinfo->filedatalength = 0;
        } else {
            buf = read_and_fix_file(path, &size);
            if (!buf) return cwErrFileNotFound;
            fileinfo->filedata = buf;
            fileinfo->filedatalength = size;
        }
    } else {
        if (!is_full_path(path) && !strchr(path, '/')) {
            /* For suppress-load checks, existence still matters for leaf names. */
            FILE* f = fopen(path, "rb");
            if (!f) return cwErrFileNotFound;
            fclose(f);
        } else {
            FILE* f = fopen(path, "rb");
            if (!f) return cwErrFileNotFound;
            fclose(f);
        }
        fileinfo->filedata = NULL;
        fileinfo->filedatalength = 0;
    }

    fileinfo->filedatatype = cwFileTypeText;
    fileinfo->fileID = ctx->nextFileID++;
    cwfilespec_from_cpath(&fileinfo->filespec, path);
    fileinfo->alreadyincluded = already_included;
    fileinfo->recordbrowseinfo = FALSE;
    record_file_id(ctx, fileinfo->fileID, path, is_system);
    if (!suppressload) {
        mark_include_loaded(ctx, path);
    }
    get_directory(path, ctx->lastIncludeDir, sizeof(ctx->lastIncludeDir));
    return cwNoErr;
}

static CWResult try_load_include_recursive(const char* dir, const char* filename,
                                           CWFileInfo* fileinfo, CWPluginContext ctx,
                                           Boolean suppressload, Boolean is_system, int depth)
{
    char pattern[MAX_PATH];
    char candidate[MAX_PATH];
    WIN32_FIND_DATAA ffd;
    HANDLE h;

    if (depth > 32) return cwErrFileNotFound;

    if (!join_path(pattern, sizeof(pattern), dir, "*")) return cwErrFileNotFound;
    h = FindFirstFileA(pattern, &ffd);
    if (h == INVALID_HANDLE_VALUE) return cwErrFileNotFound;

    do {
        char child[MAX_PATH];
        CWResult r;
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
        if (!join_path(child, sizeof(child), dir, ffd.cFileName)) continue;
        if (!join_path(candidate, sizeof(candidate), child, filename)) continue;
        r = load_file_for_include(candidate, fileinfo, ctx, suppressload, is_system);
        if (r == cwNoErr) {
            FindClose(h);
            return cwNoErr;
        }

        if (try_load_include_recursive(child, filename, fileinfo, ctx, suppressload, is_system, depth + 1) == cwNoErr)
        {
            FindClose(h);
            return cwNoErr;
        }
    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    return cwErrFileNotFound;
}

/*
 * Helper: try to load a file from a directory path + filename.
 * If recursive is TRUE, scan subdirectories too (MWCC -ir behavior).
 */
static CWResult try_load_include(const char* dir, const char* filename,
    CWFileInfo* fileinfo, CWPluginContext ctx, Boolean suppressload, Boolean recursive,
    Boolean is_system)
{
    char fullpath[MAX_PATH];

    if (!join_path(fullpath, sizeof(fullpath), dir, filename)) return cwErrFileNotFound;
    if (load_file_for_include(fullpath, fileinfo, ctx, suppressload, is_system) == cwNoErr) {
        return cwNoErr;
    }
    if (recursive) {
        return try_load_include_recursive(dir, filename, fileinfo, ctx, suppressload, is_system, 0);
    }
    return cwErrFileNotFound;
}

/* ============================================================
 * CW Plugin Core
 * ============================================================ */

CW_CALLBACK CWGetPluginRequest(CWPluginContext context, SInt32* request) {
    g_context = context;
    LOG("CWGetPluginRequest(context=%p)", context);
    if (!context || !request) return cwErrInvalidParameter;
    *request = context->request;
    LOG("  request=%d", *request);
    return cwNoErr;
}

CW_CALLBACK CWDonePluginRequest(CWPluginContext context, CWResult resultCode) {
    LOG("CWDonePluginRequest(result=%d)", resultCode);
    return cwNoErr;
}

CW_CALLBACK CWGetAPIVersion(CWPluginContext context, SInt32* version) {
    LOG("CWGetAPIVersion");
    if (!context || !version) return cwErrInvalidParameter;
    *version = context->apiVersion;
    return cwNoErr;
}

CW_CALLBACK CWGetProjectFile(CWPluginContext context, CWFileSpec* projectSpec) {
    LOG("CWGetProjectFile");
    if (!context || !projectSpec) return cwErrInvalidParameter;
    memset(projectSpec, 0, sizeof(*projectSpec)); // TODO: stub
    return cwNoErr;
}

CW_CALLBACK CWGetProjectFileCount(CWPluginContext context, SInt32* count) {
    LOG("CWGetProjectFileCount");
    if (!context || !count) return cwErrInvalidParameter;
    *count = context->numFiles;
    return cwNoErr;
}

CW_CALLBACK CWGetOutputFileDirectory(CWPluginContext context, CWFileSpec* outputFileDirectory) {
    char outdir[MAX_PATH];

    LOG("CWGetOutputFileDirectory");
    if (!context || !outputFileDirectory) return cwErrInvalidParameter;

    if (context->outputFile[0]) {
        get_directory(context->outputFile, outdir, sizeof(outdir));
    } else {
        DWORD n = GetCurrentDirectoryA(sizeof(outdir), outdir);
        if (n == 0 || n >= sizeof(outdir)) return cwErrRequestFailed;
    }

    cwfilespec_from_cpath(outputFileDirectory, outdir);
    return cwNoErr;
}

CW_CALLBACK CWGetFileInfo(CWPluginContext context, SInt32 whichfile, Boolean checkFileLocation,
    CWProjectFileInfo* fileinfo)
{
    const char* path = NULL;

    LOG("CWGetFileInfo(whichfile=%d)", (int)whichfile);
    if (!context || !fileinfo) return cwErrInvalidParameter;

    memset(fileinfo, 0, sizeof(*fileinfo));
    fileinfo->fileID = (short)whichfile;
    fileinfo->gendebug = context->debugInfo ? TRUE : FALSE;

    (void)checkFileLocation;

    if (whichfile == context->whichFile && context->sourceFile[0]) {
        path = context->sourceFile;
    } else {
        path = lookup_file_id_path(context, (short)whichfile);
    }

    if (path && path[0]) {
        cwfilespec_from_cpath(&fileinfo->filespec, path);
        GetSystemTimeAsFileTime(&fileinfo->moddate);
        return cwNoErr;
    }

    return cwErrUnknownFile;
}

CW_CALLBACK CWGetOverlay1GroupsCount(CWPluginContext context, SInt32* count) {
    LOG("CWGetOverlay1GroupsCount");
    if (!context || !count) return cwErrInvalidParameter;
    *count = 0;
    return cwNoErr;
}

CW_CALLBACK CWGetOverlay1GroupInfo(CWPluginContext context, SInt32 whichgroup,
    CWOverlay1GroupInfo* groupinfo)
{
    LOG("CWGetOverlay1GroupInfo(whichgroup=%d)", (int)whichgroup);
    if (!context || !groupinfo) return cwErrInvalidParameter;
    memset(groupinfo, 0, sizeof(*groupinfo));
    return cwErrUnknownSegment;
}

CW_CALLBACK CWGetOverlay1Info(CWPluginContext context, SInt32 whichgroup, SInt32 whichoverlay,
    CWOverlay1Info* overlayinfo)
{
    LOG("CWGetOverlay1Info(whichgroup=%d, whichoverlay=%d)", (int)whichgroup, (int)whichoverlay);
    if (!context || !overlayinfo) return cwErrInvalidParameter;
    memset(overlayinfo, 0, sizeof(*overlayinfo));
    return cwErrUnknownSegment;
}

CW_CALLBACK CWGetOverlay1FileInfo(CWPluginContext context, SInt32 whichgroup,
    SInt32 whichoverlay, SInt32 whichoverlayfile, CWOverlay1FileInfo* fileinfo)
{
    LOG("CWGetOverlay1FileInfo(whichgroup=%d, whichoverlay=%d, whichoverlayfile=%d)",
        (int)whichgroup, (int)whichoverlay, (int)whichoverlayfile);
    if (!context || !fileinfo) return cwErrInvalidParameter;
    memset(fileinfo, 0, sizeof(*fileinfo));
    return cwErrUnknownSegment;
}

CW_CALLBACK CWAlert(CWPluginContext context, const char* msg1, const char* msg2,
    const char* msg3, const char* msg4)
{
    LOG("CWAlert");
    if (!context) return cwErrInvalidParameter;
    if (msg1) fprintf(stderr, "%s", msg1);
    if (msg2) fprintf(stderr, " %s", msg2);
    if (msg3) fprintf(stderr, " %s", msg3);
    if (msg4) fprintf(stderr, " %s", msg4);
    if (msg1 || msg2 || msg3 || msg4) fprintf(stderr, "\n");
    return cwNoErr;
}

CW_CALLBACK CWShowStatus(CWPluginContext context, const char* line1, const char* line2) {
    if (line1) fprintf(stderr, "%s", line1);
    if (line2) fprintf(stderr, " %s", line2);
    if (line1 || line2) fprintf(stderr, "\n");
    return cwNoErr;
}

CW_CALLBACK CWUserBreak(CWPluginContext context) {
    STUB("CWUserBreak");
    return cwNoErr;
}

/*
 * The compiler reports line numbers in a shifted 16.16 form
 * (e.g. line 2 arrives as 0x00020000). Normalize before printing.
 */
static SInt32 normalize_message_line(SInt32 raw_line) {
    UInt32 line = (UInt32)raw_line;
    if ((line & 0xFFFFu) == 0 && (line >> 16) != 0)
        return (SInt32)(line >> 16);
    return raw_line;
}

CW_CALLBACK CWReportMessage(CWPluginContext ctx,
    const CWMessageRef* msgRef, const char* line1, const char* line2,
    short errorlevel, SInt32 errorNumber)
{
    const char* level_str = "info";
    SInt32 linenumber = 0;
    char source_path[MAX_PATH];
    source_path[0] = '\0';
    if (msgRef) {
        const CWMessageRef* msg = msgRef;
        linenumber = normalize_message_line(msg->linenumber);
        cwfilespec_to_cpath(&msg->sourcefile, source_path, sizeof(source_path));
    }

    if (errorlevel == messagetypeWarning) {
        level_str = "warning";
        ctx->numWarnings++;
    } else if (errorlevel == messagetypeError) {
        level_str = "error";
        ctx->numErrors++;
    }

    if (source_path[0]) {
        fprintf(stderr, "%s:%d: %s: ", source_path, (int)linenumber, level_str);
    } else {
        fprintf(stderr, "%s: ", level_str);
    }

    if (line1) fprintf(stderr, "%s", line1);
    if (line2) fprintf(stderr, "\n  %s", line2);
    fprintf(stderr, "\n");

    return cwNoErr;
}

CW_CALLBACK CWSetModDate(CWPluginContext ctx,
    const CWFileSpec* filespec, CWFileTime* moddate, Boolean isGenerated)
{
    STUB("CWSetModDate");
    return cwNoErr;
}

CW_CALLBACK CWCreateNewTextDocument(CWPluginContext ctx,
    const CWNewTextDocumentInfo* docinfo)
{
    LOG("CWCreateNewTextDocument");
    /* Used for preprocessor output (-E mode) */
    if (!ctx || !docinfo) return cwErrInvalidParameter;

    if (ctx->preprocess == 2 && !ctx->preprocess) {
        /*
         * MWCC dependency pass (-M/-MM/-make) runs with preprocess mode 2.
         * This pass is for dependency collection, not text emission.
         */
        return cwNoErr;
    }
    if (docinfo->text) {
        void* ptr = NULL;
        SInt32 size = 0;
        SInt32 emitted = 0;
        CWGetMemHandleSize(ctx, docinfo->text, &size);
        CWLockMemHandle(ctx, docinfo->text, FALSE, &ptr);
        if (ptr) {
            /* Fall back to strlen if size is unknown (e.g. handle allocated
             * internally by the DLL without going through COS_NewHandle) */
            if (size <= 0)
                size = (SInt32)strlen((const char*)ptr);
            /* Strip trailing null terminator if present (matches MWCC) */
            if (size > 0 && ((const char*)ptr)[size - 1] == '\0')
                size--;
            if (size > 0) {
                /*
                 * Normalize line endings: \r\n -> \n, \r -> \n.
                 * The DLL uses Mac-convention \r for line endings.
                 * Matches MWCC reference SendHandleToFile() behavior.
                 */
                const char* p = (const char*)ptr;
                const char* end = p + size;
                while (p < end) {
                    const char* lineEnd = p;
                    while (lineEnd < end && *lineEnd != '\r' && *lineEnd != '\n')
                        lineEnd++;
                    if (lineEnd > p) {
                        fwrite(p, 1, lineEnd - p, stdout);
                        emitted += (SInt32)(lineEnd - p);
                    }
                    fputc('\n', stdout);
                    emitted++;
                    if (lineEnd < end) {
                        if (*lineEnd == '\r' && lineEnd + 1 < end && *(lineEnd + 1) == '\n')
                            lineEnd++;
                        lineEnd++;
                    }
                    p = lineEnd;
                }
                fflush(stdout);
                ctx->preprocessedTextSize += emitted;
            }
            CWUnlockMemHandle(ctx, docinfo->text);
        }
    }
    return cwNoErr;
}

/* ============================================================
 * Memory Handle Management
 * ============================================================ */

CW_CALLBACK CWAllocateMemory(CWPluginContext ctx, SInt32 size, Boolean isPermanent, void** ptr) {
    void* p;

    LOG("CWAllocateMemory(size=%d, isPermanent=%d)", (int)size, (int)isPermanent);
    if (!ctx || !ptr) return cwErrInvalidParameter;
    if (size < 0) return cwErrInvalidParameter;

    if (size == 0) {
        *ptr = NULL;
        return cwNoErr;
    }

    p = calloc(1, (size_t)size);
    if (!p) return cwErrOutOfMemory;
    *ptr = p;
    return cwNoErr;
}

CW_CALLBACK CWFreeMemory(CWPluginContext ctx, void* ptr, Boolean isPermanent) {
    LOG("CWFreeMemory(ptr=%p, isPermanent=%d)", ptr, (int)isPermanent);
    if (!ctx) return cwErrInvalidParameter;
    if (ptr) free(ptr);
    return cwNoErr;
}

CW_CALLBACK CWAllocMemHandle(CWPluginContext ctx,
    SInt32 size, Boolean useTempMemory, CWMemHandle* handle)
{
    LOG("CWAllocMemHandle(size=%d)", size);
    if (!handle) return cwErrInvalidParameter;

    CWMemHandleImpl* h = (CWMemHandleImpl*)calloc(1, sizeof(CWMemHandleImpl));
    if (!h) return cwErrOutOfMemory;

    if (size > 0) {
        h->data = calloc(1, size);
        if (!h->data) {
            free(h);
            return cwErrOutOfMemory;
        }
    }
    h->size = size;
    h->locked = 0;
    *handle = (CWMemHandle)h;
    return cwNoErr;
}

CW_CALLBACK CWFreeMemHandle(CWPluginContext ctx, CWMemHandle handle) {
    LOG("CWFreeMemHandle");
    if (!handle) return cwErrInvalidParameter;

    CWMemHandleImpl* h = (CWMemHandleImpl*)handle;
    if (h->data) free(h->data);
    free(h);
    return cwNoErr;
}

CW_CALLBACK CWGetMemHandleSize(CWPluginContext ctx,
    CWMemHandle handle, SInt32* size)
{
    if (!handle || !size) return cwErrInvalidParameter;
    CWMemHandleImpl* h = (CWMemHandleImpl*)handle;
    *size = h->size;
    return cwNoErr;
}

CW_CALLBACK CWResizeMemHandle(CWPluginContext ctx,
    CWMemHandle handle, SInt32 newSize)
{
    LOG("CWResizeMemHandle(newSize=%d)", newSize);
    if (!handle) return cwErrInvalidParameter;
    CWMemHandleImpl* h = (CWMemHandleImpl*)handle;
    void* newdata = realloc(h->data, newSize);
    if (!newdata && newSize > 0) return cwErrOutOfMemory;
    h->data = newdata;
    h->size = newSize;
    return cwNoErr;
}

CW_CALLBACK CWLockMemHandle(CWPluginContext ctx,
    CWMemHandle handle, Boolean moveHi, void** ptr)
{
    if (!handle || !ptr) return cwErrInvalidParameter;
    CWMemHandleImpl* h = (CWMemHandleImpl*)handle;
    h->locked++;
    *ptr = h->data;
    return cwNoErr;
}

CW_CALLBACK CWUnlockMemHandle(CWPluginContext ctx, CWMemHandle handle) {
    if (!handle) return cwErrInvalidParameter;
    CWMemHandleImpl* h = (CWMemHandleImpl*)handle;
    if (h->locked > 0) h->locked--;
    return cwNoErr;
}

/* ============================================================
 * Source File Access
 * ============================================================ */

CW_CALLBACK CWGetMainFileSpec(CWPluginContext ctx, CWFileSpec* fileSpec) {
    LOG("CWGetMainFileSpec");
    if (!ctx || !fileSpec) return cwErrInvalidParameter;
    cwfilespec_from_cpath(fileSpec, ctx->sourceFile);
    return cwNoErr;
}

CW_CALLBACK CWGetMainFileText(CWPluginContext ctx,
    const char** text, SInt32* textLength)
{
    LOG("CWGetMainFileText");
    if (!ctx || !text || !textLength) return cwErrInvalidParameter;

    if (!ctx->sourceText) return cwErrFileNotFound;

    *text = ctx->sourceText;
    *textLength = ctx->sourceTextSize;
    return cwNoErr;
}

CW_CALLBACK CWGetMainFileNumber(CWPluginContext ctx, SInt32* fileNumber) {
    LOG("CWGetMainFileNumber");
    if (!ctx || !fileNumber) return cwErrInvalidParameter;
    *fileNumber = ctx->whichFile;
    return cwNoErr;
}

CW_CALLBACK CWGetMainFileID(CWPluginContext ctx, short* fileID) {
    LOG("CWGetMainFileID");
    if (!ctx || !fileID) return cwErrInvalidParameter;
    *fileID = (short)(ctx->whichFile + 1);
    return cwNoErr;
}

CW_CALLBACK CWGetFileText(CWPluginContext ctx,
    const CWFileSpec* filespec, const char** text, SInt32* textLength, short* filedatatype)
{
    char path[MAX_PATH];
    path[0] = '\0';
    if (filespec) {
        cwfilespec_to_cpath(filespec, path, sizeof(path));
    }
    LOG("CWGetFileText(%s)", filespec ? path : "NULL");
    if (!ctx || !filespec || !text || !textLength) return cwErrInvalidParameter;

    SInt32 size;
    char* buf = read_and_fix_file(path, &size);
    if (!buf) return cwErrFileNotFound;

    *text = buf;
    *textLength = size;
    if (filedatatype) *filedatatype = cwFileTypeText;
    return cwNoErr;
}

CW_CALLBACK CWReleaseFileText(CWPluginContext ctx, const char* text) {
    LOG("CWReleaseFileText");
    if (text && ctx) {
        /* Don't free the main source text - we own that */
        if (text != ctx->sourceText) {
            free((void*)text);
        }
    }
    return cwNoErr;
}

static int is_cmdline_defines_name(const char* filename) {
    const size_t name_len = strlen(CMDLINE_DEFINES_VFILE);
    static const char* alt_name = "command-line defines)";
    const size_t alt_len = 21;
    if (!filename) return 0;

    /* C-string form */
    if (strncmp(filename, CMDLINE_DEFINES_VFILE, name_len) == 0 && filename[name_len] == '\0')
        return 1;
    if (strncmp(filename, alt_name, alt_len) == 0 && filename[alt_len] == '\0')
        return 1;

    /* Pascal Str31 form */
    if ((unsigned char)filename[0] == name_len &&
        memcmp(filename + 1, CMDLINE_DEFINES_VFILE, name_len) == 0)
        return 1;
    if ((unsigned char)filename[0] == alt_len &&
        memcmp(filename + 1, alt_name, alt_len) == 0)
        return 1;

    return 0;
}

CW_CALLBACK CWFindAndLoadFile(CWPluginContext ctx,
    const char* filename, CWFileInfo* fileinfo)
{
    CWFileInfo* fileinfo_out;
    if (!ctx || !filename || !fileinfo) return cwErrInvalidParameter;

    fileinfo_out = fileinfo;

    /* Keep request fields before clearing output. */
    Boolean fullsearch = fileinfo_out->fullsearch;
    SInt32 dependent_file = fileinfo_out->isdependentoffile;
    Boolean suppressload = fileinfo_out->suppressload;

    /*
     * Copy filename BEFORE memset: the DLL may pass a pointer that overlaps
     * with fileinfo storage.
     */
    char fname[MAX_PATH];
    char special_dir[MAX_PATH];
    int have_special_dir = 0;

    if (ctx->fileRecordCount == 0 && ctx->sourceFile[0]) {
        record_file_id(ctx, (short)(ctx->whichFile + 1), ctx->sourceFile, FALSE);
    }

    cw_filename_arg_to_cpath(filename, fname, sizeof(fname));
    LOG("CWFindAndLoadFile(%s, fullsearch=%d, dep=%d, suppress=%d)",
        fname, (int)fullsearch, (int)dependent_file, (int)suppressload);

    if (is_cmdline_defines_name(fname)) {
        filename = CMDLINE_DEFINES_VFILE;
    } else {
        filename = fname;
    }

    memset(fileinfo_out, 0, sizeof(*fileinfo_out));

    /* MWCC-style command-line virtual prefix file. */
    if (ctx->defineText && ctx->defineTextLen > 0 &&
        strcmp(filename, CMDLINE_DEFINES_VFILE) == 0)
    {
        if (!suppressload) {
            char* buf = (char*)malloc((size_t)ctx->defineTextLen + 1);
            if (!buf) return cwErrOutOfMemory;
            memcpy(buf, ctx->defineText, (size_t)ctx->defineTextLen);
            buf[ctx->defineTextLen] = '\0';
            fileinfo_out->filedata = buf;
            fileinfo_out->filedatalength = ctx->defineTextLen;
        } else {
            fileinfo_out->filedata = NULL;
            fileinfo_out->filedatalength = 0;
        }

        fileinfo_out->filedatatype = cwFileTypeText;
        fileinfo_out->fileID = 0;
        cwfilespec_from_cpath(&fileinfo_out->filespec, CMDLINE_DEFINES_VFILE);
        fileinfo_out->alreadyincluded = FALSE;
        fileinfo_out->recordbrowseinfo = FALSE;
        return cwNoErr;
    }

    fullsearch = (fullsearch || ctx->noSysPath) ? TRUE : FALSE;

    if (is_full_path(filename)) {
        if (load_file_for_include(filename, fileinfo, ctx, suppressload, FALSE) == cwNoErr) {
            return cwNoErr;
        }
    }

    switch (ctx->includeSearchMode) {
        case hostIncludeSearchProj:
            have_special_dir = GetCurrentDirectoryA(MAX_PATH, special_dir) > 0;
            break;
        case hostIncludeSearchSource:
            if (ctx->sourceFile[0]) {
                get_directory(ctx->sourceFile, special_dir, sizeof(special_dir));
                have_special_dir = 1;
            }
            break;
        case hostIncludeSearchInclude:
            if (fullsearch && dependent_file >= 0) {
                const char* dep_path = lookup_file_id_path(ctx, (short)dependent_file);
                if (dep_path && dep_path[0]) {
                    get_directory(dep_path, special_dir, sizeof(special_dir));
                    have_special_dir = 1;
                }
            }
            if (fullsearch && !have_special_dir && ctx->lastIncludeDir[0]) {
                copy_cstr(special_dir, sizeof(special_dir), ctx->lastIncludeDir);
                have_special_dir = 1;
            }
            if (!have_special_dir && ctx->sourceFile[0]) {
                get_directory(ctx->sourceFile, special_dir, sizeof(special_dir));
                have_special_dir = 1;
            }
            break;
        case hostIncludeSearchExplicit:
        default:
            break;
    }

    if (have_special_dir &&
        try_load_include(special_dir, filename, fileinfo, ctx, suppressload, FALSE, FALSE) == cwNoErr)
    {
        return cwNoErr;
    }

    if (fullsearch) {
        for (SInt32 i = 0; i < ctx->userPathCount; i++) {
            if (try_load_include(ctx->userPaths[i].path, filename, fileinfo,
                    ctx, suppressload, ctx->userPaths[i].recursive, FALSE) == cwNoErr)
                return cwNoErr;
        }
    }

    for (SInt32 i = 0; i < ctx->systemPathCount; i++) {
        if (try_load_include(ctx->systemPaths[i].path, filename, fileinfo,
                ctx, suppressload, ctx->systemPaths[i].recursive, TRUE) == cwNoErr)
            return cwNoErr;
    }

    fprintf(stderr, "Cannot find include file: %s\n", filename);
    return cwErrFileNotFound;
}

/* ============================================================
 * Object Data Storage
 * ============================================================ */

CW_CALLBACK CWStoreObjectData(CWPluginContext ctx,
    SInt32 whichfile, CWObjectData* object)
{
    LOG("CWStoreObjectData(whichfile=%d, codesize=%d, udatasize=%d, idatasize=%d)",
        whichfile, object ? object->codesize : 0,
        object ? object->udatasize : 0, object ? object->idatasize : 0);

    if (!ctx || !object) return cwErrInvalidParameter;

    ctx->storedObject = *object;
    ctx->objectStored = 1;

    /* Copy the object data to our own buffer */
    if (object->objectdata) {
        void* ptr = NULL;
        SInt32 size = 0;
        CWGetMemHandleSize(ctx, object->objectdata, &size);
        CWLockMemHandle(ctx, object->objectdata, FALSE, &ptr);
        LOG("  objectdata: ptr=%p, size=%d", ptr, size);

        if (ptr && size > 0) {
            ctx->objectData = malloc(size);
            if (ctx->objectData) {
                memcpy(ctx->objectData, ptr, size);
                ctx->objectDataSize = size;
                LOG("  Captured %d bytes of object data", size);
            }
        } else if (ptr) {
            /* Size unknown (from CWSecretAttachHandle), use codesize+udatasize+idatasize */
            SInt32 totalSize = object->codesize + object->udatasize + object->idatasize;
            if (totalSize <= 0) totalSize = 4096; /* fallback guess */
            LOG("  Using computed size: %d", totalSize);
            ctx->objectData = malloc(totalSize);
            if (ctx->objectData) {
                memcpy(ctx->objectData, ptr, totalSize);
                ctx->objectDataSize = totalSize;
                LOG("  Captured %d bytes of object data (computed)", totalSize);
            }
        }
        CWUnlockMemHandle(ctx, object->objectdata);
    }

    return cwNoErr;
}

CW_CALLBACK CWLoadObjectData(CWPluginContext ctx,
    SInt32 whichfile, CWMemHandle* objectdata)
{
    LOG("CWLoadObjectData(whichfile=%d)", whichfile);
    if (!ctx || !objectdata) return cwErrInvalidParameter;

    if (!ctx->objectData || ctx->objectDataSize <= 0) {
        return cwErrObjectFileNotStored;
    }

    CWMemHandle h = NULL;
    CWResult r = CWAllocMemHandle(ctx, ctx->objectDataSize, FALSE, &h);
    if (r != cwNoErr) return r;

    void* ptr = NULL;
    r = CWLockMemHandle(ctx, h, FALSE, &ptr);
    if (r != cwNoErr || !ptr) {
        CWFreeMemHandle(ctx, h);
        return r != cwNoErr ? r : cwErrRequestFailed;
    }

    memcpy(ptr, ctx->objectData, (size_t)ctx->objectDataSize);
    CWUnlockMemHandle(ctx, h);
    *objectdata = h;
    return cwNoErr;
}

CW_CALLBACK CWFreeObjectData(CWPluginContext ctx, SInt32 whichfile, CWMemHandle objectdata) {
    LOG("CWFreeObjectData(whichfile=%d, objectdata=%p)", (int)whichfile, objectdata);
    if (!ctx) return cwErrInvalidParameter;
    if (!objectdata) return cwNoErr;
    return CWFreeMemHandle(ctx, objectdata);
}

/* ============================================================
 * Preferences
 * ============================================================ */

static void build_mips_codegen_panel(CWPluginContext ctx) {
    memcpy(ctx->prefsMIPSCodeGenPanel,
           &ctx->prefsMIPSCodeGen,
           sizeof(ctx->prefsMIPSCodeGenPanel));

    if (ctx->prefsMIPSCodeGenR4Compat) {
        /*
         * R4 reads PMIPSCodeGen with a different layout than R5/R5.2.
         * Keep a 20-byte panel, but patch overlapping bytes so both
         * layouts see sensible values.
         */
        ctx->prefsMIPSCodeGenPanel[0x04] = (UInt8)(ctx->prefsMIPSCodeGen.processor & 0xFF);
        ctx->prefsMIPSCodeGenPanel[0x05] = 0;
        ctx->prefsMIPSCodeGenPanel[0x06] = (UInt8)(ctx->prefsMIPSCodeGen.fpuType & 0xFF);
        ctx->prefsMIPSCodeGenPanel[0x07] = (ctx->prefsMIPSCodeGen.fpuType != 0) ? 1 : 0;
        ctx->prefsMIPSCodeGenPanel[0x0C] = ctx->prefsMIPSCodeGen.useIntrinsics;
    }
}

static void build_mips_linker_panel(CWPluginContext ctx) {
    memcpy(ctx->prefsMIPSLinkerPanel,
           &ctx->prefsMIPSLinker,
           sizeof(ctx->prefsMIPSLinkerPanel));

    /*
     * R4 reads genOutput at 0x03, R5/R5.2 reads 0x05.
     * Mirror to both offsets to avoid version branching.
     */
    ctx->prefsMIPSLinkerPanel[0x03] = ctx->prefsMIPSLinker.genOutput;
    ctx->prefsMIPSLinkerPanel[0x05] = ctx->prefsMIPSLinker.genOutput;
}

CW_CALLBACK CWSecretGetNamedPreferences(CWPluginContext ctx,
    const char* prefsname, void** prefsdata)
{
    LOG("CWSecretGetNamedPreferences(\"%s\", %p)", prefsname ? prefsname : "NULL", prefsdata);
    if (!ctx || !prefsname || !prefsdata) return cwErrInvalidParameter;

    const void* prefs = NULL;
    SInt32 prefsSize = 0;
    if (strcmp(prefsname, "C/C++ Compiler") == 0) {
        prefs = &ctx->prefsFrontEnd;
        prefsSize = sizeof(ctx->prefsFrontEnd);
    } else if (strcmp(prefsname, "C/C++ Warnings") == 0) {
        prefs = &ctx->prefsWarnings;
        prefsSize = sizeof(ctx->prefsWarnings);
    } else if (strcmp(prefsname, "Global Optimizer") == 0 ||
               strcmp(prefsname, "PS Global Optimizer") == 0 ||
               strcmp(prefsname, "EPPC Global Optimizer") == 0) {
        prefs = &ctx->prefsOptimizer;
        prefsSize = sizeof(ctx->prefsOptimizer);
    } else if (strcmp(prefsname, "MIPS CodeGen") == 0) {
        build_mips_codegen_panel(ctx);
        prefs = ctx->prefsMIPSCodeGenPanel;
        prefsSize = sizeof(ctx->prefsMIPSCodeGenPanel);
    } else if (strcmp(prefsname, "MIPS Linker Panel") == 0) {
        build_mips_linker_panel(ctx);
        prefs = ctx->prefsMIPSLinkerPanel;
        prefsSize = sizeof(ctx->prefsMIPSLinkerPanel);
    } else if (strcmp(prefsname, "MIPS Project") == 0) {
        prefs = &ctx->prefsMIPSProject;
        prefsSize = sizeof(ctx->prefsMIPSProject);
    } else if (strcmp(prefsname, "IR Optimizer") == 0) {
        /* CW PS R4/R4.1 vestigial panel: data never read, just return zeros */
        prefsSize = 12;
    }
    /* ---- PPC EABI panels (GC/Wii target) ---- */
    else if (strcmp(prefsname, "PPC EABI CodeGen") == 0) {
        prefs = &ctx->prefsPPCCodeGen;
        prefsSize = sizeof(ctx->prefsPPCCodeGen);
    } else if (strcmp(prefsname, "PPC EABI Linker") == 0) {
        prefs = &ctx->prefsPPCLinker;
        prefsSize = sizeof(ctx->prefsPPCLinker);
    } else if (strcmp(prefsname, "PPC EABI Project") == 0) {
        prefs = &ctx->prefsPPCProject;
        prefsSize = sizeof(ctx->prefsPPCProject);
    } else if (strcmp(prefsname, "C/C++ Preprocessor") == 0) {
        prefs = &ctx->prefsPreprocessor;
        prefsSize = sizeof(ctx->prefsPreprocessor);
    } else {
        /* Return a zeroed blob for any unknown panel */
        LOG("  Unknown preference panel: %s", prefsname);
        prefsSize = 256;
    }

    CWAllocMemHandle(ctx, prefsSize, FALSE, (CWMemHandle*)prefsdata);
    if (prefs && prefsSize > 0) {
        void* ptr = NULL;
        CWLockMemHandle(ctx, *(CWMemHandle*)prefsdata, FALSE, &ptr);
        if (ptr) {
            memcpy(ptr, prefs, (size_t)prefsSize);
        }
        CWUnlockMemHandle(ctx, *(CWMemHandle*)prefsdata);
    }
    return cwNoErr;
}

CW_CALLBACK CWGetNamedPreferences(CWPluginContext ctx, const char* prefsname, CWMemHandle* prefsdata) {
    LOG("CWGetNamedPreferences(\"%s\", %p)", prefsname ? prefsname : "NULL", prefsdata);
    return CWSecretGetNamedPreferences(ctx, prefsname, (void**)prefsdata);
}

/* ============================================================
 * Compiler State Queries
 * ============================================================ */

CW_CALLBACK CWIsPrecompiling(CWPluginContext context, Boolean* isPrecompiling) {
    LOG("CWIsPrecompiling");
    if (!context || !isPrecompiling) return cwErrInvalidParameter;
    *isPrecompiling = FALSE; // TODO: stub
    return cwNoErr;
}

CW_CALLBACK CWIsAutoPrecompiling(CWPluginContext context, Boolean* isAutoPrecompiling) {
    LOG("CWIsAutoPrecompiling");
    if (!context || !isAutoPrecompiling) return cwErrInvalidParameter;
    *isAutoPrecompiling = FALSE; // TODO: stub
    return cwNoErr;
}

CW_CALLBACK CWIsPreprocessing(CWPluginContext context, Boolean* isPreprocessing) {
    LOG("CWIsPreprocessing");
    if (!context || !isPreprocessing) return cwErrInvalidParameter;
    *isPreprocessing = context->preprocess;
    return cwNoErr;
}

CW_CALLBACK CWIsGeneratingDebugInfo(CWPluginContext context, Boolean* isGenerating) {
    LOG("CWIsGeneratingDebugInfo");
    if (!context || !isGenerating) return cwErrInvalidParameter;
    *isGenerating = context->debugInfo;
    return cwNoErr;
}

CW_CALLBACK CWIsCachingPrecompiledHeaders(CWPluginContext context, Boolean* isCaching) {
    LOG("CWIsCachingPrecompiledHeaders");
    if (!context || !isCaching) return cwErrInvalidParameter;
    *isCaching = FALSE;
    return cwNoErr;
}

CW_CALLBACK CWGetBrowseOptions(CWPluginContext context, CWBrowseOptions* browseOptions) {
    LOG("CWGetBrowseOptions");
    if (!context || !browseOptions) return cwErrInvalidParameter;
    memset(browseOptions, 0, sizeof(*browseOptions)); // TODO: stub
    return cwNoErr;
}

CW_CALLBACK CWGetTargetInfo(CWPluginContext ctx, CWTargetInfo* targetInfo) {
    LOG("CWGetTargetInfo");
    if (!ctx || !targetInfo) return cwErrInvalidParameter;
    memset(targetInfo, 0, sizeof(CWTargetInfo));
    targetInfo->targetCPU = targetCPUMips; // TODO
    targetInfo->targetOS = targetOSAny;
    targetInfo->linkType = exelinkageFlat;
    targetInfo->outputType = linkOutputFile; // TODO
    return cwNoErr;
}

CW_CALLBACK CWGetBuildSequenceNumber(CWPluginContext ctx, SInt32* sequenceNumber) {
    LOG("CWGetBuildSequenceNumber");
    if (!ctx || !sequenceNumber) return cwErrInvalidParameter;
    *sequenceNumber = 0;
    return cwNoErr;
}

CW_CALLBACK CWSetTargetInfo(CWPluginContext ctx, CWTargetInfo* targetInfo) {
    LOG("CWSetTargetInfo");
    if (!ctx || !targetInfo) return cwErrInvalidParameter;
    return cwNoErr;
}

CW_CALLBACK CWGetTargetName(CWPluginContext ctx, char* name, short maxLength) {
    static const char* kTargetName = "command-line target";
    size_t n;

    LOG("CWGetTargetName");
    if (!ctx || !name || maxLength <= 0) return cwErrInvalidParameter;

    n = (size_t)maxLength;
    strncpy(name, kTargetName, n);
    name[n - 1] = '\0';
    return cwNoErr;
}

/* ============================================================
 * Precompiled Headers
 * ============================================================ */

CW_CALLBACK CWCachePrecompiledHeader(CWPluginContext context,
    const CWFileSpec* filespec, CWMemHandle pchhandle)
{
    STUB("CWCachePrecompiledHeader");
    return cwNoErr;
}

CW_CALLBACK CWGetPrecompiledHeaderSpec(CWPluginContext context,
    CWFileSpec* pchspec, const char* target)
{
    STUB("CWGetPrecompiledHeaderSpec");
    if (pchspec) memset(pchspec, 0, sizeof(CWFileSpec));
    return cwErrFileNotFound;
}

/* ============================================================
 * Secret/Internal
 * ============================================================ */

CW_CALLBACK CWSecretAttachHandle(CWPluginContext context,
    HandleStructure* handle, CWMemHandle* memHandle)
{
    LOG("CWSecretAttachHandle(handle=%p)", handle);
    if (!memHandle) return cwErrInvalidParameter;

    /*
     * Wrap a Handle (HandleStructure*) into a CWMemHandle.
     * The handle's first field (addr) is the cached data pointer.
     * Size is read from hand.used in the HandleStructure.
     */
    CWMemHandleImpl* h = (CWMemHandleImpl*)calloc(1, sizeof(CWMemHandleImpl));
    if (!h) return cwErrOutOfMemory;

    if (handle) {
        h->data = handle->addr;
        h->size = (SInt32)handle->hand.used;
        LOG("  CWSecretAttachHandle: data=%p, size=%d", h->data, h->size);
    }
    h->locked = 0;

    *memHandle = (CWMemHandle)h;
    return cwNoErr;
}

CW_CALLBACK CWSecretPeekHandle(CWPluginContext context, CWMemHandle memHandle, HandleStructure** handle) {
    CWMemHandleImpl* h = (CWMemHandleImpl*)memHandle;

    LOG("CWSecretPeekHandle(memHandle=%p)", memHandle);
    if (!context || !handle) return cwErrInvalidParameter;

    if (!h) {
        *handle = NULL;
        return cwNoErr;
    }

    *handle = (HandleStructure*)calloc(1, sizeof(HandleStructure));
    if (!*handle) return cwErrOutOfMemory;

    if (h->size > 0) {
        (*handle)->hand.addr = malloc((size_t)h->size);
        if (!(*handle)->hand.addr) {
            free(*handle);
            *handle = NULL;
            return cwErrOutOfMemory;
        }
        (*handle)->addr = (char*)(*handle)->hand.addr;
        (*handle)->hand.used = (UInt32)h->size;
        (*handle)->hand.size = (UInt32)h->size;
        if (h->data) memcpy((*handle)->addr, h->data, (size_t)h->size);
    }

    return cwNoErr;
}

/* ============================================================
 * Display
 * ============================================================ */

CW_CALLBACK CWDisplayLines(CWPluginContext ctx, SInt32 nlines) {
    LOG("CWDisplayLines(%d)", nlines);
    return cwNoErr;
}

/* ============================================================
 * Licensing (stub - bypass)
 * ============================================================ */

CW_CALLBACK CWCheckoutLicense(CWPluginContext context,
    const char* featureName, const char* licenseVersion,
    SInt32 flags, void* reserved, SInt32* cookie)
{
    LOG("CWCheckoutLicense(\"%s\", \"%s\")",
        featureName ? featureName : "NULL",
        licenseVersion ? licenseVersion : "NULL");
    if (cookie) *cookie = 1; /* fake cookie */
    return cwNoErr;
}

CW_CALLBACK CWCheckinLicense(CWPluginContext context, SInt32 cookie) {
    LOG("CWCheckinLicense(%d)", cookie);
    return cwNoErr;
}

/* ============================================================
 * COS Handle Management
 * ============================================================ */

static int cos_handle_count = 0;

void* __cdecl COS_NewHandle(SInt32 byteCount) {
    cos_handle_count++;
    LOG("COS_NewHandle(%d) [#%d]", byteCount, cos_handle_count);
    if (byteCount <= 0) byteCount = 1;

    HandleStructure* hs = (HandleStructure*)calloc(1, sizeof(HandleStructure));
    if (!hs) { LOG("  COS_NewHandle: handle alloc FAILED"); return NULL; }

    /* Allocate the data block (round up to 256-byte boundary) */
    UInt32 allocSize = (byteCount + 255) & ~255;
    hs->hand.addr = calloc(1, allocSize);
    if (!hs->hand.addr) { free(hs); LOG("  COS_NewHandle: data alloc FAILED for %d bytes", byteCount); return NULL; }

    hs->hand.used = byteCount;
    hs->hand.size = allocSize;
    hs->addr = (char*)hs->hand.addr;  /* Cache the data pointer at offset 0 */

    LOG("  COS_NewHandle: handle=%p, *handle(data)=%p, used=%u, size=%u",
        (void*)hs, (void*)hs->addr, hs->hand.used, hs->hand.size);
    return (void*)hs;
}

static int cos_oshandle_count = 0;

void* __cdecl COS_NewOSHandle(SInt32 logicalSize) {
    cos_oshandle_count++;
    LOG("COS_NewOSHandle(%d) [#%d] -> delegating to COS_NewHandle", logicalSize, cos_oshandle_count);
    return COS_NewHandle(logicalSize);
}

void __cdecl COS_FreeHandle(HandleStructure* handle) {
    LOG("COS_FreeHandle(%p)", handle);
    if (handle) {
        if (handle->hand.addr) free(handle->hand.addr);
        handle->addr = NULL;
        handle->hand.addr = NULL;
        handle->hand.used = 0;
        handle->hand.size = 0;
        free(handle);
    }
}

int __cdecl COS_ResizeHandle(HandleStructure* handle, SInt32 newSize) {
    LOG("COS_ResizeHandle(%p, %d)", handle, newSize);
    if (!handle) return 0;

    UInt32 allocSize = (newSize + 255) & ~255;
    void* newdata = realloc(handle->hand.addr, allocSize > 0 ? allocSize : 256);
    if (!newdata) return 0;

    handle->hand.addr = newdata;
    handle->hand.used = newSize;
    handle->hand.size = allocSize;
    handle->addr = (char*)newdata;  /* Update cached pointer */
    return 1;
}

void* __cdecl COS_LockHandle(HandleStructure* handle) {
    LOG("COS_LockHandle(%p)", handle);
    if (!handle) return NULL;
    handle->addr = (char*)handle->hand.addr;  /* Refresh cached pointer */
    LOG("  COS_LockHandle: *handle=%p", (void*)handle->addr);
    return handle->addr;
}

void* __cdecl COS_LockHandleHi(HandleStructure* handle) {
    LOG("COS_LockHandleHi(%p)", handle);
    if (!handle) return NULL;
    handle->addr = (char*)handle->hand.addr;  /* Refresh cached pointer */
    LOG("  COS_LockHandleHi: *handle=%p", (void*)handle->addr);
    return handle->addr;
}

void __cdecl COS_UnlockHandle(HandleStructure* handle) {
    /* no-op on flat memory system */
}

/* ============================================================
 * COS File I/O
 * ============================================================ */

typedef SInt32 OSErr;
typedef SInt32 OSType;
typedef unsigned char* StringPtr;
typedef const unsigned char* ConstStringPtr;

typedef struct COSOpenFile {
    FILE* fp;
    char  path[MAX_PATH];
} COSOpenFile;

#define COS_MAX_OPEN_FILES 256
static COSOpenFile g_open_files[COS_MAX_OPEN_FILES];

static SInt16 cos_alloc_refnum(FILE* fp, const char* path) {
    for (SInt16 i = 1; i < COS_MAX_OPEN_FILES; i++) {
        if (!g_open_files[i].fp) {
            g_open_files[i].fp = fp;
            if (path) {
                copy_cstr(g_open_files[i].path, MAX_PATH, path);
            } else {
                g_open_files[i].path[0] = '\0';
            }
            return i;
        }
    }
    return -1;
}

static FILE* cos_get_fp(SInt16 refNum) {
    if (refNum <= 0 || refNum >= COS_MAX_OPEN_FILES)
        return NULL;
    return g_open_files[refNum].fp;
}

static void cos_release_refnum(SInt16 refNum) {
    if (refNum <= 0 || refNum >= COS_MAX_OPEN_FILES)
        return;
    g_open_files[refNum].fp = NULL;
    g_open_files[refNum].path[0] = '\0';
}

static const char* cos_basename(const char* path) {
    const char* p;
    if (!path) return "";
    p = strrchr(path, '/');
    if (!p) p = strrchr(path, '\\');
    return p ? (p + 1) : path;
}

static void cos_c_to_pascal(const char* src, StringPtr dst, size_t dst_cap) {
    size_t len;
    if (!dst || dst_cap == 0) return;
    if (!src) src = "";

    len = strlen(src);
    if (len > 255) len = 255;
    if (len + 1 > dst_cap) len = dst_cap - 1;

    dst[0] = (unsigned char)len;
    if (len > 0)
        memcpy(dst + 1, src, len);
    if (len + 1 < dst_cap)
        dst[len + 1] = '\0';
}

static void cos_pascal_to_c(ConstStringPtr src, char* dst, size_t dst_cap) {
    size_t len;
    if (!src || !dst || dst_cap == 0) return;
    len = src[0];
    if (len + 1 > dst_cap) len = dst_cap - 1;
    if (len > 0)
        memcpy(dst, src + 1, len);
    dst[len] = '\0';
}

OSErr __cdecl COS_FileNew(const CWFileSpec* spec, SInt16* refNum, OSType creator, OSType fileType) {
    FILE* fp;
    SInt16 ref;
    char path[MAX_PATH];
    path[0] = '\0';
    (void)creator;
    (void)fileType;
    if (spec) {
        cwfilespec_to_cpath(spec, path, sizeof(path));
    }
    LOG("COS_FileNew(%s)", spec ? path : "NULL");

    if (!spec || !refNum || !path[0]) return -1;

    fp = fopen(path, "wb");
    if (!fp) return -1;
    fclose(fp);

    fp = fopen(path, "r+b");
    if (!fp) return -1;

    ref = cos_alloc_refnum(fp, path);
    if (ref < 0) {
        fclose(fp);
        return -1;
    }

    *refNum = ref;
    return 0;
}

OSErr __cdecl COS_FileOpen(const CWFileSpec* spec, SInt16* refNum) {
    FILE* fp;
    SInt16 ref;
    char path[MAX_PATH];
    path[0] = '\0';
    if (spec) {
        cwfilespec_to_cpath(spec, path, sizeof(path));
    }
    LOG("COS_FileOpen(%s)", spec ? path : "NULL");

    if (!spec || !refNum || !path[0]) return -1;

    fp = fopen(path, "rb");
    if (!fp) return -1;

    ref = cos_alloc_refnum(fp, path);
    if (ref < 0) {
        fclose(fp);
        return -1;
    }

    *refNum = ref;
    return 0;
}

OSErr __cdecl COS_FileGetType(const CWFileSpec* spec, OSType* fileType) {
    (void)spec;
    if (!fileType) return -1;
    *fileType = CWFOURCHAR('T', 'E', 'X', 'T');
    return 0;
}

OSErr __cdecl COS_FileGetSize(SInt16 refNum, SInt32* size) {
    FILE* fp = cos_get_fp(refNum);
    long pos;
    if (!fp || !size) return -1;
    pos = ftell(fp);
    if (pos < 0) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    *size = (SInt32)ftell(fp);
    if (fseek(fp, pos, SEEK_SET) != 0) return -1;
    return 0;
}

OSErr __cdecl COS_FileRead(SInt16 refNum, void* buf, SInt32 size) {
    FILE* fp = cos_get_fp(refNum);
    size_t r;
    if (!fp || !buf || size < 0) return -1;
    r = fread(buf, 1, (size_t)size, fp);
    return (r == (size_t)size) ? 0 : -1;
}

OSErr __cdecl COS_FileWrite(SInt16 refNum, const void* buf, SInt32 size) {
    FILE* fp = cos_get_fp(refNum);
    size_t w;
    if (!fp || !buf || size < 0) return -1;
    w = fwrite(buf, 1, (size_t)size, fp);
    return (w == (size_t)size) ? 0 : -1;
}

OSErr __cdecl COS_FileGetPos(SInt16 refNum, SInt32* pos) {
    FILE* fp = cos_get_fp(refNum);
    long v;
    if (!fp || !pos) return -1;
    v = ftell(fp);
    if (v < 0) return -1;
    *pos = (SInt32)v;
    return 0;
}

OSErr __cdecl COS_FileSetPos(SInt16 refNum, SInt32 pos) {
    FILE* fp = cos_get_fp(refNum);
    if (!fp) return -1;
    return (fseek(fp, pos, SEEK_SET) == 0) ? 0 : -1;
}

OSErr __cdecl COS_FileClose(SInt16 refNum) {
    FILE* fp = cos_get_fp(refNum);
    LOG("COS_FileClose(%d)", refNum);
    if (!fp) return -1;
    fclose(fp);
    cos_release_refnum(refNum);
    return 0;
}

void __cdecl COS_FileSetFSSpec(CWFileSpec* spec, ConstStringPtr path) {
    UInt8 len;
    char tmp[MAX_PATH];
    LOG("COS_FileSetFSSpec");
    if (!spec || !path) return;

    len = path[0];
    if (len > 0 && len < MAX_PATH && memchr(path + 1, '\0', len) == NULL) {
        cos_pascal_to_c(path, tmp, sizeof(tmp));
    } else {
        copy_cstr(tmp, sizeof(tmp), (const char*)path);
    }

    cwfilespec_from_cpath(spec, tmp);
}

void __cdecl COS_FileGetFSSpecInfo(const CWFileSpec* spec, SInt16* vRefNum, SInt32* dirID, StringPtr fileName) {
    char path[MAX_PATH];
    path[0] = '\0';
    if (spec) {
        cwfilespec_to_cpath(spec, path, sizeof(path));
    }
    if (vRefNum) *vRefNum = 0;
    if (dirID) *dirID = 0;
    if (fileName)
        cos_c_to_pascal(spec ? cos_basename(path) : "", fileName, 256);
}

void __cdecl COS_FileGetPathName(char* buffer, const CWFileSpec* spec, SInt32* mdDat) {
    struct stat st;
    char path[MAX_PATH];
    path[0] = '\0';
    if (!buffer) return;
    if (spec) {
        cwfilespec_to_cpath(spec, path, sizeof(path));
    }

    if (spec && path[0]) {
        copy_cstr(buffer, MAX_PATH, path);
    } else {
        buffer[0] = '\0';
    }

    if (mdDat) {
        if (spec && path[0] && stat(path, &st) == 0)
            *mdDat = (SInt32)st.st_mtime;
        else
            *mdDat = 0;
    }
}

/* ============================================================
 * COS Utility
 * ============================================================ */

UInt32 __cdecl COS_GetTicks(void) {
    return GetTickCount();
}

/* ============================================================
 * COS_GetString - String table from Mac resource fork
 *
 * The compiler DLL embeds a Mac resource fork as a Win32 custom
 * resource ("MACRSRC", ID 101). This contains STR# resources
 * with compiler error/warning message templates.
 *
 * The DLL's frontend code calls COS_GetString(buf, listID, idx)
 * to look up error messages by (strListID, 1-based index).
 * ============================================================ */

#define MAX_STR_LISTS       16
#define MAX_STRINGS_PER_LIST 512

typedef struct {
    SInt16 strListID;
    int    numStrings;
    char*  strings[MAX_STRINGS_PER_LIST];
} CachedStringList;

static CachedStringList cached_str_lists[MAX_STR_LISTS];
static int num_cached_str_lists = 0;
static int string_table_initialized = 0;

/* Big-endian read helpers for Mac resource fork parsing */
static UInt32 read_be32(const unsigned char* p) {
    return ((UInt32)p[0] << 24) | ((UInt32)p[1] << 16) | ((UInt32)p[2] << 8) | p[3];
}
static UInt16 read_be16(const unsigned char* p) {
    return ((UInt16)p[0] << 8) | p[1];
}
static UInt32 read_be24(const unsigned char* p) {
    return ((UInt32)p[0] << 16) | ((UInt32)p[1] << 8) | p[2];
}

/*
 * Decode a Pascal string from the Mac resource fork.
 * Handles Mac-Roman special characters: smart quotes -> ASCII quotes,
 * ellipsis (0xC9) -> "..."
 */
static char* decode_mac_pascal_string(const unsigned char* src, int len) {
    /* Worst case: each byte expands to 3 chars (ellipsis) */
    char* str = (char*)malloc(len * 3 + 1);
    if (!str) return NULL;

    int out = 0;
    for (int i = 0; i < len; i++) {
        unsigned char ch = src[i];
        switch (ch) {
        case 0xD4: str[out++] = '`';  break;  /* open single quote */
        case 0xD5: str[out++] = '\''; break;  /* close single quote */
        case 0xD2: str[out++] = '"';  break;  /* open double quote */
        case 0xD3: str[out++] = '"';  break;  /* close double quote */
        case 0xC9: str[out++] = '.'; str[out++] = '.'; str[out++] = '.'; break;
        default:   str[out++] = (char)ch; break;
        }
    }
    str[out] = '\0';
    return str;
}

/*
 * Parse a Mac STR# resource and cache the strings.
 * STR# format: 2-byte count (big-endian), then count Pascal strings
 * (1-byte length + string data).
 */
static void parse_str_list(const unsigned char* data, UInt32 dataLen, SInt16 resID) {
    if (num_cached_str_lists >= MAX_STR_LISTS) return;
    if (dataLen < 2) return;

    UInt16 numStrings = read_be16(data);
    if (numStrings > MAX_STRINGS_PER_LIST) numStrings = MAX_STRINGS_PER_LIST;

    CachedStringList* sl = &cached_str_lists[num_cached_str_lists++];
    sl->strListID = resID;
    sl->numStrings = numStrings;
    memset(sl->strings, 0, sizeof(sl->strings));

    UInt32 pos = 2;
    for (int i = 0; i < numStrings; i++) {
        if (pos >= dataLen) break;
        unsigned char slen = data[pos];
        if (pos + 1 + slen > dataLen) break;
        sl->strings[i] = decode_mac_pascal_string(data + pos + 1, slen);
        pos += 1 + slen;
    }

    LOG("  Loaded STR# %d: %d strings", resID, numStrings);
}

/*
 * Parse a Mac resource fork embedded in a Win32 MACRSRC resource.
 * Finds all STR# resources and caches their strings.
 */
static void parse_mac_resource_fork(const unsigned char* rsrc, DWORD rsrcSize) {
    if (rsrcSize < 16) return;

    UInt32 dataOffset = read_be32(rsrc);
    UInt32 mapOffset  = read_be32(rsrc + 4);

    if (mapOffset + 28 > rsrcSize) return;

    UInt16 typeListOffset = read_be16(rsrc + mapOffset + 24);
    UInt32 typeListStart  = mapOffset + typeListOffset;

    if (typeListStart + 2 > rsrcSize) return;
    UInt16 numTypes = read_be16(rsrc + typeListStart) + 1;

    for (int i = 0; i < numTypes; i++) {
        UInt32 typeOff = typeListStart + 2 + i * 8;
        if (typeOff + 8 > rsrcSize) break;

        /* Check for 'STR#' type code */
        if (rsrc[typeOff] != 'S' || rsrc[typeOff+1] != 'T' ||
            rsrc[typeOff+2] != 'R' || rsrc[typeOff+3] != '#')
            continue;

        UInt16 numResources = read_be16(rsrc + typeOff + 4) + 1;
        UInt16 refListOff   = read_be16(rsrc + typeOff + 6);
        UInt32 refListStart = typeListStart + refListOff;

        for (int j = 0; j < numResources; j++) {
            UInt32 refOff = refListStart + j * 12;
            if (refOff + 8 > rsrcSize) break;

            SInt16 resID = (SInt16)read_be16(rsrc + refOff);
            UInt32 resDataOff = read_be24(rsrc + refOff + 5);
            UInt32 absDataOff = dataOffset + resDataOff;

            if (absDataOff + 4 > rsrcSize) continue;
            UInt32 resLen = read_be32(rsrc + absDataOff);

            if (absDataOff + 4 + resLen > rsrcSize) continue;

            parse_str_list(rsrc + absDataOff + 4, resLen, resID);
        }
    }
}

/*
 * Initialize the string table by extracting the Mac resource fork
 * from the compiler DLL's MACRSRC Win32 resource.
 */
void __cdecl MWCC_InitStringTable(HMODULE hCompilerDll) {
    if (string_table_initialized) return;

    LOG("MWCC_InitStringTable: extracting strings from compiler DLL");

    HRSRC hRes = FindResourceA(hCompilerDll, "IDR_MACRSRC1", "MACRSRC");
    if (!hRes) {
        LOG("  MACRSRC resource not found in compiler DLL (err=%lu)", GetLastError());
        return;
    }

    DWORD resSize = SizeofResource(hCompilerDll, hRes);
    HGLOBAL hGlob = LoadResource(hCompilerDll, hRes);
    if (!hGlob) {
        LOG("  Failed to load MACRSRC resource");
        return;
    }

    const unsigned char* rsrcData = (const unsigned char*)LockResource(hGlob);
    if (!rsrcData) {
        LOG("  Failed to lock MACRSRC resource");
        return;
    }

    parse_mac_resource_fork(rsrcData, resSize);
    string_table_initialized = 1;

    LOG("MWCC_InitStringTable: loaded %d string lists", num_cached_str_lists);
}

/*
 * COS_GetString - look up a string by (strListID, 1-based index).
 *
 * The DLL calls this to get error/warning message templates.
 * Buffer must be at least 256 bytes (Str255 convention).
 */
void __cdecl COS_GetString(char* buffer, SInt16 strListID, SInt16 index) {
    LOG("COS_GetString(buf=%p, listID=%d, index=%d)", (void*)buffer, strListID, index);

    if (!buffer) return;
    buffer[0] = '\0';

    if (index < 1) return;

    for (int i = 0; i < num_cached_str_lists; i++) {
        if (cached_str_lists[i].strListID == strListID) {
            int idx = index - 1; /* Convert 1-based to 0-based */
            if (idx < cached_str_lists[i].numStrings && cached_str_lists[i].strings[idx]) {
                strncpy(buffer, cached_str_lists[i].strings[idx], 255);
                buffer[255] = '\0';
                LOG("  -> \"%s\"", buffer);
                return;
            }
            break;
        }
    }

    /* String not found - return a placeholder so it's obvious */
    snprintf(buffer, 256, "[string %d:%d not found]", strListID, index);
    LOG("  -> not found");
}

Boolean __cdecl COS_IsMultiByte(const char* str) {
    STUB("COS_IsMultiByte");
    return FALSE;
}

/* ============================================================
 * DLL entry point
 * ============================================================ */

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        typedef void (__cdecl *MWCC_RegisterPluginLibFunc)(HMODULE, int);
        HMODULE host_module = GetModuleHandleA(NULL);
        if (host_module) {
            MWCC_RegisterPluginLibFunc register_fn =
                (MWCC_RegisterPluginLibFunc)GetProcAddress(host_module, "MWCC_RegisterPluginLib");
            if (register_fn) {
                register_fn((HMODULE)hinstDLL, PLUGINLIB_VER);
            }
        }
    }
    return TRUE;
}
