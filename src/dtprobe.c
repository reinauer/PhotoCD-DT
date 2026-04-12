#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <libraries/iffparse.h>

#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>

#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/iffparse.h>

struct Library *DataTypesBase = NULL;
struct Library *IFFParseBase = NULL;

static void id_to_string(ULONG id, char out[5])
{
    int shift;
    int index;

    for (shift = 24, index = 0; shift >= 0; shift -= 8, index++) {
        UBYTE ch = (UBYTE)((id >> shift) & 0xff);
        out[index] = isprint((int)ch) ? (char)ch : '.';
    }

    out[4] = '\0';
}

static const char *dt_error_string(LONG error)
{
    if (DataTypesBase != NULL &&
        error >= DTERROR_UNKNOWN_DATATYPE &&
        error <= DTERROR_NOT_AVAILABLE) {
        return GetDTString((ULONG)error);
    }

    return NULL;
}

static const char *safe_string(CONST_STRPTR value)
{
    if (value == NULL) {
        return "<none>";
    }

    return (const char *)value;
}

static void print_error_context(const char *where, LONG error, const char *detail)
{
    const char *dt_error;
    char fault[256];
    char formatted[256];

    dt_error = dt_error_string(error);
    if (dt_error != NULL) {
        if (detail != NULL && strstr(dt_error, "%s") != NULL) {
            snprintf(formatted, sizeof(formatted), dt_error, detail);
            printf("%s failed: %ld (%s)\n", where, error, formatted);
        } else {
            printf("%s failed: %ld (%s)\n", where, error, dt_error);
        }
        return;
    }

    if (Fault(error, NULL, fault, sizeof(fault)) != 0) {
        printf("%s failed: %ld (%s)\n", where, error, fault);
        return;
    }

    printf("%s failed: %ld\n", where, error);
}

static void print_error(const char *where, LONG error)
{
    print_error_context(where, error, NULL);
}

static void print_probe_at(BPTR fh, LONG offset, const char *label)
{
    char buffer[8];
    LONG got;
    int i;

    if (Seek(fh, offset, OFFSET_BEGINNING) == -1) {
        print_error(label, IoErr());
        return;
    }

    got = Read(fh, buffer, sizeof(buffer));
    if (got < 0) {
        print_error(label, IoErr());
        return;
    }

    printf("%s @ %ld:", label, offset);
    for (i = 0; i < got; i++) {
        printf(" %02x", (unsigned char)buffer[i]);
    }
    printf("  |");
    for (i = 0; i < got; i++) {
        unsigned char ch = (unsigned char)buffer[i];
        putchar(isprint((int)ch) ? (int)ch : '.');
    }
    printf("|\n");
}

static void probe_file_headers(const char *path)
{
    BPTR fh;

    fh = Open(path, MODE_OLDFILE);
    if (fh == 0) {
        print_error("Open", IoErr());
        return;
    }

    print_probe_at(fh, 0, "Header");
    print_probe_at(fh, 2048, "Header");

    Close(fh);
}

static void describe_datatype(struct DataType *dt)
{
    struct DataTypeHeader *dth;
    char id_string[5];
    UWORD type;
    const char *type_name;
    const char *group_name;

    dth = dt->dtn_Header;
    type = dth->dth_Flags & DTF_TYPE_MASK;
    type_name = GetDTString((ULONG)type + DTMSG_TYPE_OFFSET);
    group_name = GetDTString(dth->dth_GroupID);
    id_to_string(dth->dth_ID, id_string);

    printf("ObtainDataTypeA: success\n");
    printf("  Description: %s\n", safe_string(dth->dth_Name));
    printf("  Base name:   %s\n", safe_string(dth->dth_BaseName));
    printf("  Pattern:     %s\n", safe_string(dth->dth_Pattern));
    printf("  Group:       %s (%08lx)\n",
        group_name != NULL ? group_name : "<unknown>",
        (unsigned long)dth->dth_GroupID);
    printf("  ID:          %s (%08lx)\n", id_string, (unsigned long)dth->dth_ID);
    printf("  Type:        %s (%u)\n", type_name != NULL ? type_name : "<unknown>", (unsigned int)type);
    printf("  Flags:       0x%04x\n", (unsigned int)dth->dth_Flags);
    printf("  Priority:    %u\n", (unsigned int)dth->dth_Priority);
    printf("  Mask length: %d\n", (int)dth->dth_MaskLen);
    printf("  Compare fn:  %s\n", safe_string(dt->dtn_FunctionName));

    if (dt->dtn_FunctionName == NULL && dth->dth_MaskLen == 0 && dth->dth_Pattern != NULL) {
        printf("  Note:        descriptor relies on filename/pattern matching.\n");
    }
}

static void probe_class_library(CONST_STRPTR base_name)
{
    char libpath[128];
    char filepath[128];
    BPTR lock;
    BPTR seglist;
    struct Library *base;

    if (base_name == NULL) {
        return;
    }

    snprintf(libpath, sizeof(libpath), "datatypes/%s.datatype", (const char *)base_name);
    snprintf(filepath, sizeof(filepath), "SYS:Classes/DataTypes/%s.datatype", (const char *)base_name);
    printf("Class path:    %s\n", libpath);
    printf("Class file:    %s\n", filepath);

    lock = Lock(filepath, ACCESS_READ);
    if (lock == 0) {
        print_error("Lock class file", IoErr());
    } else {
        printf("Lock class file: success\n");
        UnLock(lock);
    }

    seglist = LoadSeg(filepath);
    if (seglist == 0) {
        print_error("LoadSeg class file", IoErr());
    } else {
        printf("LoadSeg class file: success\n");
        UnLoadSeg(seglist);
    }

    base = OpenLibrary(libpath, 0);
    if (base == NULL) {
        LONG error = IoErr();

        if (error != 0) {
            print_error("OpenLibrary class", error);
        } else {
            printf("OpenLibrary class failed: could not initialize %s\n", libpath);
        }
        return;
    }

    printf("OpenLibrary class: success (%u.%u)\n",
        (unsigned int)base->lib_Version, (unsigned int)base->lib_Revision);
    CloseLibrary(base);
}

static void probe_picture_superclass(void)
{
    struct Library *base;

    base = OpenLibrary("datatypes/picture.datatype", 44);
    if (base == NULL) {
        LONG error = IoErr();

        if (error != 0) {
            print_error("OpenLibrary picture.datatype", error);
        } else {
            printf("OpenLibrary picture.datatype failed\n");
        }
        return;
    }

    printf("OpenLibrary picture.datatype: success (%u.%u)\n",
        (unsigned int)base->lib_Version, (unsigned int)base->lib_Revision);
    CloseLibrary(base);
}

static void probe_obtain_datatype(const char *path)
{
    BPTR lock;
    struct DataType *dt;
    LONG error;

    lock = Lock(path, ACCESS_READ);
    if (lock == 0) {
        print_error("Lock", IoErr());
        return;
    }

    dt = ObtainDataTypeA(DTST_FILE, (APTR)lock, NULL);
    error = IoErr();

    if (dt == NULL) {
        print_error("ObtainDataTypeA", error);
        UnLock(lock);
        return;
    }

    describe_datatype(dt);
    probe_class_library(dt->dtn_Header->dth_BaseName);
    ReleaseDataType(dt);
    UnLock(lock);
}

static void print_object_datatype(Object *obj)
{
    struct DataType *dt;
    ULONG group_id;
    char id_string[5];

    dt = NULL;
    group_id = 0;

    GetDTAttrs(obj,
        DTA_DataType, (ULONG)&dt,
        DTA_GroupID, (ULONG)&group_id,
        TAG_DONE);

    if (dt != NULL && dt->dtn_Header != NULL) {
        id_to_string(dt->dtn_Header->dth_ID, id_string);
        printf("    Object datatype: %s / %s / %s\n",
            safe_string(dt->dtn_Header->dth_Name),
            safe_string(dt->dtn_Header->dth_BaseName),
            id_string);
    }

    if (group_id != 0) {
        const char *group_name = GetDTString(group_id);
        printf("    Object group:    %s (%08lx)\n",
            group_name != NULL ? group_name : "<unknown>",
            (unsigned long)group_id);
    }
}

static void probe_newdtobject_generic(const char *path)
{
    Object *obj;
    LONG error;

    obj = NewDTObject(path,
        DTA_SourceType, DTST_FILE,
        TAG_DONE);
    error = IoErr();

    if (obj == NULL) {
        print_error_context("NewDTObject", error, path);
        return;
    }

    printf("NewDTObject: success\n");
    print_object_datatype(obj);
    DisposeDTObject(obj);
}

static void probe_newdtobject_picture(const char *path)
{
    Object *obj;
    LONG error;

    obj = NewDTObject(path,
        DTA_SourceType, DTST_FILE,
        DTA_GroupID, GID_PICTURE,
        PDTA_Remap, FALSE,
        TAG_DONE);
    error = IoErr();

    if (obj == NULL) {
        print_error_context("NewDTObject picture", error, path);
        return;
    }

    printf("NewDTObject picture: success\n");
    print_object_datatype(obj);
    DisposeDTObject(obj);
}

static void usage(const char *argv0)
{
    printf("Usage: %s <file>\n", argv0);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    DataTypesBase = OpenLibrary("datatypes.library", 39);
    if (DataTypesBase == NULL) {
        print_error("OpenLibrary datatypes.library", IoErr());
        return 1;
    }

    IFFParseBase = OpenLibrary("iffparse.library", 39);
    if (IFFParseBase == NULL) {
        print_error("OpenLibrary iffparse.library", IoErr());
        CloseLibrary(DataTypesBase);
        return 1;
    }

    printf("File: %s\n", argv[1]);
    probe_file_headers(argv[1]);
    probe_picture_superclass();
    probe_obtain_datatype(argv[1]);
    probe_newdtobject_generic(argv[1]);
    probe_newdtobject_picture(argv[1]);

    CloseLibrary(IFFParseBase);
    CloseLibrary(DataTypesBase);
    return 0;
}
