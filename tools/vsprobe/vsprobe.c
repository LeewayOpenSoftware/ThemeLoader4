/*
 * PROJECT:     Theme Loader 4 - tools
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows reference probe: opens a .msstyles with the system's uxtheme.dll (no theme is applied, no
 *              setting changes) and records what GetTheme* returns for a list of (class, part, state, property, type)
 *              queries, as CSV, so the loader's model can be compared with the reference behaviour
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Non-destructive by construction: the style is opened with OpenThemeFile(global = FALSE) and OpenThemeDataFromFile,
 * which affect this process only; ApplyTheme (ordinal 4), SetWindowTheme and every registry or system-parameter call
 * are absent. The three undocumented entry points are resolved by ordinal: 2 OpenThemeFile, 3 CloseThemeFile,
 * 16 OpenThemeDataFromFile (SOURCE: LiberNT dll/win32/uxtheme/uxtheme.spec, derived from the Windows debug symbols;
 * the same ordinals in every Windows uxtheme.dll since XP); if one is missing the tool stops before any call.
 *
 * Usage: vsprobe <style.msstyles> <queries.csv> <out.csv>
 *   queries.csv: class,part,state,property,type   (UTF-8; `class` is "App::Class" or "Class"; type = TMT primitive
 *                200..211 as in the style's records; lines starting with # are skipped)
 *   out.csv:     windows build, uxtheme version and style size in a header, then one row per query:
 *                class,part,state,property,type,hresult,origin,value
 * Build (any of):  cl /W4 /O2 vsprobe.c uxtheme.lib version.lib
 *                  clang-cl /W4 /O2 vsprobe.c uxtheme.lib version.lib
 *                  x86_64-w64-mingw32-gcc -Wall -O2 -municode vsprobe.c -luxtheme -lversion -o vsprobe.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef HANDLE HTHEMEFILE;
typedef HRESULT (WINAPI *PFN_OpenThemeFile)(LPCWSTR file, LPCWSTR color, LPCWSTR size, HTHEMEFILE *out, BOOL global);
typedef HRESULT (WINAPI *PFN_CloseThemeFile)(HTHEMEFILE h);
typedef HTHEME (WINAPI *PFN_OpenThemeDataFromFile)(HTHEMEFILE h, HWND hwnd, LPCWSTR classlist, DWORD flags);
typedef LONG (WINAPI *PFN_RtlGetVersion)(PRTL_OSVERSIONINFOW);        /* NTSTATUS without winternl.h */

static void csv_escape(FILE *f, const wchar_t *s)
{
    fputc('"', f);
    for (; *s; s++)
    {
        if (*s == L'"') fputs("\"\"", f);
        else if (*s < 128) fputc((int)*s, f);
        else fprintf(f, "\\u%04x", (unsigned)*s);
    }
    fputc('"', f);
}

static void header(FILE *out, const wchar_t *style)
{
    RTL_OSVERSIONINFOW v; HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    PFN_RtlGetVersion rgv = nt ? (PFN_RtlGetVersion)(void *)GetProcAddress(nt, "RtlGetVersion") : 0;
    wchar_t path[MAX_PATH]; DWORD vsize, dummy; WIN32_FILE_ATTRIBUTE_DATA fa;
    memset(&v, 0, sizeof(v)); v.dwOSVersionInfoSize = sizeof(v);
    if (rgv) rgv(&v);
    fprintf(out, "# vsprobe 1.0; windows %lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    if (GetSystemDirectoryW(path, MAX_PATH) && wcscat_s(path, MAX_PATH, L"\\uxtheme.dll") == 0 && (vsize = GetFileVersionInfoSizeW(path, &dummy)) != 0)
    {
        void *buf = malloc(vsize); VS_FIXEDFILEINFO *ffi; UINT len;
        if (buf && GetFileVersionInfoW(path, 0, vsize, buf) && VerQueryValueW(buf, L"\\", (void **)&ffi, &len))
            fprintf(out, "; uxtheme %u.%u.%u.%u", HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS), HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
        free(buf);
    }
    if (GetFileAttributesExW(style, GetFileExInfoStandard, &fa)) fprintf(out, "; style bytes %lu", fa.nFileSizeLow);
    fputs("\n# class,part,state,property,type,hresult,origin,value\n", out);
}

static const char *origin_name(PROPERTYORIGIN o)
{
    switch (o) { case PO_STATE: return "state"; case PO_PART: return "part"; case PO_CLASS: return "class"; case PO_GLOBAL: return "global"; case PO_NOTFOUND: return "notfound"; default: return "?"; }
}

/* One query against an open HTHEME. The value column holds the decoded value, or nothing on failure. */
static void query(FILE *out, HTHEME h, int part, int state, int prop, int type)
{
    HRESULT hr = E_FAIL; PROPERTYORIGIN origin = PO_NOTFOUND;
    char value[4096]; value[0] = 0;
    GetThemePropertyOrigin(h, part, state, prop, &origin);
    switch (type)
    {
        case TMT_ENUM: case TMT_INT: case TMT_SIZE: { int v = 0; hr = type == TMT_ENUM ? GetThemeEnumValue(h, part, state, prop, &v) : type == TMT_INT ? GetThemeInt(h, part, state, prop, &v) : GetThemeMetric(h, NULL, part, state, prop, &v); if (SUCCEEDED(hr)) sprintf_s(value, sizeof(value), "%d", v); break; }
        case TMT_BOOL: { BOOL v = 0; hr = GetThemeBool(h, part, state, prop, &v); if (SUCCEEDED(hr)) sprintf_s(value, sizeof(value), "%d", v ? 1 : 0); break; }
        case TMT_COLOR: { COLORREF c = 0; hr = GetThemeColor(h, part, state, prop, &c); if (SUCCEEDED(hr)) sprintf_s(value, sizeof(value), "0x%08lX", (unsigned long)c); break; }
        case TMT_MARGINS: { MARGINS m = {0}; hr = GetThemeMargins(h, NULL, part, state, prop, NULL, &m); if (SUCCEEDED(hr)) sprintf_s(value, sizeof(value), "%d;%d;%d;%d", m.cxLeftWidth, m.cxRightWidth, m.cyTopHeight, m.cyBottomHeight); break; }
        case TMT_POSITION: { POINT p = {0}; hr = GetThemePosition(h, part, state, prop, &p); if (SUCCEEDED(hr)) sprintf_s(value, sizeof(value), "%ld;%ld", p.x, p.y); break; }
        case TMT_RECT: { RECT r = {0}; hr = GetThemeRect(h, part, state, prop, &r); if (SUCCEEDED(hr)) sprintf_s(value, sizeof(value), "%ld;%ld;%ld;%ld", r.left, r.top, r.right, r.bottom); break; }
        case TMT_INTLIST: { INTLIST l; int i; size_t n = 0; memset(&l, 0, sizeof(l)); hr = GetThemeIntList(h, part, state, prop, &l); if (SUCCEEDED(hr)) { n = (size_t)sprintf_s(value, sizeof(value), "%d:", l.iValueCount); for (i = 0; i < l.iValueCount && i < MAX_INTLIST_COUNT && n < sizeof(value) - 16; i++) n += (size_t)sprintf_s(value + n, sizeof(value) - n, "%d;", l.iValues[i]); } break; }
        case TMT_FONT: { LOGFONTW lf; memset(&lf, 0, sizeof(lf)); hr = GetThemeFont(h, NULL, part, state, prop, &lf); if (SUCCEEDED(hr)) sprintf_s(value, sizeof(value), "%ls;h=%ld;w=%ld;i=%d;u=%d;s=%d;q=%d", lf.lfFaceName, lf.lfHeight, lf.lfWeight, lf.lfItalic, lf.lfUnderline, lf.lfStrikeOut, lf.lfQuality); break; }
        case TMT_STRING: case TMT_FILENAME: { wchar_t w[1024]; w[0] = 0; hr = type == TMT_STRING ? GetThemeString(h, part, state, prop, w, 1024) : GetThemeFilename(h, part, state, prop, w, 1024); if (SUCCEEDED(hr)) { FILE *tmp = out; fprintf(tmp, "%d,%d,%d,%d,0x%08lX,%s,", part, state, prop, type, (unsigned long)hr, origin_name(origin)); csv_escape(tmp, w); fputc('\n', tmp); return; } break; }
        default: hr = E_INVALIDARG; break;
    }
    fprintf(out, "%d,%d,%d,%d,0x%08lX,%s,%s\n", part, state, prop, type, (unsigned long)hr, origin_name(origin), value);
}

int wmain(int argc, wchar_t **argv)
{
    HMODULE ux; PFN_OpenThemeFile open_file; PFN_CloseThemeFile close_file; PFN_OpenThemeDataFromFile open_data;
    HTHEMEFILE tf = NULL; HRESULT hr; FILE *in, *out; char line[2048];
    wchar_t cur_class[256] = L""; HTHEME h = NULL; unsigned rows = 0, failed_open = 0;

    if (argc != 4) { fputs("usage: vsprobe <style.msstyles> <queries.csv> <out.csv>\n", stderr); return 2; }
    ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) { fputs("uxtheme.dll not found\n", stderr); return 3; }
    open_file = (PFN_OpenThemeFile)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(2));
    close_file = (PFN_CloseThemeFile)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(3));
    open_data = (PFN_OpenThemeDataFromFile)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(16));
    if (!open_file || !close_file || !open_data) { fputs("uxtheme ordinals 2/3/16 missing: this build is not probed\n", stderr); return 3; }

    hr = open_file(argv[1], NULL, NULL, &tf, FALSE);                      /* this process only: nothing is applied */
    if (FAILED(hr) || !tf) { fprintf(stderr, "OpenThemeFile failed: 0x%08lX\n", (unsigned long)hr); return 4; }
    if (_wfopen_s(&in, argv[2], L"rt, ccs=UTF-8") != 0 || !in) { fputs("cannot read the query list\n", stderr); close_file(tf); return 5; }
    if (_wfopen_s(&out, argv[3], L"wt") != 0 || !out) { fputs("cannot write the output\n", stderr); fclose(in); close_file(tf); return 5; }
    header(out, argv[1]);

    while (fgets(line, sizeof(line), in))
    {
        char cls[512]; int part, state, prop, type; wchar_t wcls[256];
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf_s(line, "%511[^,],%d,%d,%d,%d", cls, (unsigned)sizeof(cls), &part, &state, &prop, &type) != 5) continue;
        if (!MultiByteToWideChar(CP_UTF8, 0, cls, -1, wcls, 256)) continue;
        if (wcscmp(wcls, cur_class) != 0)
        {
            if (h) CloseThemeData(h);
            h = open_data(tf, NULL, wcls, 0);
            wcscpy_s(cur_class, 256, wcls);
            if (!h) failed_open++;
        }
        csv_escape(out, wcls); fputc(',', out);
        if (!h) { fprintf(out, "%d,%d,%d,%d,0x%08lX,noclass,\n", part, state, prop, type, (unsigned long)E_HANDLE); continue; }
        query(out, h, part, state, prop, type);
        rows++;
    }
    if (h) CloseThemeData(h);
    fclose(in); fclose(out);
    close_file(tf);
    FreeLibrary(ux);
    fprintf(stderr, "vsprobe: %u rows, %u classes not opened\n", rows, failed_open);
    return 0;
}
