#include <stddef.h>

#include <exec/initializers.h>
#include <exec/libraries.h>
#include <exec/nodes.h>
#include <exec/resident.h>
#include <exec/semaphores.h>
#include <intuition/classes.h>

#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#ifndef PCD_VERSION_MAJOR
#define PCD_VERSION_MAJOR 44
#endif

#ifndef PCD_VERSION_REVISION
#define PCD_VERSION_REVISION 11
#endif

#ifndef PCD_VERSION_DATE
#error "PCD_VERSION_DATE must be supplied by version.mk"
#endif

#define PCD_STR2(value) #value
#define PCD_STR(value) PCD_STR2(value)

const char LibName[] = "photocd.datatype";
const char LibIdString[] = "$VER: photocd.datatype " PCD_STR(PCD_VERSION_MAJOR) "." PCD_STR(PCD_VERSION_REVISION) " (" PCD_VERSION_DATE ")";
const UWORD LibVersion = PCD_VERSION_MAJOR;
const UWORD LibRevision = PCD_VERSION_REVISION;

struct PCDLibraryBase {
    struct Library libNode;
    BPTR segList;
    struct SignalSemaphore classLock;
    UBYTE mallocReady;
};

struct ExecBase *SysBase;
struct Library *DataTypesBase;
struct DosLibrary *DOSBase;
struct IntuitionBase *IntuitionBase;
struct Library *SuperClassBase;
struct Library *UtilityBase;

static struct PCDLibraryBase *PCDBase;
static Class *PCDClass;

extern __ASM__ ULONG Dispatcher(__REG__(a0, Class *cl),
    __REG__(a2, Object *obj), __REG__(a1, Msg msg));
extern void __initmalloc(void);
extern void __exitmalloc(void);

static void zero_memory(APTR ptr, ULONG size)
{
    UBYTE *p = (UBYTE *)ptr;

    while (size-- > 0) {
        *p++ = 0;
    }
}

static void TraceInit(const char *stage)
{
    (void)stage;
}

static void CloseBases(void)
{
    if (SuperClassBase != NULL) {
        CloseLibrary(SuperClassBase);
        SuperClassBase = NULL;
    }

    if (DataTypesBase != NULL) {
        CloseLibrary(DataTypesBase);
        DataTypesBase = NULL;
    }

    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }

    if (UtilityBase != NULL) {
        CloseLibrary((struct Library *)UtilityBase);
        UtilityBase = NULL;
    }

    if (DOSBase != NULL) {
        CloseLibrary((struct Library *)DOSBase);
        DOSBase = NULL;
    }
}

static void CleanupRuntime(struct PCDLibraryBase *base)
{
    if (base != NULL && base->mallocReady != 0) {
        __exitmalloc();
        base->mallocReady = 0;
    }
}

static Class *InitClass(void)
{
    Class *cl;

    cl = MakeClass((ClassID)LibName, (ClassID)"picture.datatype", NULL, 0, 0);
    if (cl != NULL) {
        cl->cl_Dispatcher.h_Entry = (HOOKFUNC)Dispatcher;
        cl->cl_UserData = (ULONG)PCDBase;
        AddClass(cl);
    }

    return cl;
}

static void FreeLibBase(struct PCDLibraryBase *base)
{
    if (base == NULL) {
        return;
    }

    FreeMem((APTR)((ULONG)base - (ULONG)base->libNode.lib_NegSize),
        (ULONG)base->libNode.lib_NegSize + (ULONG)base->libNode.lib_PosSize);
}

__ASM__ ULONG LibNull(void)
{
    return 0;
}

__ASM__ struct Library *LibInit(
    __REG__(d0, struct PCDLibraryBase *base),
    __REG__(a0, BPTR segList),
    __REG__(a6, struct ExecBase *execBase))
{
    if (base == NULL) {
        return NULL;
    }

    zero_memory((UBYTE *)base + sizeof(struct Library),
        sizeof(*base) - sizeof(struct Library));

    PCDBase = base;
    SysBase = execBase;
    base->segList = segList;
    base->libNode.lib_IdString = (STRPTR)LibIdString;
    InitSemaphore(&base->classLock);

    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 39);
    if (DOSBase == NULL) {
        PCDBase = NULL;
        FreeLibBase(base);
        return NULL;
    }
    TraceInit("dos ok");

    __initmalloc();
    base->mallocReady = 1;
    TraceInit("malloc ok");

    UtilityBase = OpenLibrary("utility.library", 39);
    if (UtilityBase == NULL) {
        TraceInit("utility open failed");
        CloseBases();
        CleanupRuntime(base);
        PCDBase = NULL;
        FreeLibBase(base);
        return NULL;
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (IntuitionBase == NULL) {
        TraceInit("intuition open failed");
        CloseBases();
        CleanupRuntime(base);
        PCDBase = NULL;
        FreeLibBase(base);
        return NULL;
    }

    DataTypesBase = OpenLibrary("datatypes.library", 40);
    if (DataTypesBase == NULL) {
        TraceInit("datatypes open failed");
        CloseBases();
        CleanupRuntime(base);
        PCDBase = NULL;
        FreeLibBase(base);
        return NULL;
    }
    TraceInit("core libs ok");

    SuperClassBase = OpenLibrary("datatypes/picture.datatype", 44);
    if (SuperClassBase == NULL) {
        TraceInit("picture datatype open failed");
        CloseBases();
        CleanupRuntime(base);
        PCDBase = NULL;
        FreeLibBase(base);
        return NULL;
    }
    TraceInit("picture datatype ok");

    ObtainSemaphore(&base->classLock);
    TraceInit("makeclass start");
    PCDClass = InitClass();
    ReleaseSemaphore(&base->classLock);

    if (PCDClass == NULL) {
        TraceInit("makeclass failed");
        CloseBases();
        CleanupRuntime(base);
        PCDBase = NULL;
        FreeLibBase(base);
        return NULL;
    }

    TraceInit("init ok");
    return (struct Library *)base;
}

__ASM__ struct Library *LibOpen(
    __REG__(a6, struct PCDLibraryBase *base))
{
    ObtainSemaphore(&base->classLock);
    base->libNode.lib_Flags &= ~LIBF_DELEXP;
    base->libNode.lib_OpenCnt++;
    ReleaseSemaphore(&base->classLock);

    return (struct Library *)base;
}

__ASM__ BPTR LibExpunge(
    __REG__(a6, struct PCDLibraryBase *base))
{
    BPTR segList;

    if (base->libNode.lib_OpenCnt != 0) {
        base->libNode.lib_Flags |= LIBF_DELEXP;
        return 0;
    }

    Forbid();
    Remove((struct Node *)base);
    Permit();

    segList = base->segList;

    if (PCDClass != NULL) {
        FreeClass(PCDClass);
        PCDClass = NULL;
    }

    CloseBases();
    CleanupRuntime(base);
    PCDBase = NULL;
    FreeLibBase(base);
    return segList;
}

__ASM__ BPTR LibClose(
    __REG__(a6, struct PCDLibraryBase *base))
{
    ObtainSemaphore(&base->classLock);

    if (base->libNode.lib_OpenCnt > 0) {
        base->libNode.lib_OpenCnt--;
    }

    ReleaseSemaphore(&base->classLock);

    if ((base->libNode.lib_Flags & LIBF_DELEXP) != 0 &&
        base->libNode.lib_OpenCnt == 0) {
        return LibExpunge(base);
    }

    return 0;
}

__ASM__ Class *ObtainClass(
    __REG__(a6, struct PCDLibraryBase *base))
{
    (void)base;
    return PCDClass;
}

const APTR LibFunctions[] = {
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)LibNull,
    (APTR)ObtainClass,
    (APTR)-1
};

#define WORDINIT(name) UWORD name##W1; UWORD name##W2; UWORD name##ARG;
#define LONGINIT(name) UBYTE name##A1; UBYTE name##A2; ULONG name##ARG;

static struct LibInitData {
    WORDINIT(w1)
    LONGINIT(l1)
    WORDINIT(w2)
    WORDINIT(w3)
    WORDINIT(w4)
    LONGINIT(l2)
    ULONG end_initlist;
} LibInitializers = {
    INITBYTE(offsetof(struct Node, ln_Type), NT_LIBRARY),
    0x80, (UBYTE)offsetof(struct Node, ln_Name), (ULONG)&LibName[0],
    INITBYTE(offsetof(struct Library, lib_Flags), LIBF_SUMUSED | LIBF_CHANGED),
    INITWORD(offsetof(struct Library, lib_Version), PCD_VERSION_MAJOR),
    INITWORD(offsetof(struct Library, lib_Revision), PCD_VERSION_REVISION),
    0x80, (UBYTE)offsetof(struct Library, lib_IdString), (ULONG)&LibIdString[0],
    0
};

const APTR LibInitTab[] = {
    (APTR)sizeof(struct PCDLibraryBase),
    (APTR)&LibFunctions,
    (APTR)&LibInitializers,
    (APTR)LibInit
};

const struct Resident RomTag __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&RomTag,
    (struct Resident *)(&RomTag + 1),
    RTF_AUTOINIT,
    PCD_VERSION_MAJOR,
    NT_LIBRARY,
    0,
    (char *)LibName,
    (char *)LibIdString,
    (APTR)LibInitTab
};
