#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/intuition.h>
#include <workbench/startup.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    GID_RESOLUTION = 1,
    GID_SAVE,
    GID_USE,
    GID_CANCEL
};

static const CONST_STRPTR cycle_res[] = {
    (CONST_STRPTR)"192*128",
    (CONST_STRPTR)"384*256",
    (CONST_STRPTR)"768*512",
    (CONST_STRPTR)"1536*1024",
    (CONST_STRPTR)"3072*2048",
    (CONST_STRPTR)"6144*4096",
    NULL
};

struct IntuitionBase *IntuitionBase;
struct Library *GadToolsBase;

static LONG ClampResolution(LONG value)
{
    if (value < 0) {
        return 0;
    }

    if (value > 5) {
        return 5;
    }

    return value;
}

static LONG LoadENV(void)
{
    LONG ret = 2;
    BPTR fh;

    fh = Open("ENV:DataTypes/photocd.prefs", MODE_OLDFILE);
    if (fh != 0) {
        char text[20];

        if (FGets(fh, text, sizeof(text)) != NULL) {
            ret = ClampResolution(atol(text));
        }

        Close(fh);
    }

    return ret;
}

static BOOL SaveFile(CONST_STRPTR path, ULONG active)
{
    BPTR lock;
    BPTR file;
    char dir[64];
    CONST_STRPTR slash;

    slash = strrchr(path, '/');
    if (slash != NULL) {
        size_t len = (size_t)(slash - path);

        if (len < sizeof(dir)) {
            memcpy(dir, path, len);
            dir[len] = '\0';

            lock = Lock(dir, ACCESS_READ);
            if (lock != 0) {
                UnLock(lock);
            } else {
                lock = CreateDir(dir);
                if (lock != 0) {
                    UnLock(lock);
                } else {
                    return FALSE;
                }
            }
        }
    }

    file = Open(path, MODE_NEWFILE);
    if (file != 0) {
        FPrintf(file, "%ld\n", active);
        Flush(file);
        Close(file);
        return TRUE;
    }

    return FALSE;
}

static ULONG GetSelectedResolution(struct Gadget *cycle_gad, struct Window *win, ULONG fallback)
{
    if (cycle_gad != NULL && GadToolsBase != NULL && GadToolsBase->lib_Version >= 39) {
        ULONG active = fallback;
        struct TagItem tags[] = {
            { GTCY_Active, (ULONG)&active },
            { TAG_DONE, 0 }
        };

        if (GT_GetGadgetAttrsA(cycle_gad, win, NULL, tags) > 0) {
            return (ULONG)ClampResolution((LONG)active);
        }
    }

    return fallback;
}

static void FormatDatatypeStatus(char *buffer, size_t size)
{
    struct Library *base;

    if (buffer == NULL || size == 0) {
        return;
    }

    base = OpenLibrary("datatypes/photocd.datatype", 0);
    if (base != NULL) {
        snprintf(buffer, size, "Datatype: installed (%u.%u)",
            (unsigned int)base->lib_Version, (unsigned int)base->lib_Revision);
        CloseLibrary(base);
        return;
    }

    snprintf(buffer, size, "Datatype: not found or failed to load");
}

static void DrawDatatypeStatus(struct Window *win, APTR visual_info,
    struct DrawInfo *draw_info, CONST_STRPTR status)
{
    struct IntuiText text;
    struct TagItem bevel_tags[] = {
        { GT_VisualInfo, 0 },
        { GTBB_Recessed, TRUE },
        { TAG_DONE, 0 }
    };
    UWORD text_pen;
    UWORD back_pen;

    if (win == NULL || visual_info == NULL || status == NULL) {
        return;
    }

    bevel_tags[0].ti_Data = (ULONG)visual_info;
    DrawBevelBoxA(win->RPort, 24, 36, 292, 18, bevel_tags);

    text_pen = draw_info != NULL ? draw_info->dri_Pens[TEXTPEN] : 1;
    back_pen = draw_info != NULL ? draw_info->dri_Pens[BACKGROUNDPEN] : 0;

    text.FrontPen = text_pen;
    text.BackPen = back_pen;
    text.DrawMode = JAM2;
    text.LeftEdge = 30;
    text.TopEdge = 41;
    text.ITextFont = win->WScreen->Font;
    text.IText = (STRPTR)status;
    text.NextText = NULL;

    PrintIText(win->RPort, &text, 0, 0);
}

static void InitNewGadgetFields(struct NewGadget *ng, struct Window *win, APTR visual_info,
    UWORD left, UWORD top, UWORD width, UWORD height, CONST_STRPTR text, UWORD id)
{
    ng->ng_LeftEdge = left;
    ng->ng_TopEdge = top;
    ng->ng_Width = width;
    ng->ng_Height = height;
    ng->ng_GadgetText = text;
    ng->ng_TextAttr = win->WScreen->Font;
    ng->ng_GadgetID = id;
    ng->ng_Flags = 0;
    ng->ng_VisualInfo = visual_info;
    ng->ng_UserData = NULL;
}

static BOOL OpenLibraries(void)
{
    if (DOSBase == NULL) {
        return FALSE;
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    if (IntuitionBase == NULL) {
        return FALSE;
    }

    GadToolsBase = OpenLibrary("gadtools.library", 37);
    if (GadToolsBase == NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }

    return TRUE;
}

static void CloseLibraries(void)
{
    if (GadToolsBase != NULL) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
    }

    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

static void DisposePrefsUI(struct Screen *screen, struct DrawInfo *draw_info,
    struct Window *win, struct Gadget *gadlist, APTR visual_info)
{
    if (win != NULL) {
        CloseWindow(win);
    }

    if (gadlist != NULL) {
        FreeGadgets(gadlist);
    }

    if (visual_info != NULL) {
        FreeVisualInfo(visual_info);
    }

    if (screen != NULL && draw_info != NULL) {
        FreeScreenDrawInfo(screen, draw_info);
    }
}

static int RunPrefs(void)
{
    enum
    {
        WINDOW_WIDTH = 340,
        OUTER_MARGIN = 24,
        CYCLE_WIDTH = 160
    };
    struct Screen *screen;
    struct DrawInfo *draw_info;
    struct Window *win;
    struct Gadget *gadlist = NULL;
    struct Gadget *gad;
    struct Gadget *cycle_gad;
    struct NewGadget ng;
    struct TagItem win_tags[] = {
        { WA_Title, (ULONG)"PhotoCD Preferences" },
        { WA_Left, 100 },
        { WA_Top, 40 },
        { WA_Width, 340 },
        { WA_Height, 115 },
        { WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
            WFLG_SMART_REFRESH | WFLG_ACTIVATE },
        { WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW |
            IDCMP_VANILLAKEY },
        { WA_PubScreen, 0 },
        { TAG_DONE, 0 }
    };
    const struct TagItem cycle_tags_template[] = {
        { GTCY_Labels, (ULONG)cycle_res },
        { GTCY_Active, 0 },
        { GT_Underscore, '_' },
        { TAG_DONE, 0 }
    };
    const struct TagItem button_tags[] = {
        { GT_Underscore, '_' },
        { TAG_DONE, 0 }
    };
    struct TagItem cycle_tags[sizeof(cycle_tags_template) / sizeof(cycle_tags_template[0])];
    APTR visual_info;
    ULONG active;
    char datatype_status[80];
    BOOL done = FALSE;

    screen = LockPubScreen(NULL);
    if (screen == NULL) {
        return 20;
    }

    win_tags[7].ti_Data = (ULONG)screen;
    win = OpenWindowTagList(NULL, win_tags);
    if (win == NULL) {
        UnlockPubScreen(NULL, screen);
        return 20;
    }

    visual_info = GetVisualInfoA(screen, NULL);
    draw_info = GetScreenDrawInfo(screen);
    UnlockPubScreen(NULL, screen);
    if (visual_info == NULL || draw_info == NULL) {
        if (draw_info != NULL) {
            FreeScreenDrawInfo(screen, draw_info);
        }
        CloseWindow(win);
        return 20;
    }

    gad = CreateContext(&gadlist);
    if (gad == NULL) {
        DisposePrefsUI(screen, draw_info, win, gadlist, visual_info);
        return 20;
    }

    active = (ULONG)LoadENV();
    FormatDatatypeStatus(datatype_status, sizeof(datatype_status));
    memcpy(cycle_tags, cycle_tags_template, sizeof(cycle_tags));
    cycle_tags[1].ti_Data = active;

    InitNewGadgetFields(&ng, win, visual_info,
        WINDOW_WIDTH - OUTER_MARGIN - CYCLE_WIDTH, 18, CYCLE_WIDTH, 14,
        (CONST_STRPTR)"_Resolution",
        GID_RESOLUTION);
    cycle_gad = CreateGadgetA(CYCLE_KIND, gad, &ng, cycle_tags);
    if (cycle_gad == NULL) {
        DisposePrefsUI(screen, draw_info, win, gadlist, visual_info);
        return 20;
    }
    gad = cycle_gad;

    InitNewGadgetFields(&ng, win, visual_info, 24, 70, 80, 14, (CONST_STRPTR)"_Save",
        GID_SAVE);
    gad = CreateGadgetA(BUTTON_KIND, gad, &ng, button_tags);
    if (gad == NULL) {
        DisposePrefsUI(screen, draw_info, win, gadlist, visual_info);
        return 20;
    }

    InitNewGadgetFields(&ng, win, visual_info, 128, 70, 80, 14, (CONST_STRPTR)"_Use",
        GID_USE);
    gad = CreateGadgetA(BUTTON_KIND, gad, &ng, button_tags);
    if (gad == NULL) {
        DisposePrefsUI(screen, draw_info, win, gadlist, visual_info);
        return 20;
    }

    InitNewGadgetFields(&ng, win, visual_info, 232, 70, 80, 14, (CONST_STRPTR)"_Cancel",
        GID_CANCEL);
    gad = CreateGadgetA(BUTTON_KIND, gad, &ng, button_tags);
    if (gad == NULL) {
        DisposePrefsUI(screen, draw_info, win, gadlist, visual_info);
        return 20;
    }

    AddGList(win, gadlist, -1, -1, NULL);
    RefreshGList(gadlist, win, NULL, (LONG)-1);
    GT_RefreshWindow(win, NULL);
    DrawDatatypeStatus(win, visual_info, draw_info, datatype_status);

    while (!done) {
        struct IntuiMessage *imsg;

        WaitPort(win->UserPort);
        while ((imsg = (struct IntuiMessage *)GT_GetIMsg(win->UserPort)) != NULL) {
            ULONG cls = imsg->Class;
            UWORD code = imsg->Code;
            struct Gadget *gg = (struct Gadget *)imsg->IAddress;
            UWORD gid = gg != NULL ? gg->GadgetID : 0;

            GT_ReplyIMsg(imsg);

            switch (cls) {
                case IDCMP_CLOSEWINDOW:
                    done = TRUE;
                    break;

                case IDCMP_REFRESHWINDOW:
                    GT_BeginRefresh(win);
                    GT_EndRefresh(win, TRUE);
                    DrawDatatypeStatus(win, visual_info, draw_info, datatype_status);
                    break;

                case IDCMP_GADGETUP:
                    if (gid == GID_RESOLUTION) {
                        active = code;
                    } else if (gid == GID_SAVE) {
                        active = GetSelectedResolution(cycle_gad, win, active);
                        if (SaveFile("ENV:DataTypes/photocd.prefs", active) &&
                            SaveFile("ENVARC:DataTypes/photocd.prefs", active)) {
                            done = TRUE;
                        } else {
                            DisplayBeep(win->WScreen);
                        }
                    } else if (gid == GID_USE) {
                        active = GetSelectedResolution(cycle_gad, win, active);
                        if (SaveFile("ENV:DataTypes/photocd.prefs", active)) {
                            done = TRUE;
                        } else {
                            DisplayBeep(win->WScreen);
                        }
                    } else if (gid == GID_CANCEL) {
                        done = TRUE;
                    }
                    break;

                case IDCMP_VANILLAKEY:
                    if (code == 27) {
                        done = TRUE;
                    }
                    break;
            }
        }
    }

    DisposePrefsUI(screen, draw_info, win, gadlist, visual_info);

    return 0;
}

int main(int argc, char **argv)
{
    int result;

    (void)argc;
    (void)argv;

    if (!OpenLibraries()) {
        result = 20;
    } else {
        result = RunPrefs();
        CloseLibraries();
    }

    return result;
}
