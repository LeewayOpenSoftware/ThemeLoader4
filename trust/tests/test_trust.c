/*
 * PROJECT:     Theme Loader 4 - trust tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Unit tests of the user-writable test (T-DEF-1/2/5) and the privileged-process classifier (T-DEF-3/4):
 *              one case per clause, the descriptors the LiberNT image ships, and every truncation of a descriptor
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stdio.h>
#include <string.h>
#include <tl4/trust/trust.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(expr, want) do { int v_ = (int)(expr); checks++; if (v_ != (int)(want)) { failures++; \
    printf("FAIL %s:%d: %s -> %d, wanted %d\n", __FILE__, __LINE__, #expr, v_, (int)(want)); } } while (0)

/* ---- SID and descriptor builders ------------------------------------------------------------------------------- */

typedef struct { uint8_t b[68]; size_t n; } SID_T;

static SID_T
sid(uint8_t authority, uint32_t nsub, const uint32_t *sub)
{
    SID_T s;
    uint32_t i;

    memset(&s, 0, sizeof(s));
    s.b[0] = 1;
    s.b[1] = (uint8_t)nsub;
    s.b[7] = authority;
    for (i = 0; i < nsub; i++)
    {
        s.b[8 + 4 * i] = (uint8_t)sub[i];
        s.b[9 + 4 * i] = (uint8_t)(sub[i] >> 8);
        s.b[10 + 4 * i] = (uint8_t)(sub[i] >> 16);
        s.b[11 + 4 * i] = (uint8_t)(sub[i] >> 24);
    }
    s.n = 8u + 4u * nsub;
    return s;
}

static const uint32_t S18[] = { 18 }, S19[] = { 19 }, S20[] = { 20 }, S11[] = { 11 }, BA_[] = { 32, 544 },
                      BU_[] = { 32, 545 }, USER_[] = { 21, 1, 2, 3, 1001 }, TI_[] = { 80, 956008885u, 3418522649u,
                      1831038044u, 1853292631u, 2271478464u }, SVC_[] = { 80, 1, 2, 3, 4, 5 }, AC_[] = { 2, 1 },
                      RAP_[] = { 2, 2 }, ZERO_[] = { 0 }, CO_[] = { 0 };
#define SY sid(5, 1, S18)
#define LS sid(5, 1, S19)
#define NS sid(5, 1, S20)
#define AU sid(5, 1, S11)
#define BA sid(5, 2, BA_)
#define BU sid(5, 2, BU_)
#define USER sid(5, 5, USER_)
#define TI sid(5, 6, TI_)
#define SVC sid(5, 6, SVC_)
#define AC sid(15, 2, AC_)
#define RAP sid(15, 2, RAP_)
#define WD sid(1, 1, ZERO_)
#define CO sid(3, 1, CO_)

typedef struct { uint8_t type, flags; uint32_t mask; SID_T who; } ACE_T;

#define OI 0x01
#define CI 0x02
#define IO 0x08

static uint8_t sdbuf[2048];

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

/* owner, then the DACL; dacl: 0 = no DACL flag, 1 = present, 2 = present but NULL (offset 0) */
static size_t
build(const SID_T *owner, int dacl, const ACE_T *aces, uint32_t n)
{
    size_t pos = 20, acl, i;

    memset(sdbuf, 0, sizeof(sdbuf));
    sdbuf[0] = 1;
    put16(sdbuf + 2, 0x8000u | (dacl ? 0x0004u : 0));
    if (owner)
    {
        put32(sdbuf + 4, (uint32_t)pos);
        memcpy(sdbuf + pos, owner->b, owner->n);
        pos += owner->n;
    }
    if (dacl != 1)
        return pos;
    put32(sdbuf + 16, (uint32_t)pos);
    acl = pos;
    sdbuf[acl] = 2;
    pos += 8;
    for (i = 0; i < n; i++)
    {
        size_t sz = 8 + aces[i].who.n;

        sdbuf[pos] = aces[i].type;
        sdbuf[pos + 1] = aces[i].flags;
        put16(sdbuf + pos + 2, (uint32_t)sz);
        put32(sdbuf + pos + 4, aces[i].mask);
        memcpy(sdbuf + pos + 8, aces[i].who.b, aces[i].who.n);
        pos += sz;
    }
    put16(sdbuf + acl + 2, (uint32_t)(pos - acl));
    put16(sdbuf + acl + 4, n);
    return pos;
}

#define N(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))
#define FA 0x001F01FFu
#define READ_ 0x001200A9u
#define MODIFY 0x001301BFu
#define T_FILE TL4_TRUST_FILE
#define T_DIR TL4_TRUST_DIR
#define T_ABOVE TL4_TRUST_DIR_ABOVE_ROOT
#define TI_OK TL4_TRUST_COMPONENT_OWNER_TRUSTED

static tl4_trust_reason w(size_t n, enum tl4_trust_object o, unsigned f) { return tl4_trust_sd_writable(sdbuf, n, o, f); }

static void
test_writable(void)
{
    size_t n, k;
    SID_T ba = BA, user = USER, ti = TI, svc = SVC;

    CHECK_EQ(tl4_trust_sd_writable(NULL, 0, T_FILE, 0), TL4_TRUST_W_UNREADABLE);

    /* T-STORE-5 store descriptor: safe for every object kind */
    {
        ACE_T a[] = { { 0, OI | CI, FA, SY }, { 0, OI | CI, FA, BA }, { 0, OI | CI, READ_, AU } };
        n = build(&ba, 1, a, N(a));
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_SAFE);
        CHECK_EQ(w(n, T_DIR, 0), TL4_TRUST_SAFE);
        CHECK_EQ(w(n, T_ABOVE, 0), TL4_TRUST_SAFE);
        /* every truncation of it is refused */
        for (k = 0; k < n; k++)
            CHECK(w(k, T_FILE, 0) != TL4_TRUST_SAFE);
    }
    /* owner rules */
    {
        ACE_T a[] = { { 0, 0, FA, SY } };
        n = build(&user, 1, a, N(a));
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_OWNER);
        CHECK_EQ(w(n, T_ABOVE, 0), TL4_TRUST_W_OWNER);
        n = build(NULL, 1, a, N(a));
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_OWNER);
        n = build(&svc, 1, a, N(a));                       /* another service SID is never trusted (T-DEF-5) */
        CHECK_EQ(w(n, T_FILE, TI_OK), TL4_TRUST_W_OWNER);
        n = build(&ti, 1, a, N(a));
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_OWNER);
        CHECK_EQ(w(n, T_FILE, TI_OK), TL4_TRUST_SAFE);
    }
    /* no DACL, NULL DACL */
    n = build(&ba, 0, NULL, 0);
    CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_NO_DACL);
    n = build(&ba, 2, NULL, 0);
    CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_NO_DACL);
    /* an empty DACL grants nothing */
    n = build(&ba, 1, NULL, 0);
    CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_SAFE);

    /* write-class rights, one at a time, for Users */
    {
        static const uint32_t file_rights[] = { 0x2, 0x4, 0x10, 0x100, 0x10000, 0x40000, 0x80000, 0x2000000,
                                                0x10000000, 0x40000000 };
        uint32_t i;

        for (i = 0; i < N(file_rights); i++)
        {
            ACE_T a[] = { { 0, 0, file_rights[i], BU } };
            n = build(&ba, 1, a, N(a));
            CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_GRANT);
            CHECK_EQ(w(n, T_DIR, 0), TL4_TRUST_W_GRANT);
        }
        {
            ACE_T a[] = { { 0, 0, 0x40, BU } };                /* FILE_DELETE_CHILD: a directory right */
            n = build(&ba, 1, a, N(a));
            CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_SAFE);
            CHECK_EQ(w(n, T_DIR, 0), TL4_TRUST_W_GRANT);
            CHECK_EQ(w(n, T_ABOVE, 0), TL4_TRUST_W_GRANT);
        }
        {
            ACE_T a[] = { { 0, 0, 0x2 | 0x4 | 0x10 | 0x100 | 0x40000000, BU } };  /* add file/subdir, generic write */
            n = build(&ba, 1, a, N(a));
            CHECK_EQ(w(n, T_DIR, 0), TL4_TRUST_W_GRANT);
            CHECK_EQ(w(n, T_ABOVE, 0), TL4_TRUST_SAFE);       /* T-DEF-2 narrow set above the store root */
        }
        {
            ACE_T a[] = { { 0, 0, READ_, BU }, { 0, 0, READ_, WD } };
            n = build(&ba, 1, a, N(a));
            CHECK_EQ(w(n, T_DIR, 0), TL4_TRUST_SAFE);
        }
    }
    /* inherit-only entries do not apply to the object; deny entries never make it safe */
    {
        ACE_T a[] = { { 0, OI | CI | IO, 0x10000000, CO } };
        n = build(&ba, 1, a, N(a));
        CHECK_EQ(w(n, T_DIR, 0), TL4_TRUST_SAFE);
    }
    {
        ACE_T a[] = { { 1, 0, FA, WD }, { 0, 0, 0x2, user } };
        n = build(&ba, 1, a, N(a));
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_GRANT);
    }
    /* unknown ACE types count as grants unless inherit-only; deny-like types are ignored */
    {
        ACE_T a[] = { { 9, 0, 0x1, BU } };
        n = build(&ba, 1, a, N(a));
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_ACE_UNKNOWN);
        a[0].flags = IO;
        n = build(&ba, 1, a, N(a));
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_SAFE);
        a[0].type = 10;
        a[0].flags = 0;
        n = build(&ba, 1, a, N(a));
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_SAFE);
    }
    /* malformed */
    {
        ACE_T a[] = { { 0, 0, READ_, BU } };
        n = build(&ba, 1, a, N(a));
        sdbuf[2] = 0; sdbuf[3] = 0;                          /* not self-relative */
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_UNREADABLE);
        n = build(&ba, 1, a, N(a));
        sdbuf[0] = 2;                                        /* revision */
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_UNREADABLE);
        n = build(&ba, 1, a, N(a));
        sdbuf[21] = 16;                                      /* owner with 16 sub-authorities */
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_UNREADABLE);
        n = build(&ba, 1, a, N(a));
        put16(sdbuf + 20 + ba.n + 4, 2);                     /* two ACEs claimed, one present */
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_UNREADABLE);
        n = build(&ba, 1, a, N(a));
        put16(sdbuf + 20 + ba.n + 8 + 2, 4);                 /* ACE too short for its SID */
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_UNREADABLE);
        n = build(&ba, 1, a, N(a));
        put16(sdbuf + 20 + ba.n + 2, 0xFFFF);                /* ACL larger than the descriptor */
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_UNREADABLE);
        n = build(&ba, 1, a, N(a));
        put32(sdbuf + 4, 0xFFFFFFF0u);                       /* owner offset outside */
        CHECK_EQ(w(n, T_FILE, 0), TL4_TRUST_W_UNREADABLE);
    }

    /* the descriptors the LiberNT image applies (boot/bootdata/filesecurity.txt) */
    {
        /* COMPONENTDIR: O:TI D:PAI(A;;FA;;;TI)(A;CIIO;GA;;;TI)(A;;0x1301bf;;;SY)(A;OICIIO;GA;;;SY)(A;;0x1301bf;;;BA)
         * (A;OICIIO;GA;;;BA)(A;;0x1200a9;;;BU)(A;OICIIO;GXGR;;;BU)(A;OICIIO;GA;;;CO)(A;;0x1200a9;;;AC)
         * (A;OICIIO;GXGR;;;AC)(A;;0x1200a9;;;RAP)(A;OICIIO;GXGR;;;RAP) */
        ACE_T a[] = { { 0, 0, FA, TI }, { 0, CI | IO, 0x10000000, TI }, { 0, 0, MODIFY, SY },
                      { 0, OI | CI | IO, 0x10000000, SY }, { 0, 0, MODIFY, BA }, { 0, OI | CI | IO, 0x10000000, BA },
                      { 0, 0, READ_, BU }, { 0, OI | CI | IO, 0xA0000000u, BU }, { 0, OI | CI | IO, 0x10000000, CO },
                      { 0, 0, READ_, AC }, { 0, OI | CI | IO, 0xA0000000u, AC }, { 0, 0, READ_, RAP },
                      { 0, OI | CI | IO, 0xA0000000u, RAP } };
        n = build(&ti, 1, a, N(a));
        CHECK_EQ(w(n, T_DIR, TI_OK), TL4_TRUST_SAFE);
        CHECK_EQ(w(n, T_ABOVE, TI_OK), TL4_TRUST_SAFE);
        CHECK_EQ(w(n, T_DIR, 0), TL4_TRUST_W_OWNER);       /* T-DEF-5 not shown: TI is ordinary */
    }
    {
        /* COMPONENTFILE: O:TI D:P(A;;FA;;;TI)(A;;0x1200a9;;;BA)(A;;0x1200a9;;;SY)(A;;0x1200a9;;;BU)... */
        ACE_T a[] = { { 0, 0, FA, TI }, { 0, 0, READ_, BA }, { 0, 0, READ_, SY }, { 0, 0, READ_, BU },
                      { 0, 0, READ_, AC }, { 0, 0, READ_, RAP } };
        n = build(&ti, 1, a, N(a));
        CHECK_EQ(w(n, T_FILE, TI_OK), TL4_TRUST_SAFE);
    }
    {
        /* volume root: O:TI D:PAI(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)(A;OICI;0x1200a9;;;BU)(A;OICIIO;SDGXGWGR;;;AU)
         * (A;;LC;;;AU) -- LC = 0x4, FILE_ADD_SUBDIRECTORY */
        ACE_T a[] = { { 0, OI | CI, FA, BA }, { 0, OI | CI, FA, SY }, { 0, OI | CI, READ_, BU },
                      { 0, OI | CI | IO, 0xE0010000u, AU }, { 0, 0, 0x4, AU } };
        n = build(&ti, 1, a, N(a));
        CHECK_EQ(w(n, T_ABOVE, TI_OK), TL4_TRUST_SAFE);     /* above the store root: only the narrow set counts */
        CHECK_EQ(w(n, T_DIR, TI_OK), TL4_TRUST_W_GRANT);    /* as a store directory it would be writable */
    }
    CHECK(tl4_trust_write_mask((enum tl4_trust_object)7) == 0xFFFFFFFFu);
}

/* ---- classifier ------------------------------------------------------------------------------------------------ */

static const uint16_t DESK_DEFAULT[] = { 'D', 'e', 'f', 'a', 'u', 'l', 't' };
static const uint16_t DESK_WINLOGON[] = { 'w', 'i', 'n', 'l', 'o', 'g', 'o', 'n' };
static const uint16_t DESK_SAVER[] = { 'S', 'c', 'r', 'e', 'e', 'n', '-', 's', 'a', 'v', 'e', 'r' };
static const uint16_t WINSTA0[] = { 'W', 'i', 'n', 'S', 't', 'a', '0' };
static const uint16_t WINSTA_SVC[] = { 'S', 'e', 'r', 'v', 'i', 'c', 'e', '-', '0', 'x', '0' };

static SID_T s_user, s_users, s_admins, s_bad;
static tl4_trust_sid_ref groups[2];
static uint64_t privs[4];

static tl4_trust_facts
ordinary(void)
{
    tl4_trust_facts f;

    s_user = USER;
    s_users = BU;
    s_admins = BA;
    memset(&f, 0, sizeof(f));
    f.complete = 1;
    f.user.sid = s_user.b;
    f.user.size = s_user.n;
    groups[0].sid = s_users.b; groups[0].size = s_users.n; groups[0].attributes = TL4_SE_GROUP_ENABLED;
    groups[1].sid = s_admins.b; groups[1].size = s_admins.n; groups[1].attributes = TL4_SE_GROUP_USE_FOR_DENY_ONLY;
    f.groups = groups;
    f.ngroups = 2;
    f.elevation_type = 3;
    f.integrity_rid = 0x2000;
    f.session_id = 1;
    privs[0] = 23; privs[1] = 19;                            /* SeChangeNotify, SeShutdown */
    f.privileges = privs;
    f.nprivileges = 2;
    f.desktop = DESK_DEFAULT; f.desktop_chars = N(DESK_DEFAULT);
    f.winstation = WINSTA0; f.winstation_chars = N(WINSTA0);
    return f;
}

static void
test_classify(void)
{
    tl4_trust_facts f;
    static const uint32_t listed[] = { TL4_SE_TCB, TL4_SE_DEBUG, TL4_SE_IMPERSONATE, TL4_SE_ASSIGN_PRIMARY,
                                       TL4_SE_CREATE_TOKEN, TL4_SE_LOAD_DRIVER, TL4_SE_BACKUP, TL4_SE_RESTORE,
                                       TL4_SE_TAKE_OWNERSHIP, TL4_SE_SECURITY };
    uint32_t i;
    SID_T sy = SY, ls = LS, ns = NS;

    f = ordinary();
    CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_ORDINARY);

    /* T-DEF-4: missing facts */
    CHECK_EQ(tl4_trust_classify(NULL), TL4_TRUST_P_INCOMPLETE);
    f = ordinary(); f.complete = 0; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INCOMPLETE);
    f = ordinary(); f.ngroups = TL4_TRUST_MAX_GROUPS + 1; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INCOMPLETE);
    f = ordinary(); f.nprivileges = TL4_TRUST_MAX_PRIVILEGES + 1; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INCOMPLETE);
    f = ordinary(); f.groups = NULL; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INCOMPLETE);
    f = ordinary(); f.desktop_chars = 0; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INCOMPLETE);
    f = ordinary(); f.winstation = NULL; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INCOMPLETE);
    f = ordinary(); f.desktop_chars = TL4_TRUST_MAX_NAME + 1; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INCOMPLETE);
    f = ordinary(); f.user.size = 7; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INCOMPLETE);
    f = ordinary(); s_bad = USER; s_bad.b[1] = 16; groups[0].sid = s_bad.b; groups[0].size = sizeof(s_bad.b);
    CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INCOMPLETE);

    /* one case per T-DEF-3 clause */
    f = ordinary(); f.user.sid = sy.b; f.user.size = sy.n; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_SERVICE_USER);
    f = ordinary(); f.user.sid = ls.b; f.user.size = ls.n; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_SERVICE_USER);
    f = ordinary(); f.user.sid = ns.b; f.user.size = ns.n; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_SERVICE_USER);
    f = ordinary(); groups[1].attributes = TL4_SE_GROUP_ENABLED; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_ADMIN_ENABLED);
    f = ordinary(); groups[1].attributes = TL4_SE_GROUP_ENABLED | TL4_SE_GROUP_USE_FOR_DENY_ONLY;
    CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_ORDINARY);
    f = ordinary(); groups[1].attributes = 0; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_ORDINARY);
    f = ordinary(); f.elevation_type = 2; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_ELEVATED);
    f = ordinary(); f.is_elevated = 1; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_ELEVATED);
    f = ordinary(); f.elevation_type = 1; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_ORDINARY);
    f = ordinary(); f.integrity_rid = 0x3000; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INTEGRITY);
    f = ordinary(); f.integrity_rid = 0x4000; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_INTEGRITY);
    f = ordinary(); f.integrity_rid = 0x2FFF; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_ORDINARY);
    f = ordinary(); f.started_by_scm = 1; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_SERVICE_START);
    f = ordinary(); f.session_id = 0; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_SESSION0);
    f = ordinary(); f.session_id = 0; f.interactive_in_session0 = 1; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_ORDINARY);
    for (i = 0; i < N(listed); i++)
    {
        f = ordinary(); privs[2] = listed[i]; f.nprivileges = 3;
        CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_PRIVILEGE);
    }
    f = ordinary(); privs[2] = ((uint64_t)1 << 32) | TL4_SE_DEBUG; f.nprivileges = 3;
    CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_ORDINARY);
    f = ordinary(); f.is_logon_process = 1; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_LOGON);
    f = ordinary(); f.desktop = DESK_WINLOGON; f.desktop_chars = N(DESK_WINLOGON); CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_LOGON);
    f = ordinary(); f.desktop = DESK_SAVER; f.desktop_chars = N(DESK_SAVER); CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_LOGON);
    f = ordinary(); f.winstation = WINSTA_SVC; f.winstation_chars = N(WINSTA_SVC); CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_LOGON);
    f = ordinary(); f.desktop_chars = 3; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_ORDINARY);     /* "Def" */
    f = ordinary(); f.winstation_chars = 6; CHECK_EQ(tl4_trust_classify(&f), TL4_TRUST_P_LOGON);  /* "WinSta" */
}

static void
test_sid_length(void)
{
    SID_T s = USER;

    CHECK(tl4_trust_sid_length(s.b, s.n) == s.n);
    CHECK(tl4_trust_sid_length(s.b, s.n - 1) == 0);
    CHECK(tl4_trust_sid_length(NULL, 64) == 0);
    s.b[0] = 2;
    CHECK(tl4_trust_sid_length(s.b, sizeof(s.b)) == 0);
}

int
main(void)
{
    test_sid_length();
    test_writable();
    test_classify();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
