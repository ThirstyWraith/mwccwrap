/*
 * asintppc.c - Lightweight ASINTPPC.DLL shim
 */

#include <windows.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cw_types.h"

enum {
    kNoErr = 0,
    kMemFullErr = -108,
    kFnfErr = -43,
    kEofErr = -39,
    kParamErr = -50,
    kOpWrErr = -49
};

static SInt16 g_mem_error = kNoErr;
static FILE* g_open_files[256];
static SInt16 g_next_refnum = 16;
static int g_verbose = 0;

#define ASILOG(fmt, ...) do { if (g_verbose) { fprintf(stderr, "[ASINTPPC] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } } while(0)

static void pstr_to_cstr(const UInt8* pstr, char* out, size_t out_size) {
    size_t len;
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!pstr) return;
    len = pstr[0];
    if (len + 1 > out_size) len = out_size - 1;
    if (len > 0) memcpy(out, pstr + 1, len);
    out[len] = '\0';
}

static void cstr_to_pstr(const char* cstr, UInt8* out) {
    size_t len = 0;
    if (!out) return;
    if (cstr) len = strlen(cstr);
    if (len > 255) len = 255;
    out[0] = (UInt8)len;
    if (len > 0) memcpy(out + 1, cstr, len);
}

static SInt16 alloc_refnum(FILE* f) {
    for (int i = 0; i < 256; i++) {
        SInt16 ref = (SInt16)((g_next_refnum + i) & 0xFF);
        if (ref == 0) continue;
        if (!g_open_files[(unsigned char)ref]) {
            g_open_files[(unsigned char)ref] = f;
            g_next_refnum = (SInt16)(ref + 1);
            return ref;
        }
    }
    return 0;
}

static FILE* lookup_refnum(SInt16 refnum) {
    return g_open_files[(unsigned char)refnum];
}

static void close_refnum(SInt16 refnum) {
    g_open_files[(unsigned char)refnum] = NULL;
}

/*
 * EqualString - Mac Toolbox Pascal string comparison
 *
 * caseSensitive: if 0, compare case-insensitively
 * diacSensitive: ignored in this shim
 */
int __stdcall EqualString(const UInt8* str1, const UInt8* str2,
                          int caseSensitive, int diacSensitive)
{
    UInt8 len1, len2;
    ASILOG("EqualString");
    (void)diacSensitive;

    if (!str1 || !str2) return 0;
    len1 = str1[0];
    len2 = str2[0];
    if (len1 != len2) return 0;

    for (UInt8 i = 1; i <= len1; i++) {
        UInt8 c1 = str1[i];
        UInt8 c2 = str2[i];
        if (!caseSensitive) {
            c1 = (UInt8)tolower(c1);
            c2 = (UInt8)tolower(c2);
        }
        if (c1 != c2) return 0;
    }
    return 1;
}

void __stdcall ASI_CopyPtoC(const UInt8* pstr, char* cstr) {
    ASILOG("ASI_CopyPtoC");
    if (!cstr) return;
    if (!pstr) {
        cstr[0] = '\0';
        return;
    }
    pstr_to_cstr(pstr, cstr, 1024);
}

SInt16 __stdcall CharacterByteType(const char* text, SInt32 offset, SInt32 script) {
    ASILOG("CharacterByteType");
    (void)script;
    if (!text || offset < 0) return 0;
    /* Treat all bytes as single-byte characters in this shim. */
    return 1;
}

void __stdcall GetIndString(UInt8* out, SInt16 list_id, SInt16 index) {
    ASILOG("GetIndString(list=%d,index=%d)", (int)list_id, (int)index);
    (void)list_id;
    (void)index;
    if (!out) return;
    out[0] = 0;
}

HandleStructure* __stdcall NewHandle(SInt32 size) {
    HandleStructure* h;
    size_t alloc_size;
    ASILOG("NewHandle(size=%d)", (int)size);
    if (size < 0) size = 0;

    h = (HandleStructure*)calloc(1, sizeof(HandleStructure));
    if (!h) {
        g_mem_error = kMemFullErr;
        return NULL;
    }

    alloc_size = (size > 0) ? (size_t)size : 1u;
    h->hand.addr = calloc(1, alloc_size);
    if (!h->hand.addr) {
        free(h);
        g_mem_error = kMemFullErr;
        return NULL;
    }

    h->addr = (char*)h->hand.addr;
    h->hand.used = (UInt32)size;
    h->hand.size = (UInt32)alloc_size;
    g_mem_error = kNoErr;
    return h;
}

HandleStructure* __stdcall TempNewHandle(SInt32 size, SInt16* result) {
    ASILOG("TempNewHandle(size=%d)", (int)size);
    HandleStructure* h = NewHandle(size);
    if (result) *result = h ? kNoErr : g_mem_error;
    return h;
}

void __stdcall DisposeHandle(HandleStructure* h) {
    ASILOG("DisposeHandle");
    if (!h) return;
    free(h->hand.addr);
    free(h);
    g_mem_error = kNoErr;
}

SInt16 __stdcall SetHandleSize(HandleStructure* h, SInt32 new_size) {
    ASILOG("SetHandleSize(size=%d)", (int)new_size);
    void* new_data;
    size_t alloc_size;
    if (!h || new_size < 0) {
        g_mem_error = kParamErr;
        return g_mem_error;
    }

    alloc_size = (new_size > 0) ? (size_t)new_size : 1u;
    new_data = realloc(h->hand.addr, alloc_size);
    if (!new_data) {
        g_mem_error = kMemFullErr;
        return g_mem_error;
    }

    h->hand.addr = new_data;
    h->addr = (char*)new_data;
    h->hand.used = (UInt32)new_size;
    h->hand.size = (UInt32)alloc_size;
    g_mem_error = kNoErr;
    return kNoErr;
}

SInt16 __stdcall MemError(void) {
    ASILOG("MemError -> %d", (int)g_mem_error);
    return g_mem_error;
}

UInt32 __stdcall TickCount(void) {
    ASILOG("TickCount");
    return (UInt32)GetTickCount();
}

SInt16 __stdcall HDelete(SInt16 vRefNum, SInt32 dirID, const UInt8* name) {
    char path[MAX_PATH];
    ASILOG("HDelete");
    (void)vRefNum;
    (void)dirID;
    pstr_to_cstr(name, path, sizeof(path));
    if (!path[0]) return kParamErr;
    return (remove(path) == 0) ? kNoErr : kFnfErr;
}

SInt16 __stdcall HCreate(SInt16 vRefNum, SInt32 dirID, const UInt8* name, UInt32 creator, UInt32 fileType) {
    FILE* f;
    char path[MAX_PATH];
    ASILOG("HCreate");
    (void)vRefNum;
    (void)dirID;
    (void)creator;
    (void)fileType;
    pstr_to_cstr(name, path, sizeof(path));
    if (!path[0]) return kParamErr;
    f = fopen(path, "wb");
    if (!f) return kOpWrErr;
    fclose(f);
    return kNoErr;
}

SInt16 __stdcall HOpen(SInt16 vRefNum, SInt32 dirID, const UInt8* name, SInt16 permission, SInt16* refNum) {
    FILE* f = NULL;
    SInt16 ref;
    char path[MAX_PATH];
    ASILOG("HOpen");
    (void)vRefNum;
    (void)dirID;
    pstr_to_cstr(name, path, sizeof(path));
    if (!path[0] || !refNum) return kParamErr;

    if (permission == 1) f = fopen(path, "rb");
    if (!f) f = fopen(path, "r+b");
    if (!f) f = fopen(path, "rb");
    if (!f) return kFnfErr;

    ref = alloc_refnum(f);
    if (ref == 0) {
        fclose(f);
        return kMemFullErr;
    }
    *refNum = ref;
    return kNoErr;
}

SInt16 __stdcall HGetFInfo(SInt16 vRefNum, SInt32 dirID, const UInt8* name, void* fileInfo) {
    char path[MAX_PATH];
    FILE* f;
    ASILOG("HGetFInfo");
    (void)vRefNum;
    (void)dirID;
    pstr_to_cstr(name, path, sizeof(path));
    if (!path[0]) return kParamErr;
    f = fopen(path, "rb");
    if (!f) return kFnfErr;
    fclose(f);
    if (fileInfo) memset(fileInfo, 0, 64);
    return kNoErr;
}

SInt16 __stdcall GetEOF(SInt16 refNum, SInt32* eofPos) {
    ASILOG("GetEOF(ref=%d)", (int)refNum);
    long pos;
    long end;
    FILE* f = lookup_refnum(refNum);
    if (!f || !eofPos) return kParamErr;
    pos = ftell(f);
    if (fseek(f, 0, SEEK_END) != 0) return kParamErr;
    end = ftell(f);
    fseek(f, pos, SEEK_SET);
    *eofPos = (SInt32)end;
    return kNoErr;
}

SInt16 __stdcall GetFPos(SInt16 refNum, SInt32* position) {
    ASILOG("GetFPos(ref=%d)", (int)refNum);
    long pos;
    FILE* f = lookup_refnum(refNum);
    if (!f || !position) return kParamErr;
    pos = ftell(f);
    *position = (SInt32)pos;
    return kNoErr;
}

SInt16 __stdcall SetFPos(SInt16 refNum, SInt16 posMode, SInt32 offset) {
    ASILOG("SetFPos(ref=%d,mode=%d,off=%d)", (int)refNum, (int)posMode, (int)offset);
    int whence = SEEK_SET;
    FILE* f = lookup_refnum(refNum);
    if (!f) return kParamErr;
    if (posMode == 2) whence = SEEK_CUR;
    else if (posMode == 3) whence = SEEK_END;
    if (fseek(f, (long)offset, whence) != 0) return kParamErr;
    return kNoErr;
}

SInt16 __stdcall FSRead(SInt16 refNum, SInt32* count, void* buffer) {
    ASILOG("FSRead(ref=%d,count=%d)", (int)refNum, (int)(count ? *count : -1));
    size_t got;
    FILE* f = lookup_refnum(refNum);
    if (!f || !count || !buffer || *count < 0) return kParamErr;
    got = fread(buffer, 1, (size_t)*count, f);
    *count = (SInt32)got;
    if (got == 0 && feof(f)) return kEofErr;
    return kNoErr;
}

SInt16 __stdcall FSWrite(SInt16 refNum, SInt32* count, const void* buffer) {
    ASILOG("FSWrite(ref=%d,count=%d)", (int)refNum, (int)(count ? *count : -1));
    size_t put;
    FILE* f = lookup_refnum(refNum);
    if (!f || !count || !buffer || *count < 0) return kParamErr;
    put = fwrite(buffer, 1, (size_t)*count, f);
    *count = (SInt32)put;
    return (put > 0 || *count == 0) ? kNoErr : kOpWrErr;
}

SInt16 __stdcall FSClose(SInt16 refNum) {
    ASILOG("FSClose(ref=%d)", (int)refNum);
    FILE* f = lookup_refnum(refNum);
    if (!f) return kParamErr;
    fclose(f);
    close_refnum(refNum);
    return kNoErr;
}

SInt16 __stdcall FSMakeFSSpec(SInt16 vRefNum, SInt32 dirID, const UInt8* name, FSSpec* outSpec) {
    ASILOG("FSMakeFSSpec");
    if (!outSpec) return kParamErr;
    memset(outSpec, 0, 0x120);
    outSpec->vRefNum = vRefNum;
    outSpec->parID = dirID;
    if (name) {
        size_t n = name[0];
        if (n > 255) n = 255;
        outSpec->name[0] = (UInt8)n;
        if (n > 0) memcpy(outSpec->name + 1, name + 1, n);
    }
    return kNoErr;
}

SInt16 __stdcall PBGetCatInfoSync(void* pb) {
    ASILOG("PBGetCatInfoSync");
    if (pb) memset(pb, 0, 128);
    return kNoErr;
}

SInt16 __stdcall GetVInfo(SInt16 drvNum, UInt8* volumeName, SInt16* vRefNum, SInt32* freeBytes) {
    ASILOG("GetVInfo");
    (void)drvNum;
    if (volumeName) cstr_to_pstr("", volumeName);
    if (vRefNum) *vRefNum = 0;
    if (freeBytes) *freeBytes = 0;
    return kNoErr;
}

SInt16 __stdcall PBHGetFInfoSync(void* pb) {
    ASILOG("PBHGetFInfoSync");
    if (pb) memset(pb, 0, 128);
    return kNoErr;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    (void)hinstDLL;
    (void)fdwReason;
    (void)lpReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_verbose = (getenv("MWCC_ASI_VERBOSE") != NULL);
    }
    return TRUE;
}
