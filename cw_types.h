/*
 * cw_types.h - CodeWarrior Plugin API types for Win32
 *
 * Reconstructed from the MWCC decomp SDK headers, adapted for Win32 target.
 * This is the minimal set needed by the mwccwrap host and PluginLib3 shim.
 */

#ifndef CW_TYPES_H
#define CW_TYPES_H

#include <windows.h>
#include <stdint.h>

#ifndef PLUGINLIB_VER
#define PLUGINLIB_VER 3
#endif

#define _STRINGIFY(s) #s
#define STRINGIFY(s) _STRINGIFY(s)

/* Basic types matching CW SDK */
typedef int32_t  SInt32;
typedef int16_t  SInt16;
typedef uint32_t UInt32;
typedef uint16_t UInt16;
typedef uint8_t  UInt8;
typedef unsigned char Boolean;

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

/* CWResult is the error/status result returned by all IDE API routines */
typedef SInt32 CWResult;

/* Four-character code */
typedef SInt32 CWFourCharType;
#define CWFOURCHAR(a, b, c, d)                            \
                 (((CWFourCharType) ((a) & 0xff) << 24)   \
                | ((CWFourCharType) ((b) & 0xff) << 16)   \
                | ((CWFourCharType) ((c) & 0xff) << 8)    \
                | ((CWFourCharType) ((d) & 0xff)))

typedef UInt32 CWDataType;

/* Mac-style FSSpec with Pascal name */
#pragma pack(push, 2)
typedef struct FSSpec {
    SInt16 vRefNum;
    SInt32 parID;
    UInt8  name[256];
} FSSpec;
#pragma pack(pop)

typedef struct OSHandle {
    void*  addr;
    UInt32 used;
    UInt32 size;
} OSHandle;

typedef struct HandleStructure {
    char*    addr; /* Mac Handle-compatible first field */
    OSHandle hand;
} HandleStructure;

#if PLUGINLIB_VER < 3
typedef FSSpec CWFileSpec;
#else
typedef struct CWFileSpec {
    char path[MAX_PATH];
} CWFileSpec;
#endif

typedef char CWFileName[65];
typedef FILETIME CWFileTime;
typedef DWORD CWOSResult;

/* Calling conventions */
#define CW_CALLBACK CWResult __stdcall

/* Memory handle - opaque pointer */
typedef struct CWMemHandlePrivateStruct* CWMemHandle;

/* Plugin context - pointer to private context struct */
typedef struct CWPluginPrivateContext* CWPluginContext;

/* Error codes */
enum {
    cwNoErr = 0,
    cwErrUserCanceled,
    cwErrRequestFailed,
    cwErrInvalidParameter,
    cwErrInvalidCallback,
    cwErrInvalidMPCallback,
    cwErrOSError,
    cwErrOutOfMemory,
    cwErrFileNotFound,
    cwErrUnknownFile,
    cwErrSilent,
    cwErrCantSetAttribute,
    cwErrStringBufferOverflow,
    cwErrDirectoryNotFound,
    cwErrLastCommonError = 512,

    cwErrUnknownSegment,
    cwErrSBMNotFound,
    cwErrObjectFileNotStored,
    cwErrLicenseCheckFailed,
    cwErrFileSpecNotSpecified,
    cwErrFileSpecInvalid,
    cwErrLastCompilerLinkerError = 1024
};

/* Request codes */
enum {
    reqInitialize  = -2,
    reqTerminate   = -1,
    reqIdle        = -100,
    reqAbout       = -101,
    reqPrefsChange = -102
};

/* Compiler request codes */
enum {
    reqCompile = 0,
    reqMakeParse,
    reqCompDisassemble,
    reqCheckSyntax,
    reqPreprocessForDebugger
};

/* Dependency types */
typedef enum CWDependencyType {
    cwNoDependency,
    cwNormalDependency,
    cwInterfaceDependency
} CWDependencyType;

/* File data types */
enum {
    cwFileTypeUnknown,
    cwFileTypeText,
    cwFileTypePrecompiledHeader
};

/* Message types */
enum {
    messagetypeInfo,
    messagetypeWarning,
    messagetypeError
};

/* Target CPU/OS constants */
enum {
	targetCPU68K			=	CWFOURCHAR('6','8','k',' '),
	targetCPUPowerPC		=	CWFOURCHAR('p','p','c',' '),
	targetCPUi80x86			=	CWFOURCHAR('8','0','8','6'),
	targetCPUMips			=	CWFOURCHAR('m','i','p','s'),
	targetCPUNECv800		=	CWFOURCHAR('v','8','0','0'),
	targetCPUEmbeddedPowerPC =	CWFOURCHAR('e','P','P','C'),
	targetCPUARM			=	CWFOURCHAR('a','r','m',' '),
	targetCPUSparc			=	CWFOURCHAR('s','p','r','c'),
	targetCPUIA64			=	CWFOURCHAR('I','A','6','4'),
	targetCPUAny			=	CWFOURCHAR('*','*','*','*'),
	targetCPUMCORE			=	CWFOURCHAR('m','c','o','r'),
	targetCPU_Intent        =   CWFOURCHAR('n','t','n','t')
};

enum {
	targetOSMacintosh		=	CWFOURCHAR('m','a','c',' '),
	targetOSWindows			=	CWFOURCHAR('w','i','n','t'),
	targetOSNetware			=	CWFOURCHAR('n','l','m',' '),
	targetOSMagicCap		=	CWFOURCHAR('m','c','a','p'),
	targetOSOS9				=	CWFOURCHAR('o','s','9',' '),
	targetOSEmbeddedABI		=	CWFOURCHAR('E','A','B','I'),
	targetOSJava			= 	CWFOURCHAR('j','a','v','a'),	/* java (no VM specification)	*/
	targetOSJavaMS			=	CWFOURCHAR('j','v','m','s'),	/* Microsoft VM					*/
	targetOSJavaSun			=	CWFOURCHAR('j','v','s','n'),	/* Sun VM						*/
	targetOSJavaMRJ			=	CWFOURCHAR('j','v','m','r'),	/* MRJ VM						*/
	targetOSJavaMW			=	CWFOURCHAR('j','v','m','w'),	/* Metrowerks VM				*/
	targetOSPalm			=	CWFOURCHAR('p','a','l','m'),
	targetOSGTD5			=   CWFOURCHAR('g','t','d','5'),
	targetOSSolaris			=	CWFOURCHAR('s','l','r','s'),
	targetOSLinux			=	CWFOURCHAR('l','n','u','x'),
	targetOSAny				=	CWFOURCHAR('*','*','*','*'),
	targetOS_Intent         =   CWFOURCHAR('n','t','n','t')
};

/* Linkage types */
enum {
    exelinkageFlat,
    exelinkageSegmented,
    exelinkageOverlay1
};

/* Output types */
enum {
    linkOutputNone,
    linkOutputFile,
    linkOutputDirectory
};

/*
 * CW SDK structs use mac68k alignment (Metrowerks) or 2-byte packing (MSVC).
 */
#pragma pack(push, 2)

/* Message reference */
typedef struct CWMessageRef {
    CWFileSpec sourcefile;
    SInt32     linenumber;
    short      tokenoffset;
    short      tokenlength;
    SInt32     selectionoffset;
    SInt32     selectionlength;
} CWMessageRef;

/* File info returned by CWFindAndLoadFile */
typedef struct CWFileInfo {
    Boolean     fullsearch;
    char        dependencyType;
    SInt32      isdependentoffile;
    Boolean     suppressload;
    Boolean     padding;
    const char* filedata;
    SInt32      filedatalength;
    short       filedatatype;
    short       fileID;
    CWFileSpec  filespec;
    Boolean     alreadyincluded;
    Boolean     recordbrowseinfo;
} CWFileInfo;

/* Browse options */
typedef struct CWBrowseOptions {
    Boolean recordClasses;
    Boolean recordEnums;
    Boolean recordMacros;
    Boolean recordTypedefs;
    Boolean recordConstants;
    Boolean recordTemplates;
    Boolean recordUndefinedFunctions;
    SInt32  reserved1;
    SInt32  reserved2;
} CWBrowseOptions;

/* Object data for StoreObjectData */
typedef struct CWDependencyInfo {
    SInt32     fileIndex;
    CWFileSpec fileSpec;
    short      fileSpecAccessType;
    short      dependencyType;
} CWDependencyInfo;

typedef struct CWObjectData {
    CWMemHandle       objectdata;
    CWMemHandle       browsedata;
    SInt32            reserved1;
    SInt32            codesize;
    SInt32            udatasize;
    SInt32            idatasize;
    SInt32            compiledlines;
    Boolean           interfaceChanged;
    SInt32            reserved2;
    void*             compilecontext;
    CWDependencyInfo* dependencies;
    short             dependencyCount;
    CWFileSpec*       objectfile;
} CWObjectData;

/* Legacy target info (API <= 9 era) */
typedef struct CWTargetInfoV7 {
    CWFileSpec outfile;
    CWFileSpec symfile;
    short      linkType;
    Boolean    canRun;
    Boolean    canDebug;
    Boolean    useRunHelperApp;
    char       reserved1;
    CWDataType debuggerCreator;
    CWDataType runHelperCreator;
    SInt32     reserved2[2];
} CWTargetInfoV7;

/* Target info - Win32 version */
typedef struct CWTargetInfo {
    short      outputType;
    CWFileSpec outfile;
    CWFileSpec symfile;
    CWFileSpec runfile;
    short      linkType;
    Boolean    canRun;
    Boolean    canDebug;
    CWDataType targetCPU;
    CWDataType targetOS;
    // Boolean    runHelperIsRegKey;
    // Boolean    debugHelperIsRegKey;
    // char       args[512];
    // char       runHelperName[512];
    // Boolean    runHelperRequiresURL;
    // char       reserved2;
    // char       debugHelperName[512];
    // CWFileSpec linkAgainstFile;
} CWTargetInfo;

/* Project file info */
typedef struct CWProjectFileInfo {
    CWFileSpec filespec;
    CWFileTime moddate;
    short      segment;
    Boolean    hasobjectcode;
    Boolean    hasresources;
    Boolean    isresourcefile;
    Boolean    weakimport;
    Boolean    initbefore;
    Boolean    gendebug;
    CWFileTime objmoddate;
    CWFileName dropinname;
    short      fileID;
    Boolean    recordbrowseinfo;
    Boolean    reserved;
    Boolean    hasunitdata;
    Boolean    mergeintooutput;
    UInt32     unitdatadependencytag;
} CWProjectFileInfo;

/* Segment info */
typedef struct CWProjectSegmentInfo {
    char  name[32];
    short attributes;
} CWProjectSegmentInfo;

/* New text document */
typedef struct CWNewTextDocumentInfo {
    const char* documentname;
    CWMemHandle text;
    Boolean     markDirty;
} CWNewTextDocumentInfo;

/* IDE info */
typedef struct CWIDEInfo {
    unsigned short majorVersion;
    unsigned short minorVersion;
    unsigned short bugFixVersion;
    unsigned short buildVersion;
    unsigned short dropinAPIVersion;
} CWIDEInfo;

/* Access path info */
typedef enum CWAccessPathType {
    cwSystemPath,
    cwUserPath
} CWAccessPathType;

typedef struct CWAccessPathInfo {
    CWFileSpec pathSpec;
    Boolean    recursive;
    SInt32     subdirectoryCount;
} CWAccessPathInfo;

typedef struct CWAccessPathListInfo {
    SInt32  systemPathCount;
    SInt32  userPathCount;
    Boolean alwaysSearchUserPaths;
    Boolean convertPaths;
} CWAccessPathListInfo;

/* Relative path */
typedef struct CWRelativePath {
    short         version;
    unsigned char pathType;
    unsigned char pathFormat;
    char          userDefinedTree[256];
    char          pathString[512];
} CWRelativePath;

/* New project entry */
typedef struct CWNewProjectEntryInfo {
    SInt32      position;
    SInt32      segment;
    SInt32      overlayGroup;
    SInt32      overlay;
    const char* groupPath;
    Boolean     mergeintooutput;
    Boolean     weakimport;
    Boolean     initbefore;
} CWNewProjectEntryInfo;

/* Overlay info */
typedef struct CWAddr64 {
    SInt32 lo;
    SInt32 hi;
} CWAddr64;

typedef struct CWOverlay1GroupInfo {
    char     name[256];
    CWAddr64 address;
    SInt32   numoverlays;
} CWOverlay1GroupInfo;

typedef struct CWOverlay1Info {
    char   name[256];
    SInt32 numfiles;
} CWOverlay1Info;

typedef struct CWOverlay1FileInfo {
    SInt32 whichfile;
} CWOverlay1FileInfo;

/* Framework info */
typedef struct CWFrameworkInfo {
    CWFileSpec fileSpec;
    char       version[256];
} CWFrameworkInfo;

/* Drop-in flags */
typedef struct DropInFlags {
    short          rsrcversion;
    CWDataType     dropintype;
    unsigned short earliestCompatibleAPIVersion;
    UInt32         dropinflags;
    CWDataType     edit_language;
    unsigned short newestAPIVersion;
} DropInFlags;

/* Panel list */
typedef struct CWPanelList {
    short        version;
    short        count;
    const char** names;
} CWPanelList;

/* Compiler mapping */
typedef unsigned long CompilerMappingFlags;

typedef struct CWExtensionMapping {
    CWDataType           type;
    char                 extension[32];
    CompilerMappingFlags flags;
} CWExtensionMapping;

typedef struct CWExtMapList {
    short               version;
    short               nMappings;
    CWExtensionMapping* mappings;
} CWExtMapList;

/* Target list */
typedef struct CWTargetList {
    short      version;
    short      cpuCount;
    CWDataType* cpus;
    short      osCount;
    CWDataType* oss;
} CWTargetList;

#pragma pack(pop)

#endif /* CW_TYPES_H */
