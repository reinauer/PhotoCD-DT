#include <clib/alib_protos.h>
#include <datatypes/datatypes.h>
#include <datatypes/pictureclass.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <intuition/classes.h>
#include <intuition/icclass.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include "decoder/photocd.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

extern struct DosLibrary *DOSBase;

static LONG ReadPrefs(void)
{
    LONG ret = 2;
    BPTR fh;

    fh = Open("ENV:DataTypes/photocd.prefs", MODE_OLDFILE);
    if (fh != 0) {
        char text[20];

        if (FGets(fh, text, sizeof(text)) != NULL) {
            ret = atol(text);
            if (ret < 0) {
                ret = 0;
            } else if (ret > 5) {
                ret = 5;
            }
        }

        Close(fh);
    }

    return ret;
}

static ULONG WriteRGBRows_V43(Object *obj, struct BitMapHeader *bh,
    photocdDecode *decoder, UBYTE *rowbuf)
{
    ULONG error = 0;
    ULONG y;

    for (y = 0; y < bh->bmh_Height; y++) {
        if (!photocdDecode_populateUInt8Row(decoder, rowbuf, y, 0, bh->bmh_Width)) {
            return ERROR_NO_FREE_STORE;
        }
        DoMethod(obj, PDTM_WRITEPIXELARRAY, rowbuf, PBPAFMT_RGB, bh->bmh_Width * 3,
            0, y, bh->bmh_Width, 1);
    }
    return error;
}

static const char *FindLastSeparator(const char *path)
{
    const char *slash_pos;
    const char *colon_pos;

    slash_pos = strrchr(path, '/');
    colon_pos = strrchr(path, ':');
    if (slash_pos == NULL) {
        return colon_pos;
    }
    if (colon_pos == NULL) {
        return slash_pos;
    }

    return slash_pos > colon_pos ? slash_pos : colon_pos;
}

static BOOL LooksLowerCase(const char *text)
{
    BOOL saw_alpha = FALSE;

    while (*text != '\0') {
        UBYTE c = (UBYTE)*text++;

        if (isalpha(c)) {
            saw_alpha = TRUE;
            if (!islower(c)) {
                return FALSE;
            }
        }
    }

    return saw_alpha;
}

static char *DupPrefix(const char *text, size_t len)
{
    char *copy;

    copy = (char *)malloc(len + 1);
    if (copy != NULL) {
        memcpy(copy, text, len);
        copy[len] = '\0';
    }

    return copy;
}

static char *DeriveIpePath(const char *infile)
{
    const char *sep_pos;
    const char *dot_pos;
    const char *basename;
    char *without_ext = NULL;
    char *containing_dir = NULL;
    char *root_dir = NULL;
    char *path = NULL;
    size_t without_ext_len;
    size_t basename_len;
    size_t root_len = 0;
    BOOL lower_case;
    const char *ipe_dir;
    const char *base_dir;
    const char *info_name;

    sep_pos = FindLastSeparator(infile);
    dot_pos = strrchr(infile, '.');
    without_ext_len = strlen(infile);
    if (dot_pos != NULL && (sep_pos == NULL || dot_pos > sep_pos)) {
        without_ext_len = (size_t)(dot_pos - infile);
    }

    without_ext = DupPrefix(infile, without_ext_len);
    if (without_ext == NULL) {
        return NULL;
    }

    sep_pos = FindLastSeparator(without_ext);
    if (sep_pos == NULL) {
        basename = without_ext;
        basename_len = strlen(basename);
    } else {
        basename = sep_pos + 1;
        basename_len = strlen(basename);
        containing_dir = DupPrefix(without_ext, (size_t)(basename - without_ext));
        if (containing_dir == NULL) {
            free(without_ext);
            return NULL;
        }
    }

    if (containing_dir != NULL) {
        size_t len = strlen(containing_dir);

        if (len > 0 && containing_dir[len - 1] == '/') {
            containing_dir[len - 1] = '\0';
        }
        sep_pos = FindLastSeparator(containing_dir);
        if (sep_pos != NULL) {
            root_len = (size_t)(sep_pos - containing_dir) + 1;
            root_dir = DupPrefix(containing_dir, root_len);
            if (root_dir == NULL) {
                free(containing_dir);
                free(without_ext);
                return NULL;
            }
        }
    }

    lower_case = LooksLowerCase(infile) || LooksLowerCase(basename);
    ipe_dir = lower_case ? "ipe/" : "IPE/";
    base_dir = lower_case ? "64base/" : "64BASE/";
    info_name = lower_case ? "info.ic" : "INFO.IC";

    path = (char *)malloc(root_len + strlen(ipe_dir) + basename_len + 1 +
        strlen(base_dir) + strlen(info_name) + 1);
    if (path != NULL) {
        path[0] = '\0';
        if (root_dir != NULL) {
            strcat(path, root_dir);
        }
        strcat(path, ipe_dir);
        strcat(path, basename);
        strcat(path, "/");
        strcat(path, base_dir);
        strcat(path, info_name);
    }

    free(root_dir);
    free(containing_dir);
    free(without_ext);
    return path;
}

static ULONG GetPicture(Class *cl, Object *obj, CONST_STRPTR dname)
{
    ULONG error = 0;

    (void)cl;

    if (dname != NULL) {
        LONG res;

        res = ReadPrefs();

        {
            photocdDecode *decoder = NULL;
            struct BitMapHeader *bh = NULL;
            char *ipe_path = NULL;
            const char *ipe_file = NULL;
            UBYTE *rowbuf = NULL;
            ULONG width;
            ULONG height;

            decoder = (photocdDecode *)malloc(sizeof(*decoder));
            if (decoder == NULL) {
                error = ERROR_NO_FREE_STORE;
                return error;
            }

            photocdDecode_init(decoder);
            if (res >= k64Base) {
                ipe_path = DeriveIpePath(dname);
                if (ipe_path == NULL) {
                    error = ERROR_NO_FREE_STORE;
                    goto cleanup;
                }
                ipe_file = ipe_path;
            }

            if (!photocdDecode_parseFile(decoder, dname, ipe_file, (unsigned int)res)) {
                error = IoErr() != 0 ? IoErr() : ERROR_OBJECT_WRONG_TYPE;
                goto cleanup;
            }

            GetDTAttrs(obj, PDTA_BitMapHeader, (ULONG)&bh, TAG_END);
            if (bh == NULL) {
                error = ERROR_NO_FREE_STORE;
                goto cleanup;
            }

            width = photocdDecode_getWidth(decoder);
            height = photocdDecode_getHeight(decoder);

            bh->bmh_Width = width;
            bh->bmh_PageWidth = width;
            bh->bmh_Height = height;
            bh->bmh_PageHeight = height;
            bh->bmh_Compression = 1;
            bh->bmh_Depth = 24;
            SetDTAttrs(obj, NULL, NULL, DTA_ErrorNumber, error,
                DTA_NominalHoriz, width, DTA_NominalVert, height, PDTA_ModeID, 0,
                PDTA_SourceMode, PMODE_V43, TAG_END);
            if (error != 0) {
                goto cleanup;
            }

            if (!photocdDecode_postParse(decoder)) {
                error = ERROR_NO_FREE_STORE;
                goto cleanup;
            }

            rowbuf = (UBYTE *)malloc(width * 3);
            if (rowbuf == NULL) {
                error = ERROR_NO_FREE_STORE;
                goto cleanup;
            }

            error = WriteRGBRows_V43(obj, bh, decoder, rowbuf);

cleanup:
            free(rowbuf);
            free(ipe_path);
            if (decoder != NULL) {
                photocdDecode_cleanup(decoder);
                free(decoder);
            }
        }
    } else {
        error = ERROR_OBJECT_WRONG_TYPE;
    }

    if (error == 0) {
    }
    return error;
}

static ULONG mNew(Class *cl, Object *obj, struct opSet *msg)
{
    struct TagItem *ti;
    CONST_STRPTR source_name;

    ti = FindTagItem(DTA_SourceType, msg->ops_AttrList);
    if (ti != NULL) {
        if (ti->ti_Data != DTST_FILE && ti->ti_Data != DTST_CLIPBOARD &&
            ti->ti_Data != DTST_RAM) {
            SetIoErr(ERROR_OBJECT_WRONG_TYPE);
            return 0;
        }
    }

    ti = FindTagItem(DTA_Name, msg->ops_AttrList);
    source_name = ti != NULL ? (CONST_STRPTR)ti->ti_Data : NULL;

    if (obj == (Object *)cl) {
        obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg);
        if (obj != NULL) {
            ULONG error;

            error = GetPicture(cl, obj, source_name);
            if (error != 0) {
                SetIoErr(error);
                CoerceMethod(cl, obj, OM_DISPOSE);
                obj = NULL;
            }
        }
    } else {
        SetIoErr(ERROR_NOT_IMPLEMENTED);
        obj = NULL;
    }

    return (ULONG)obj;
}

static ULONG mSet(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG retval;

    retval = DoSuperMethodA(cl, obj, (Msg)msg);
    if (retval != 0) {
        struct RastPort *rp;

        rp = ObtainGIRPort(msg->ops_GInfo);
        if (rp != NULL) {
            DoMethod(obj, GM_RENDER, msg->ops_GInfo, rp, GREDRAW_UPDATE);
            ReleaseGIRPort(rp);
            retval = 0;
        }
    }

    return retval;
}

__ASM__ ULONG Dispatcher(__REG__(a0, Class *cl),
    __REG__(a2, Object *obj), __REG__(a1, Msg msg))
{
    ULONG retval;

    switch (msg->MethodID) {
        case OM_NEW:
            retval = mNew(cl, obj, (struct opSet *)msg);
            break;

        case OM_UPDATE:
            if (DoMethod(obj, ICM_CHECKLOOP) != 0) {
                retval = 0;
                break;
            }
            /* fall through */

        case OM_SET:
            retval = mSet(cl, obj, (struct opSet *)msg);
            break;

        default:
            retval = DoSuperMethodA(cl, obj, msg);
            break;
    }

    return retval;
}
