#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web.h"
#include "index.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")

/* ---------- GDI+ (smooth drawing) declarations for plain C ---------- */

typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback;
                 BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupInput;
typedef struct { INT X, Y, Width, Height; } GpRectI;

int  WINAPI GdiplusStartup(ULONG_PTR *token, const GpStartupInput *in, void *out);
void WINAPI GdiplusShutdown(ULONG_PTR token);
int  WINAPI GdipCreateFromHDC(HDC dc, void **g);
int  WINAPI GdipDeleteGraphics(void *g);
int  WINAPI GdipFlush(void *g, int intent);
int  WINAPI GdipSetSmoothingMode(void *g, int mode);
int  WINAPI GdipSetInterpolationMode(void *g, int mode);
int  WINAPI GdipCreateSolidFill(DWORD argb, void **brush);
int  WINAPI GdipDeleteBrush(void *brush);
int  WINAPI GdipCreatePen1(DWORD argb, float width, int unit, void **pen);
int  WINAPI GdipDeletePen(void *pen);
int  WINAPI GdipCreatePath(int fill, void **path);
int  WINAPI GdipDeletePath(void *path);
int  WINAPI GdipAddPathArc(void *path, float x, float y, float w, float h, float start, float sweep);
int  WINAPI GdipClosePathFigure(void *path);
int  WINAPI GdipFillPath(void *g, void *brush, void *path);
int  WINAPI GdipDrawPath(void *g, void *pen, void *path);
int  WINAPI GdipFillEllipseI(void *g, void *brush, int x, int y, int w, int h);
int  WINAPI GdipFillRectangleI(void *g, void *brush, int x, int y, int w, int h);
int  WINAPI GdipCreateLineBrushFromRectI(const GpRectI *r, DWORD c1, DWORD c2, int mode, int wrap, void **brush);
int  WINAPI GdipDrawEllipseI(void *g, void *pen, int x, int y, int w, int h);
int  WINAPI GdipDrawLineI(void *g, void *pen, int x1, int y1, int x2, int y2);
int  WINAPI GdipSetPenStartCap(void *pen, int cap);
int  WINAPI GdipSetPenEndCap(void *pen, int cap);
int  WINAPI GdipSetClipRectI(void *g, int x, int y, int w, int h, int mode);
int  WINAPI GdipResetClip(void *g);

/* ---------- constants ---------- */

#define WM_RESULTS (WM_APP + 1)
#define WM_QA      (WM_APP + 2)
#define WM_PAGE    (WM_APP + 3)
#define WM_CHECK   (WM_APP + 4)
#define WM_TOPIC   (WM_APP + 5)
#define TOPIC_H    118

#define HEAD_H     120
#define RES_TOP    140
#define CARD_H     124
#define CARD_STEP  138
#define QA_H       220
#define TEXT_MAX   50000
#define MAX_HIST   4

enum { V_RES, V_READER };

/* Pitch black, white and clean blue */
#define K_BG1    0x0B0D12
#define K_BG2    0x050608
#define K_CARD   0x121212
#define K_HOT    0x1C1C1C
#define K_EDGE   0x262626
#define K_ACCENT 0x2563EB
#define K_BLUE   0x3B82F6
#define K_TITLE  0xFFFFFF
#define K_TEXT   0xCCCCCC
#define K_DIM    0x737373

static const unsigned FAV[6] = {0x2563EB, 0x3B82F6, 0x1D4ED8, 0x60A5FA, 0x3B82F6, 0x2563EB};
static const char *TRENDING[4] = {"Artificial Intelligence", "C Programming", "Quantum Physics", "Space Exploration"};

typedef struct {
    char title[MAX_WEB_TITLE];
    char url[MAX_WEB_URL];
    char domain[128];
    char snippet[600];
    double score;
} Card;

typedef struct { int n; WebResult r[MAX_WEB_RESULTS]; } ResPayload;
typedef struct { int ok; char title[256]; char text[1500]; } QAPayload;
typedef struct { int gen; char q[256]; char url[MAX_WEB_URL]; } Job;
typedef struct { int verdict; char url[MAX_WEB_URL]; } CheckPayload;

/* ---------- state ---------- */

static HWND hMain, hEdit;
static HFONT fHero, fTitle, fBig, fUi, fSmall, fChip, fQT, fHead, fEdit, fRead;
static HBRUSH editBrush;
static WNDPROC oldEdit;
static void *gG = NULL;

static Card cards[MAX_WEB_RESULTS];
static int ncards = 0;

static int searched = 0, view = V_RES, cur = -1;
static int hoverCard = -1, hoverChip = -1;
static int scrollY = 0, readerY = 0, readerTotal = 0, phase = 0;
static int resBusy = 0, qaBusy = 0, readerLoading = 0, qaOk = 0;
static volatile LONG gen = 0, pageGen = 0;
static char status[200] = "";
static char qaTitle[256], qaText[1500];
static char *readerText = NULL;
static RECT homeBtnR, backR, openR, btnR, chipR[4], histR[MAX_HIST], topicR;

/* "Topic of the day": a different subject every day, summarised on the home screen */
static const char *TOPICS[] = {"Black hole", "Photosynthesis", "Roman Empire", "Artificial intelligence",
    "DNA", "Industrial Revolution", "Solar System", "Quantum mechanics", "Great Wall of China",
    "Renaissance", "Climate change", "Internet", "Human brain", "Volcano", "Printing press",
    "Electric motor", "Vaccine", "Great Pyramid of Giza", "Plate tectonics", "Evolution",
    "Theory of relativity", "Mount Everest", "Amazon rainforest", "Apollo 11", "Periodic table",
    "Steam engine", "Penicillin", "Jupiter", "Coral reef", "Gravity", "Magnetism", "Silk Road",
    "Aurora", "Honey bee", "Suspension bridge", "Telescope", "Bicycle", "Chess", "Olympic Games",
    "Mona Lisa", "Leonardo da Vinci", "Marie Curie", "Albert Einstein", "Isaac Newton",
    "Nikola Tesla", "Ada Lovelace", "Alan Turing", "Taj Mahal", "Himalayas", "Sahara", "Nile",
    "Tiger", "Elephant", "Dolphin", "Moon", "Mars", "Saturn", "Rainbow", "Earthquake", "Tsunami"};
static char topicQ[64] = "";
static char topicTitle[256], topicText[1500];
static int topicOk = 0, topicBusy = 1, hoverTopic = 0;
static char lastQuery[256] = "";
static char hist[MAX_HIST][64];
static int nhist = 0;
static int hidden = 0, ndead = 0;
static char dead[16][MAX_WEB_URL];

/* reader: page text split into blocks (0 = paragraph, 1 = heading, 2 = bullet) */
#define MAX_BLK 1500
static char *blk_s[MAX_BLK];
static int blk_t[MAX_BLK], blk_off[MAX_BLK + 1], blk_n = 0, blk_w = 0;

static void parse_blocks(void)
{
    char *p = readerText;
    blk_n = 0; blk_w = 0;
    while (p && *p && blk_n < MAX_BLK) {
        char *e = strchr(p, '\n');
        if (e) *e = '\0';
        if (*p) {
            int t = 0;
            if (p[0] == '#' && p[1] == ' ') { t = 1; p += 2; }
            else if (p[0] == '-' && p[1] == ' ') { t = 2; p += 2; }
            if (*p) { blk_s[blk_n] = p; blk_t[blk_n] = t; blk_n++; }
        }
        if (!e) break;
        p = e + 1;
    }
}

/* ---------- helpers ---------- */

static COLORREF cr(unsigned rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
static DWORD ar(int a, unsigned rgb) { return ((DWORD)a << 24) | rgb; }

static unsigned mixc(unsigned a, unsigned b, int p)
{
    int r = (int)((a >> 16) & 255) + ((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * p / 255;
    int g = (int)((a >> 8) & 255) + ((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * p / 255;
    int bl = (int)(a & 255) + ((int)(b & 255) - (int)(a & 255)) * p / 255;
    return ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)bl;
}

static HFONT make_font(int px, int weight)
{
    return CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
}

static void domain_of(const char *url, char *out, size_t n)
{
    const char *s = strstr(url, "://");
    size_t i = 0; s = s ? s + 3 : url;
    while (*s && *s != '/' && i + 1 < n) out[i++] = *s++;
    out[i] = '\0';
}

static const char *ci_find(const char *t, const char *p)
{
    size_t n = strlen(p);
    if (!n) return NULL;
    for (; *t; t++) if (_strnicmp(t, p, n) == 0) return t;
    return NULL;
}

static int by_score(const void *a, const void *b)
{
    double x = ((const Card *)a)->score, y = ((const Card *)b)->score;
    return (y > x) - (y < x);
}

static wchar_t *to_wide(const char *s, wchar_t *small, int small_n)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = small;
    if (n <= 0) return NULL;
    if (n > small_n) w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void txt(HDC dc, HFONT f, COLORREF c, const char *s, int l, int t, int r, int b, UINT fmt)
{
    wchar_t sb[1024], *w = to_wide(s, sb, 1024);
    RECT rc; rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
    if (!w) return;
    if (gG) GdipFlush(gG, 1);
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, w, -1, &rc, fmt | DT_NOPREFIX);
    if (w != sb) free(w);
}

static int txt_h(HDC dc, HFONT f, const char *s, int width)
{
    wchar_t sb[1024], *w = to_wide(s, sb, 1024);
    RECT rc = {0, 0, 0, 0};
    rc.right = width;
    if (!w) return 0;
    SelectObject(dc, f);
    DrawTextW(dc, w, -1, &rc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    if (w != sb) free(w);
    return rc.bottom;
}

static int txt_w(HDC dc, HFONT f, const char *s)
{
    wchar_t sb[256], *w = to_wide(s, sb, 256);
    SIZE sz = {0, 0};
    if (!w) return 0;
    SelectObject(dc, f);
    GetTextExtentPoint32W(dc, w, (int)wcslen(w), &sz);
    if (w != sb) free(w);
    return sz.cx;
}

static void *rr_path(float x, float y, float w, float h, float r)
{
    void *p; float d;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    d = r * 2;
    GdipCreatePath(0, &p);
    if (d < 1) d = 1;
    GdipAddPathArc(p, x, y, d, d, 180, 90);
    GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(p);
    return p;
}

static void fill_rr(void *g, int x, int y, int w, int h, int r, DWORD fill, DWORD border)
{
    void *p = rr_path((float)x, (float)y, (float)w, (float)h, (float)r);
    if (fill >> 24) { void *b; GdipCreateSolidFill(fill, &b); GdipFillPath(g, b, p); GdipDeleteBrush(b); }
    if (border >> 24) { void *pen; GdipCreatePen1(border, 1.2f, 2, &pen); GdipDrawPath(g, pen, p); GdipDeletePen(pen); }
    GdipDeletePath(p);
}

static void grad_rr(void *g, int x, int y, int w, int h, int r, DWORD c1, DWORD c2, int mode)
{
    GpRectI rc;
    void *b, *p;
    rc.X = x; rc.Y = y; rc.Width = w > 0 ? w : 1; rc.Height = h > 0 ? h : 1;
    GdipCreateLineBrushFromRectI(&rc, c1, c2, mode, 0, &b);
    p = rr_path((float)x, (float)y, (float)w, (float)h, (float)r);
    GdipFillPath(g, b, p);
    GdipDeletePath(p); GdipDeleteBrush(b);
}

/* ---------- logo + background ---------- */

/* logo: blue rounded tile with a white magnifying glass */
static void draw_logo(void *g, int x, int y, int sz)
{
    void *pen;
    float pw = sz * 0.09f;
    int lx = x + sz * 21 / 100, ly = y + sz * 19 / 100, ld = sz * 38 / 100;

    if (pw < 2.0f) pw = 2.0f;
    grad_rr(g, x, y, sz, sz, sz * 22 / 100, ar(255, 0x3B82F6), ar(255, 0x1D4ED8), 1);

    GdipCreatePen1(ar(255, 0xFFFFFF), pw, 2, &pen);
    GdipSetPenStartCap(pen, 2);
    GdipSetPenEndCap(pen, 2);
    GdipDrawEllipseI(g, pen, lx, ly, ld, ld);
    GdipDrawLineI(g, pen, x + sz * 54 / 100, y + sz * 52 / 100, x + sz * 76 / 100, y + sz * 74 / 100);
    GdipDeletePen(pen);
}

/* background: a plain, very dark surface */
static void draw_bg(void *g, int w, int h)
{
    GpRectI full;
    void *bg;

    full.X = 0; full.Y = 0; full.Width = w; full.Height = h;
    GdipCreateLineBrushFromRectI(&full, ar(255, K_BG1), ar(255, K_BG2), 1, 0, &bg);
    GdipFillRectangleI(g, bg, 0, 0, w, h);
    GdipDeleteBrush(bg);
}

/* ---------- geometry ---------- */

static int CW(int w) { int c = w - 80; return c > 900 ? 900 : (c < 400 ? 400 : c); }
static int X0(int w) { return (w - CW(w)) / 2; }

static RECT box_rect(int w, int h)
{
    RECT r;
    if (!searched) {
        int pw = w - 100; if (pw > 700) pw = 700;
        r.left = (w - pw) / 2; r.right = r.left + pw;
        r.top = h / 3 - 20; if (r.top < 150) r.top = 150;
        r.bottom = r.top + 60;
    } else {
        r.left = X0(w) + 110; r.right = X0(w) + CW(w);
        r.top = 16; r.bottom = 66;
    }
    return r;
}

static RECT btn_rect(RECT b)
{
    RECT r;
    r.left = b.right - 104; r.top = b.top + 6;
    r.right = b.right - 6;  r.bottom = b.bottom - 6;
    return r;
}

static void layout_edit(int w, int h)
{
    RECT b = box_rect(w, h);
    int eh = 28;
    MoveWindow(hEdit, b.left + 22, b.top + (b.bottom - b.top - eh) / 2,
               b.right - b.left - 22 - 110, eh, TRUE);
}

static int qa_shown(void) { return qaOk || qaBusy; }

static int card_at(int x, int y, int w)
{
    int rel, idx;
    if (view != V_RES || !searched || y < HEAD_H || x < X0(w) || x > X0(w) + CW(w)) return -1;
    rel = y + scrollY - RES_TOP - (qa_shown() ? QA_H + 16 : 0);
    if (rel < 0) return -1;
    idx = rel / CARD_STEP;
    if (idx >= ncards || rel % CARD_STEP >= CARD_H) return -1;
    return idx;
}

/* which suggestion chip is under the mouse: 0..3 = suggestions, 10..13 = recent */
static int chip_at(POINT pt)
{
    int i;
    if (searched) return -1;
    for (i = 0; i < 4; i++) if (PtInRect(&chipR[i], pt)) return i;
    for (i = 0; i < nhist; i++) if (PtInRect(&histR[i], pt)) return 10 + i;
    return -1;
}

/* ---------- background work ---------- */

static DWORD WINAPI th_search(LPVOID a)
{
    Job *j = (Job *)a;
    ResPayload *r = (ResPayload *)calloc(1, sizeof(ResPayload));
    if (r) r->n = web_search(j->q, r->r, MAX_WEB_RESULTS);
    PostMessageA(hMain, WM_RESULTS, (WPARAM)j->gen, (LPARAM)r);
    free(j); return 0;
}

static DWORD WINAPI th_qa(LPVOID a)
{
    Job *j = (Job *)a;
    QAPayload *q = (QAPayload *)calloc(1, sizeof(QAPayload));
    if (q) q->ok = web_quick_answer(j->q, q->title, sizeof(q->title), q->text, sizeof(q->text));
    PostMessageA(hMain, WM_QA, (WPARAM)j->gen, (LPARAM)q);
    free(j); return 0;
}

static DWORD WINAPI th_check(LPVOID a)
{
    Job *j = (Job *)a;
    CheckPayload *c = (CheckPayload *)calloc(1, sizeof(CheckPayload));
    if (c) {
        c->verdict = web_page_check(j->url);
        snprintf(c->url, sizeof(c->url), "%s", j->url);
    }
    PostMessageA(hMain, WM_CHECK, (WPARAM)j->gen, (LPARAM)c);
    free(j); return 0;
}

static DWORD WINAPI th_topic(LPVOID a)
{
    Job *j = (Job *)a;
    QAPayload *q = (QAPayload *)calloc(1, sizeof(QAPayload));
    if (q) q->ok = web_quick_answer(j->q, q->title, sizeof(q->title), q->text, sizeof(q->text));
    PostMessageA(hMain, WM_TOPIC, 0, (LPARAM)q);
    free(j); return 0;
}

static DWORD WINAPI th_page(LPVOID a)
{
    Job *j = (Job *)a;
    char *buf = (char *)malloc(TEXT_MAX); char t[512];
    if (buf && !web_fetch_page(j->url, t, sizeof(t), buf, TEXT_MAX))
        buf[0] = '\0';                 /* empty = could not read, GUI shows a fallback */
    PostMessageA(hMain, WM_PAGE, (WPARAM)j->gen, (LPARAM)buf);
    free(j); return 0;
}

static void launch(LPTHREAD_START_ROUTINE fn, int g, const char *q, const char *url)
{
    Job *j = (Job *)calloc(1, sizeof(Job)); HANDLE th;
    if (!j) return;
    j->gen = g; snprintf(j->q, sizeof(j->q), "%s", q ? q : ""); snprintf(j->url, sizeof(j->url), "%s", url ? url : "");
    th = CreateThread(NULL, 0, fn, j, 0, NULL);
    if (th) CloseHandle(th); else free(j);
}

static void push_hist(const char *q)
{
    char tmp[64]; int i;
    snprintf(tmp, sizeof(tmp), "%s", q);
    for (i = 0; i < nhist; i++)
        if (_stricmp(hist[i], tmp) == 0) {
            memmove(&hist[i], &hist[i + 1], (size_t)(nhist - i - 1) * sizeof(hist[0]));
            nhist--;
            break;
        }
    if (nhist < MAX_HIST) nhist++;
    memmove(&hist[1], &hist[0], (size_t)(nhist - 1) * sizeof(hist[0]));
    snprintf(hist[0], sizeof(hist[0]), "%s", tmp);
}

static void begin_search(const char *q)
{
    int g; if (!q || !*q) return;
    g = (int)InterlockedIncrement(&gen);
    searched = 1; view = V_RES; scrollY = 0; cur = -1;
    ncards = 0; qaOk = 0; resBusy = qaBusy = 1; hidden = 0; ndead = 0;
    snprintf(status, sizeof(status), "Searching...");
    snprintf(lastQuery, sizeof(lastQuery), "%s", q);
    push_hist(q);
    { RECT rc; GetClientRect(hMain, &rc); layout_edit(rc.right, rc.bottom); }
    InvalidateRect(hMain, NULL, FALSE);
    launch(th_search, g, q, NULL); launch(th_qa, g, q, NULL);
}

static void search_from_edit(void)
{
    wchar_t wb[256]; char u[600]; char *s = u; size_t len;
    GetWindowTextW(hEdit, wb, 256);
    WideCharToMultiByte(CP_UTF8, 0, wb, -1, u, sizeof(u), NULL, NULL);
    while (*s == ' ') s++;
    len = strlen(s); while (len && s[len - 1] == ' ') s[--len] = '\0';
    if (len > 60) { s[60] = '\0'; }
    begin_search(s);
}

static void search_text(const char *s)
{
    wchar_t wb[256];
    MultiByteToWideChar(CP_UTF8, 0, s, -1, wb, 256);
    SetWindowTextW(hEdit, wb);
    begin_search(s);
}

static void open_reader(int idx)
{
    int g = (int)InterlockedIncrement(&pageGen);
    cur = idx; view = V_READER; readerY = 0; readerLoading = 1;
    free(readerText); readerText = NULL; blk_n = 0; blk_w = 0;
    launch(th_page, g, NULL, cards[idx].url);
    InvalidateRect(hMain, NULL, FALSE);
}

static void remove_card(int i)
{
    RECT rc; int maxs;
    if (i < 0 || i >= ncards) return;
    memmove(&cards[i], &cards[i + 1], (size_t)(ncards - i - 1) * sizeof(Card));
    ncards--; hidden++; hoverCard = -1;
    if (cur > i) cur--;
    if (hidden > 0) snprintf(status, sizeof(status), "%d results  -  %d empty pages hidden", ncards, hidden);
    else snprintf(status, sizeof(status), "%d results", ncards);
    GetClientRect(hMain, &rc);
    maxs = (RES_TOP + (qa_shown() ? QA_H + 16 : 0) + ncards * CARD_STEP + 24) - rc.bottom;
    if (maxs < 0) maxs = 0;
    if (scrollY > maxs) scrollY = maxs;
}

/* hide pages that were found to be empty (waits if one is open in the reader) */
static void apply_dead(void)
{
    int k, i;
    if (view == V_READER) return;
    for (k = 0; k < ndead; k++)
        for (i = 0; i < ncards; i++)
            if (strcmp(cards[i].url, dead[k]) == 0) { remove_card(i); break; }
    ndead = 0;
    InvalidateRect(hMain, NULL, FALSE);
}

static void go_home(void)
{
    searched = 0; view = V_RES;
    SetWindowTextW(hEdit, L"");
    { RECT rc; GetClientRect(hMain, &rc); layout_edit(rc.right, rc.bottom); }
    InvalidateRect(hMain, NULL, FALSE);
    SetFocus(hEdit);
}

static void go_back(void)
{
    if (view == V_READER) { view = V_RES; apply_dead(); InvalidateRect(hMain, NULL, FALSE); }
}

/* ---------- drawing ---------- */

static void draw_box(void *g, HDC dc, int w, int h)
{
    RECT b = box_rect(w, h);
    int foc = (GetFocus() == hEdit);
    int bh = b.bottom - b.top;

    fill_rr(g, b.left, b.top, b.right - b.left, bh, bh / 2, ar(255, K_CARD), ar(255, foc ? K_BLUE : K_EDGE));
    btnR = btn_rect(b);
    grad_rr(g, btnR.left, btnR.top, btnR.right - btnR.left, btnR.bottom - btnR.top,
            (btnR.bottom - btnR.top) / 2, ar(255, K_ACCENT), ar(255, K_BLUE), 0);
    txt(dc, fUi, RGB(255, 255, 255), "Search", btnR.left, btnR.top, btnR.right, btnR.bottom,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

static void draw_chips(void *g, HDC dc, int w, int y, const char **lab, int n, RECT *out, int hbase)
{
    int widths[MAX_HIST + 4], sum = 0, i, x;
    for (i = 0; i < n; i++) { widths[i] = txt_w(dc, fSmall, lab[i]) + 36; sum += widths[i]; }
    sum += 10 * (n - 1);
    x = (w - sum) / 2;
    for (i = 0; i < n; i++) {
        int hot = (hoverChip == hbase + i);
        fill_rr(g, x, y, widths[i], 34, 17, ar(255, hot ? K_HOT : K_CARD), ar(255, hot ? K_BLUE : K_EDGE));
        txt(dc, fSmall, cr(hot ? K_TITLE : K_TEXT), lab[i], x, y, x + widths[i], y + 34,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        out[i].left = x; out[i].top = y; out[i].right = x + widths[i]; out[i].bottom = y + 34;
        x += widths[i] + 10;
    }
}

static void draw_topic(void *g, HDC dc, int w, int y)
{
    int cw = w - 100, x, hot = hoverTopic;
    int pulse = (int)(128 + 127 * sin(phase * 0.18));
    unsigned bar = mixc(0x1A1A1A, 0x2A2A2A, pulse);

    if (cw > 700) cw = 700;
    x = (w - cw) / 2;
    topicR.left = x; topicR.top = y; topicR.right = x + cw; topicR.bottom = y + TOPIC_H;

    fill_rr(g, x, y, cw, TOPIC_H, 14, ar(255, hot ? K_HOT : K_CARD), ar(255, hot ? K_BLUE : K_EDGE));
    grad_rr(g, x + 14, y + 16, 4, TOPIC_H - 32, 2, ar(255, K_ACCENT), ar(255, K_BLUE), 1);
    txt(dc, fChip, cr(K_BLUE), "TOPIC OF THE DAY", x + 32, y + 14, x + 240, y + 32, DT_SINGLELINE);
    if (topicOk) {
        txt(dc, fSmall, cr(K_BLUE), "Read more  >", x + cw - 130, y + 14, x + cw - 20, y + 32,
            DT_SINGLELINE | DT_RIGHT);
        txt(dc, fTitle, cr(K_TITLE), topicTitle, x + 32, y + 34, x + cw - 24, y + 60,
            DT_SINGLELINE | DT_END_ELLIPSIS);
        txt(dc, fUi, cr(K_TEXT), topicText, x + 32, y + 62, x + cw - 24, y + TOPIC_H - 10,
            DT_WORDBREAK | DT_END_ELLIPSIS);
    } else {
        fill_rr(g, x + 32, y + 38, cw / 4, 16, 8, ar(255, bar), 0);
        fill_rr(g, x + 32, y + 68, cw - 90, 10, 5, ar(255, bar), 0);
        fill_rr(g, x + 32, y + 88, cw / 2, 10, 5, ar(255, bar), 0);
    }
}

static void draw_home(void *g, HDC dc, int w, int h)
{
    RECT b = box_rect(w, h);
    int tw, total, x, y, rows, i;
    const char *lab[MAX_HIST];

    tw = txt_w(dc, fHero, "Mini Search");
    total = 60 + 18 + tw;
    x = (w - total) / 2;
    y = b.top - 112;

    draw_logo(g, x, y + 2, 60);
    txt(dc, fHero, cr(K_TITLE), "Mini Search", x + 78, y - 4, x + 78 + tw + 12, y + 68, DT_SINGLELINE | DT_VCENTER);

    {
        SYSTEMTIME st;
        char greet[96];
        GetLocalTime(&st);
        snprintf(greet, sizeof(greet), "%s. What would you like to know?",
                 st.wHour < 12 ? "Good morning" : (st.wHour < 17 ? "Good afternoon" : "Good evening"));
        txt(dc, fUi, cr(K_TEXT), greet, 0, b.top - 34, w, b.top - 12, DT_SINGLELINE | DT_CENTER);
    }

    draw_box(g, dc, w, h);

    y = b.bottom + 36;
    rows = 0;
    if (nhist > 0) {
        for (i = 0; i < nhist; i++) lab[i] = hist[i];
        txt(dc, fSmall, cr(K_DIM), "Recent searches", 0, y, w, y + 18, DT_SINGLELINE | DT_CENTER);
        draw_chips(g, dc, w, y + 24, lab, nhist, histR, 10);
        y += 84;
        rows = 1;
    }
    txt(dc, fSmall, cr(K_DIM), rows ? "Or try one of these" : "Try searching for", 0, y, w, y + 18,
        DT_SINGLELINE | DT_CENTER);
    draw_chips(g, dc, w, y + 24, TRENDING, 4, chipR, 0);

    y += 24 + 34 + 30;
    if ((topicOk || topicBusy) && y + TOPIC_H <= h - 56)
        draw_topic(g, dc, w, y);

    txt(dc, fSmall, cr(0x5A5F6B), "Type a word and press Enter", 0, h - 44, w, h - 20,
        DT_SINGLELINE | DT_CENTER);
}

static void draw_card(void *g, HDC dc, int i, int y, int x0, int cw, int hot)
{
    Card *c = &cards[i]; const char *d = c->domain, *p; char pct[32], letter[2]; unsigned hsh = 0;
    if (!_strnicmp(d, "www.", 4)) d += 4;
    for (p = d; *p; p++) hsh = hsh * 31 + (unsigned char)*p;

    fill_rr(g, x0, y, cw, CARD_H, 16, ar(255, hot ? K_HOT : K_CARD), ar(255, hot ? K_BLUE : K_EDGE));
    fill_rr(g, x0 + 20, y + 20, 36, 36, 10, ar(255, FAV[hsh % 6]), 0);
    letter[0] = (char)toupper((unsigned char)d[0]); letter[1] = '\0';
    txt(dc, fTitle, RGB(255, 255, 255), letter, x0 + 20, y + 20, x0 + 56, y + 56, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    txt(dc, fSmall, cr(K_DIM), d, x0 + 72, y + 14, x0 + cw - 110, y + 32, DT_SINGLELINE | DT_END_ELLIPSIS);
    snprintf(pct, sizeof(pct), "%d%% match", (int)(c->score * 100));
    txt(dc, fSmall, cr(K_BLUE), pct, x0 + cw - 115, y + 14, x0 + cw - 18, y + 32, DT_SINGLELINE | DT_RIGHT);
    txt(dc, fTitle, cr(K_TITLE), c->title, x0 + 72, y + 34, x0 + cw - 24, y + 62, DT_SINGLELINE | DT_END_ELLIPSIS);
    txt(dc, fUi, cr(K_TEXT), c->snippet, x0 + 72, y + 64, x0 + cw - 24, y + CARD_H - 10, DT_WORDBREAK | DT_END_ELLIPSIS);
}

static void draw_skel(void *g, int y, int x0, int cw, int pulse)
{
    unsigned base = mixc(0x101010, 0x1A1A1A, pulse), bar = mixc(0x1A1A1A, 0x2A2A2A, pulse);
    fill_rr(g, x0, y, cw, CARD_H, 16, ar(255, base), ar(255, K_EDGE));
    fill_rr(g, x0 + 20, y + 20, 36, 36, 18, ar(255, bar), 0);
    fill_rr(g, x0 + 72, y + 18, cw / 4, 12, 6, ar(255, bar), 0);
    fill_rr(g, x0 + 72, y + 42, cw / 2, 16, 8, ar(255, bar), 0);
    fill_rr(g, x0 + 72, y + 72, cw - 150, 10, 5, ar(255, bar), 0);
    fill_rr(g, x0 + 72, y + 92, cw - 260, 10, 5, ar(255, bar), 0);
}

static void draw_qa(void *g, HDC dc, int y, int x0, int cw)
{
    fill_rr(g, x0, y, cw, QA_H, 18, ar(255, K_CARD), ar(255, K_BLUE));
    grad_rr(g, x0 + 16, y + 18, 4, QA_H - 36, 2, ar(255, K_ACCENT), ar(255, K_BLUE), 1);
    txt(dc, fChip, cr(K_BLUE), "QUICK ANSWER", x0 + 36, y + 16, x0 + 200, y + 38, DT_SINGLELINE);
    txt(dc, fQT, cr(K_TITLE), qaTitle, x0 + 36, y + 44, x0 + cw - 24, y + 78, DT_SINGLELINE | DT_END_ELLIPSIS);
    txt(dc, fUi, cr(K_TEXT), qaText, x0 + 36, y + 82, x0 + cw - 24, y + QA_H - 20, DT_WORDBREAK | DT_END_ELLIPSIS);
}

static void draw_qa_skel(void *g, int y, int x0, int cw, int pulse)
{
    unsigned bar = mixc(0x1A1A1A, 0x2A2A2A, pulse);
    fill_rr(g, x0, y, cw, QA_H, 18, ar(255, K_CARD), ar(255, K_EDGE));
    fill_rr(g, x0 + 36, y + 18, 110, 14, 7, ar(255, bar), 0);
    fill_rr(g, x0 + 36, y + 48, cw / 3, 24, 8, ar(255, bar), 0);
    fill_rr(g, x0 + 36, y + 96, cw - 90, 12, 6, ar(255, bar), 0);
    fill_rr(g, x0 + 36, y + 120, cw - 120, 12, 6, ar(255, bar), 0);
    fill_rr(g, x0 + 36, y + 144, cw / 2, 12, 6, ar(255, bar), 0);
}

static void draw_reader(void *g, HDC dc, int w, int h)
{
    Card *c = &cards[cur]; int tw = CW(w) > 840 ? 840 : CW(w);
    int px = (w - tw) / 2, pad = 44, ix = px + pad, iw = tw - pad * 2;
    int y0 = RES_TOP - readerY, th = txt_h(dc, fBig, c->title, iw);
    int cy = 28 + th + 8 + 22 + 22, i, panelH; const char *domain = c->domain;

    if (!_strnicmp(domain, "www.", 4)) domain += 4;
    if (blk_w != iw) {
        int off = 0;
        for (i = 0; i < blk_n; i++) {
            blk_off[i] = off;
            if (blk_t[i] == 1) off += 22 + txt_h(dc, fHead, blk_s[i], iw) + 8;
            else if (blk_t[i] == 2) off += txt_h(dc, fRead, blk_s[i], iw - 28) + 8;
            else off += txt_h(dc, fRead, blk_s[i], iw) + 16;
        }
        blk_off[blk_n] = off; blk_w = iw;
    }
    panelH = cy + (blk_n ? blk_off[blk_n] : 60) + 40;
    fill_rr(g, px, y0, tw, panelH, 20, ar(255, K_CARD), ar(255, K_EDGE));
    grad_rr(g, px, y0, tw, 6, 3, ar(255, K_ACCENT), ar(255, K_BLUE), 0);
    txt(dc, fBig, cr(K_TITLE), c->title, ix, y0 + 28, ix + iw, y0 + 28 + th + 4, DT_WORDBREAK);
    txt(dc, fSmall, cr(K_BLUE), domain, ix, y0 + 28 + th + 8, ix + iw, y0 + 28 + th + 30, DT_SINGLELINE | DT_END_ELLIPSIS);

    if (!blk_n)
        txt(dc, fRead, cr(K_DIM), readerLoading ? "Loading page..." : "Nothing to show.",
            ix, y0 + cy, ix + iw, y0 + cy + 30, DT_SINGLELINE);

    for (i = 0; i < blk_n; i++) {
        int top = y0 + cy + blk_off[i], ht = blk_off[i + 1] - blk_off[i];
        if (top > h) break;
        if (top + ht < HEAD_H) continue;
        if (blk_t[i] == 1)
            txt(dc, fHead, cr(K_TITLE), blk_s[i], ix, top + 22, ix + iw, top + ht, DT_WORDBREAK);
        else if (blk_t[i] == 2) {
            fill_rr(g, ix + 6, top + 9, 8, 8, 4, ar(255, K_BLUE), 0);
            txt(dc, fRead, cr(K_TEXT), blk_s[i], ix + 28, top, ix + iw, top + ht, DT_WORDBREAK);
        } else
            txt(dc, fRead, cr(K_TEXT), blk_s[i], ix, top, ix + iw, top + ht, DT_WORDBREAK);
    }
    readerTotal = panelH;
}

static void draw_header(void *g, HDC dc, int w, int h)
{
    int x0 = X0(w), cw = CW(w);

    fill_rr(g, x0, 16, 100, 34, 17, ar(255, K_CARD), ar(255, K_EDGE));
    draw_logo(g, x0 + 7, 21, 24);
    txt(dc, fUi, cr(K_TITLE), "Home", x0 + 38, 16, x0 + 100, 50, DT_SINGLELINE | DT_VCENTER);
    homeBtnR.left = x0; homeBtnR.top = 16; homeBtnR.right = x0 + 100; homeBtnR.bottom = 50;

    draw_box(g, dc, w, h);

    if (view == V_RES) {
        txt(dc, fSmall, cr(K_DIM), status, x0 + cw - 280, 84, x0 + cw, 108, DT_SINGLELINE | DT_RIGHT);
    } else {
        fill_rr(g, x0, 80, 96, 30, 15, ar(255, K_CARD), ar(255, K_EDGE));
        txt(dc, fUi, cr(K_TEXT), "< Back", x0, 80, x0 + 96, 110, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        backR.left = x0; backR.top = 80; backR.right = x0 + 96; backR.bottom = 110;

        grad_rr(g, x0 + cw - 170, 80, 170, 30, 15, ar(255, K_ACCENT), ar(255, K_BLUE), 0);
        txt(dc, fUi, RGB(255, 255, 255), "Open in browser", x0 + cw - 170, 80, x0 + cw, 110, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        openR.left = x0 + cw - 170; openR.top = 80; openR.right = x0 + cw; openR.bottom = 110;
    }
}

static void paint(HDC dc, int w, int h)
{
    void *g = NULL;
    int x0 = X0(w), cw = CW(w), i;
    int pulse = (int)(128 + 127 * sin(phase * 0.18));

    GdipCreateFromHDC(dc, &g); gG = g;
    GdipSetSmoothingMode(g, 4); GdipSetInterpolationMode(g, 7); SetBkMode(dc, TRANSPARENT);

    SetRectEmpty(&homeBtnR); SetRectEmpty(&backR); SetRectEmpty(&openR); SetRectEmpty(&topicR);
    for (i = 0; i < 4; i++) SetRectEmpty(&chipR[i]);
    for (i = 0; i < MAX_HIST; i++) SetRectEmpty(&histR[i]);

    draw_bg(g, w, h);

    if (!searched) {
        draw_home(g, dc, w, h);
    } else {
        if (view == V_READER && cur >= 0 && cur < ncards) {
            draw_reader(g, dc, w, h);
        } else {
            int y = RES_TOP - scrollY;
            if (qaOk) draw_qa(g, dc, y, x0, cw);
            else if (qaBusy) draw_qa_skel(g, y, x0, cw, pulse);
            if (qa_shown()) y += QA_H + 16;

            if (ncards) {
                for (i = 0; i < ncards; i++, y += CARD_STEP) {
                    if (y + CARD_H < HEAD_H || y > h) continue;
                    draw_card(g, dc, i, y, x0, cw, i == hoverCard);
                }
            } else if (resBusy) {
                for (i = 0; i < 4; i++, y += CARD_STEP) draw_skel(g, y, x0, cw, pulse);
            } else {
                txt(dc, fTitle, cr(K_TEXT), "No results found", 0, y + 40, w, y + 74, DT_SINGLELINE | DT_CENTER);
                txt(dc, fUi, cr(K_DIM), "Check your internet connection or try different words.",
                    0, y + 80, w, y + 110, DT_SINGLELINE | DT_CENTER);
            }
        }
        GdipSetClipRectI(g, 0, 0, w, HEAD_H, 0);
        draw_bg(g, w, h);
        GdipResetClip(g);
        draw_header(g, dc, w, h);
        fill_rr(g, 0, HEAD_H - 1, w, 1, 0, ar(255, 0x1C1F26), 0);
    }

    GdipFlush(g, 1); GdipDeleteGraphics(g); gG = NULL;
}

/* ---------- window ---------- */

static int max_scroll(int h)
{
    int total = (view == V_READER) ? RES_TOP + readerTotal + 24
              : RES_TOP + (qa_shown() ? QA_H + 16 : 0) + ncards * CARD_STEP + 24;
    return total - h;
}

static int hit_any(POINT pt, int w)
{
    return PtInRect(&homeBtnR, pt) || PtInRect(&btnR, pt) || PtInRect(&backR, pt) ||
           PtInRect(&openR, pt) || (!searched && PtInRect(&topicR, pt)) || chip_at(pt) >= 0 || card_at(pt.x, pt.y, w) >= 0;
}

static LRESULT CALLBACK edit_proc(HWND hw, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_CHAR && wp == '\r') { search_from_edit(); return 0; }
    if (m == WM_KEYDOWN && wp == VK_ESCAPE) { go_back(); return 0; }
    return CallWindowProcW(oldEdit, hw, m, wp, lp);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        hMain = hwnd;
        fHero = make_font(46, FW_BOLD);
        fTitle = make_font(18, FW_SEMIBOLD); fBig = make_font(28, FW_BOLD);
        fUi = make_font(14, FW_NORMAL); fSmall = make_font(12, FW_NORMAL);
        fChip = make_font(11, FW_BOLD); fQT = make_font(22, FW_SEMIBOLD);
        fHead = make_font(21, FW_SEMIBOLD); fEdit = make_font(18, FW_NORMAL);
        fRead = make_font(17, FW_NORMAL);
        editBrush = CreateSolidBrush(cr(K_CARD));

        hEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 100, 28, hwnd, (HMENU)1, NULL, NULL);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)fEdit, TRUE);
        oldEdit = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            snprintf(topicQ, sizeof(topicQ), "%s",
                     TOPICS[(st.wYear * 372 + st.wMonth * 31 + st.wDay) % (int)(sizeof(TOPICS) / sizeof(TOPICS[0]))]);
            launch(th_topic, 0, topicQ, NULL);
        }
        SetTimer(hwnd, 1, 50, NULL); SetFocus(hEdit); return 0;

    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd, &rc);
        layout_edit(rc.right, rc.bottom);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = 760;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = 520;
        return 0;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, cr(K_TITLE)); SetBkColor((HDC)wp, cr(K_CARD)); return (LRESULT)editBrush;

    case WM_COMMAND:
        if (LOWORD(wp) == 1 && (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS))
            InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_TIMER:
        phase++;
        if (resBusy || qaBusy || (!searched && topicBusy)) InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_RESULTS: {
        ResPayload *rp = (ResPayload *)lp;
        if (rp && (int)wp == (int)gen) {
            static Index ix; char doc[3000]; double sc[MAX_DOCS], maxs = 0.0;
            int i, pass, n = 0;
            char qw[8][32]; int nq = 0;

            /* words of the search, used to favour results that name them in the title */
            {
                char t[256]; char *p;
                snprintf(t, sizeof(t), "%s", lastQuery);
                for (p = strtok(t, " "); p && nq < 8; p = strtok(NULL, " "))
                    if (strlen(p) >= 2) { snprintf(qw[nq], 32, "%s", p); nq++; }
            }

            index_init(&ix);
            for (pass = 0; pass < 2 && n == 0; pass++) {
                for (i = 0; i < rp->n && n < MAX_DOCS; i++) {
                    if (pass == 0 && web_is_junk_result(rp->r[i].title, rp->r[i].url, rp->r[i].snippet))
                        continue;
                    snprintf(cards[n].title, sizeof(cards[n].title), "%s", rp->r[i].title);
                    snprintf(cards[n].url, sizeof(cards[n].url), "%s", rp->r[i].url);
                    snprintf(cards[n].snippet, sizeof(cards[n].snippet), "%s", rp->r[i].snippet[0] ? rp->r[i].snippet : "Click to read this page.");
                    domain_of(cards[n].url, cards[n].domain, sizeof(cards[n].domain));
                    snprintf(doc, sizeof(doc), "%s %s", rp->r[i].title, rp->r[i].snippet);
                    index_add(&ix, n, doc);
                    n++;
                }
            }
            index_score(&ix, lastQuery, sc);
            for (i = 0; i < n; i++) if (sc[i] > maxs) maxs = sc[i];
            for (i = 0; i < n; i++) {
                int k, hit = 0; double tf;
                for (k = 0; k < nq; k++) if (ci_find(cards[i].title, qw[k])) hit++;
                tf = nq ? (double)hit / nq : 0.0;
                cards[i].score = (maxs > 0 ? sc[i] / maxs : 0.0) * 0.45 +
                                 (1.0 - (double)i / (n ? n : 1)) * 0.25 + tf * 0.30;
                if (cards[i].score > 1.0) cards[i].score = 1.0;
            }
            qsort(cards, (size_t)n, sizeof(Card), by_score); index_free(&ix);
            ncards = n; resBusy = 0; hidden = 0; ndead = 0;
            if (n) snprintf(status, sizeof(status), "%d results", n);
            else snprintf(status, sizeof(status), "no results");
            InvalidateRect(hwnd, NULL, FALSE);

            /* check every page in the background; empty ones disappear */
            for (i = 0; i < n; i++) launch(th_check, (int)wp, NULL, cards[i].url);
        }
        free(rp); return 0;
    }
    case WM_CHECK: {
        CheckPayload *c = (CheckPayload *)lp;
        if (c && (int)wp == (int)gen && c->verdict == 0 && ndead < 16) {
            snprintf(dead[ndead], sizeof(dead[0]), "%s", c->url);
            ndead++;
            apply_dead();
        }
        free(c); return 0;
    }
    case WM_QA: {
        QAPayload *q = (QAPayload *)lp;
        if (q && (int)wp == (int)gen) {
            qaOk = q->ok; snprintf(qaTitle, sizeof(qaTitle), "%s", q->title); snprintf(qaText, sizeof(qaText), "%s", q->text);
            qaBusy = 0; InvalidateRect(hwnd, NULL, FALSE);
        }
        free(q); return 0;
    }
    case WM_TOPIC: {
        QAPayload *q = (QAPayload *)lp;
        if (q) {
            topicOk = q->ok;
            snprintf(topicTitle, sizeof(topicTitle), "%s", q->title);
            snprintf(topicText, sizeof(topicText), "%s", q->text);
        }
        topicBusy = 0;
        free(q);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_PAGE: {
        char *t = (char *)lp;
        if (t && (int)wp == (int)pageGen) {
            size_t tlen = strlen(t);
            if (tlen > 0 && tlen < 150 && cur >= 0 && cur < ncards) {
                /* the page has no real information: remove it and go back */
                remove_card(cur);
                cur = -1; view = V_RES;
                free(t);
                snprintf(status, sizeof(status), "Removed a page with no information  -  %d results", ncards);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (tlen == 0 && cur >= 0 && cur < ncards) {
                /* site blocks reading: show what the search already told us */
                size_t cap = strlen(cards[cur].snippet) + 400;
                char *n = (char *)malloc(cap);
                if (n) {
                    snprintf(n, cap, "%s\n- This site doesn't allow reading inside the app.\n- Use \"Open in browser\" at the top right for the full page.\n",
                             cards[cur].snippet);
                    free(t); t = n;
                }
            }
            free(readerText); readerText = t; readerLoading = 0; parse_blocks(); InvalidateRect(hwnd, NULL, FALSE);
        } else free(t);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        RECT rc; int *pos, maxs; GetClientRect(hwnd, &rc);
        pos = (view == V_READER) ? &readerY : &scrollY; maxs = max_scroll(rc.bottom); if (maxs < 0) maxs = 0;
        *pos -= GET_WHEEL_DELTA_WPARAM(wp) / 2; if (*pos < 0) *pos = 0; if (*pos > maxs) *pos = maxs;
        InvalidateRect(hwnd, NULL, FALSE); return 0;
    }
    case WM_MOUSEMOVE: {
        RECT rc; POINT pt; int hc, hch; GetClientRect(hwnd, &rc);
        pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        hc = card_at(pt.x, pt.y, rc.right); hch = chip_at(pt);
        { int ht = (!searched && PtInRect(&topicR, pt)); if (hc != hoverCard || hch != hoverChip || ht != hoverTopic) { hoverCard = hc; hoverChip = hch; hoverTopic = ht; InvalidateRect(hwnd, NULL, FALSE); } }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RECT rc; POINT pt; int i; GetClientRect(hwnd, &rc);
        pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        if (PtInRect(&homeBtnR, pt)) { go_home(); return 0; }
        if (PtInRect(&btnR, pt)) { search_from_edit(); return 0; }
        if (!searched && PtInRect(&topicR, pt) && topicQ[0]) { search_text(topicQ); return 0; }
        i = chip_at(pt);
        if (i >= 10) { search_text(hist[i - 10]); return 0; }
        if (i >= 0) { search_text(TRENDING[i]); return 0; }
        if (!searched) return 0;
        if (view == V_READER) {
            if (PtInRect(&backR, pt)) go_back();
            else if (PtInRect(&openR, pt) && cur >= 0) ShellExecuteA(NULL, "open", cards[cur].url, NULL, NULL, SW_SHOWNORMAL);
            return 0;
        }
        i = card_at(pt.x, pt.y, rc.right);
        if (i >= 0) open_reader(i);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt; RECT rc;
        GetCursorPos(&pt); ScreenToClient(hwnd, &pt); GetClientRect(hwnd, &rc);
        if (LOWORD(lp) == HTCLIENT && hit_any(pt, rc.right)) { SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE; }
        break;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT rc; HDC m; HBITMAP bm; HGDIOBJ o;
        GetClientRect(hwnd, &rc);
        m = CreateCompatibleDC(dc); bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        o = SelectObject(m, bm); paint(m, rc.right, rc.bottom);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, m, 0, 0, SRCCOPY);
        SelectObject(m, o); DeleteObject(bm); DeleteDC(m); EndPaint(hwnd, &ps); return 0;
    }
    case WM_DESTROY: KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc; MSG msg; GpStartupInput si; ULONG_PTR token = 0;
    (void)prev; (void)cmd; (void)show;
    memset(&si, 0, sizeof(si)); si.GdiplusVersion = 1; GdiplusStartup(&token, &si, NULL);
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc; wc.hInstance = inst; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "MiniSearchWnd"; RegisterClassA(&wc);
    CreateWindowExA(0, "MiniSearchWnd", "Mini Search", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                    CW_USEDEFAULT, CW_USEDEFAULT, 1100, 740, NULL, NULL, inst, NULL);
    while (GetMessageA(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    GdiplusShutdown(token); return (int)msg.wParam;
}