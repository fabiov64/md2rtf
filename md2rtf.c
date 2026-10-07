/*
 * md2rtf.c — Markdown → RTF converter for macOS
 *
 * Usage: md2rtf input.md
 *        Output is written to the same directory as the input file,
 *        with the .md extension replaced by .rtf.
 *
 * Supported elements:
 *   block  : ATX headings (#…######), setext headings (=== / ---),
 *             paragraphs, fenced code blocks (``` / ~~~ / 4 spaces / tab),
 *             blockquotes (>), horizontal rules, unordered and ordered lists,
 *             GFM tables
 *   inline : bold (** and __), italic (* and _),
 *             bold+italic (***), strikethrough (~~),
 *             inline code (`/``), links ([label](url)),
 *             images (![alt](url)), backslash escape (\)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ── Dynamic buffer ──────────────────────────────────────────────────────── */
typedef struct { char *d; size_t len, cap; } Buf;

static void buf_init(Buf *b) {
    b->cap = 65536;
    b->d   = malloc(b->cap);
    b->len = 0;
    b->d[0] = '\0';
}

static void buf_grow(Buf *b, size_t n) {
    if (b->len + n + 1 < b->cap) return;
    while (b->len + n + 1 >= b->cap) b->cap *= 2;
    b->d = realloc(b->d, b->cap);
}

static void bS(Buf *b, const char *s) {
    size_t l = strlen(s);
    buf_grow(b, l);
    memcpy(b->d + b->len, s, l + 1);
    b->len += l;
}

static void bC(Buf *b, char c) {
    buf_grow(b, 1);
    b->d[b->len++] = c;
    b->d[b->len]   = '\0';
}

static void bF(Buf *b, const char *fmt, ...) {
    char t[4096];
    va_list a;
    va_start(a, fmt);
    vsnprintf(t, sizeof t, fmt, a);
    va_end(a);
    bS(b, t);
}

/* ── Escape special RTF characters (with UTF-8 → \uNNNN? decoding) ──────── */
static void rtfEsc(Buf *b, const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\') { bS(b, "\\\\"); i++; }
        else if (c == '{') { bS(b, "\\{"); i++; }
        else if (c == '}') { bS(b, "\\}"); i++; }
        else if (c < 0x80) { bC(b, (char)c); i++; }
        else {
            /* decode UTF-8 sequence → Unicode code point */
            unsigned long cp = 0;
            int bytes = 0;
            if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; bytes = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; bytes = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; bytes = 4; }
            if (bytes >= 2) {
                int ok = 1;
                for (int j = 1; j < bytes; j++) {
                    if (i+j >= n || ((unsigned char)s[i+j] & 0xC0) != 0x80) {
                        ok = 0; break;
                    }
                    cp = (cp << 6) | ((unsigned char)s[i+j] & 0x3F);
                }
                if (ok) {
                    /* RTF \u uses signed 16-bit integers (BMP) */
                    if (cp <= 32767) {
                        bF(b, "\\u%lu?", cp);
                    } else if (cp <= 65535) {
                        bF(b, "\\u%ld?", (long)cp - 65536L);
                    } else {
                        /* Supplementary plane: encode as surrogate pair for RTF */
                        unsigned long hi = 0xD800 + ((cp - 0x10000UL) >> 10);
                        unsigned long lo = 0xDC00 + ((cp - 0x10000UL) & 0x3FF);
                        bF(b, "\\u%ld?\\u%ld?",
                           (long)hi - 65536L, (long)lo - 65536L);
                    }
                    i += bytes;
                    continue;
                }
            }
            /* invalid byte: emit as numeric escape */
            bF(b, "\\u%d?", (int)(signed char)c);
            i++;
        }
    }
}

/* ── Inline Markdown → RTF (forward declaration) ────────────────────────── */
static void inlineMD(Buf *b, const char *s, size_t len);

static void inlineMD(Buf *b, const char *s, size_t len) {
    size_t i = 0;
    while (i < len) {

        /* *** bold + italic *** */
        if (i + 2 < len &&
            ((s[i]=='*' && s[i+1]=='*' && s[i+2]=='*') ||
             (s[i]=='_' && s[i+1]=='_' && s[i+2]=='_'))) {
            char m = s[i];
            size_t j = i + 3;
            while (j + 2 < len &&
                   !(s[j]==m && s[j+1]==m && s[j+2]==m)) j++;
            if (j + 2 < len) {
                bS(b, "{\\b\\i ");
                inlineMD(b, s+i+3, j-i-3);
                bS(b, "}");
                i = j + 3; continue;
            }
        }

        /* ** bold ** */
        if (i + 1 < len &&
            ((s[i]=='*' && s[i+1]=='*') ||
             (s[i]=='_' && s[i+1]=='_'))) {
            char m = s[i];
            size_t j = i + 2;
            while (j + 1 < len && !(s[j]==m && s[j+1]==m)) j++;
            if (j + 1 < len) {
                bS(b, "{\\b ");
                inlineMD(b, s+i+2, j-i-2);
                bS(b, "}");
                i = j + 2; continue;
            }
        }

        /* * italic * — skip internal ** pairs */
        if (s[i] == '*' && (i+1 >= len || s[i+1] != '*')) {
            size_t j = i + 1;
            while (j < len) {
                if (s[j]=='*' && j+1<len && s[j+1]=='*') { j+=2; continue; }
                if (s[j]=='*') break;
                j++;
            }
            if (j < len && j > i + 1) {
                bS(b, "{\\i ");
                inlineMD(b, s+i+1, j-i-1);
                bS(b, "}");
                i = j + 1; continue;
            }
        }

        /* _ italic _ (not inside words) */
        if (s[i] == '_' &&
            (i == 0 || !isalnum((unsigned char)s[i-1])) &&
            (i+1 >= len || s[i+1] != '_')) {
            size_t j = i + 1;
            while (j < len &&
                   !(s[j]=='_' &&
                     (j+1 >= len || !isalnum((unsigned char)s[j+1])))) j++;
            if (j < len && j > i + 1) {
                bS(b, "{\\i ");
                inlineMD(b, s+i+1, j-i-1);
                bS(b, "}");
                i = j + 1; continue;
            }
        }

        /* ~~ strikethrough ~~ */
        if (i + 1 < len && s[i]=='~' && s[i+1]=='~') {
            size_t j = i + 2;
            while (j + 1 < len && !(s[j]=='~' && s[j+1]=='~')) j++;
            if (j + 1 < len) {
                bS(b, "{\\strike ");
                inlineMD(b, s+i+2, j-i-2);
                bS(b, "}");
                i = j + 2; continue;
            }
        }

        /* ` or `` inline code ` / `` */
        if (s[i] == '`') {
            int ticks = (i+1 < len && s[i+1]=='`') ? 2 : 1;
            size_t j = i + ticks;
            while (j + (size_t)(ticks-1) < len) {
                if (s[j] == '`') {
                    int ok = 1;
                    for (int t = 1; t < ticks; t++)
                        if (j+t >= len || s[j+t] != '`') { ok=0; break; }
                    if (ok) break;
                }
                j++;
            }
            if (j + (size_t)(ticks-1) < len) {
                bS(b, "{\\f2\\fs20\\cf4 ");
                rtfEsc(b, s+i+ticks, j-i-ticks);
                bS(b, "}");
                i = j + ticks; continue;
            }
        }

        /* ![alt](url) image — render alt text in italics */
        if (s[i]=='!' && i+1<len && s[i+1]=='[') {
            size_t j = i + 2;
            while (j < len && s[j] != ']') j++;
            if (j < len && j+1 < len && s[j+1]=='(') {
                size_t k = j + 2;
                while (k < len && s[k] != ')') k++;
                if (k < len) {
                    bS(b, "{\\i [Image: ");
                    rtfEsc(b, s+i+2, j-i-2);
                    bS(b, "]}");
                    i = k + 1; continue;
                }
            }
        }

        /* [label](url) hyperlink */
        if (s[i] == '[') {
            size_t j = i + 1;
            while (j < len && s[j] != ']') j++;
            if (j < len && j+1 < len && s[j+1]=='(') {
                size_t k = j + 2;
                while (k < len && s[k] != ')') k++;
                if (k < len) {
                    bS(b, "{\\cf3\\ul ");
                    inlineMD(b, s+i+1, j-i-1);
                    bS(b, "}");
                    i = k + 1; continue;
                }
            }
        }

        /* \ backslash escape — forward the full UTF-8 character that follows */
        if (s[i]=='\\' && i+1 < len) {
            unsigned char nc = (unsigned char)s[i+1];
            int nb = 1;
            if      ((nc & 0xE0)==0xC0) nb=2;
            else if ((nc & 0xF0)==0xE0) nb=3;
            else if ((nc & 0xF8)==0xF0) nb=4;
            if (i+1+(size_t)nb > len) nb = (int)(len-i-1);
            rtfEsc(b, s+i+1, (size_t)nb);
            i += 1 + nb; continue;
        }

        /* plain character (ASCII or start of a UTF-8 multi-byte sequence) */
        {
            unsigned char c0 = (unsigned char)s[i];
            int nb = 1;
            if      ((c0 & 0xE0)==0xC0) nb=2;
            else if ((c0 & 0xF0)==0xE0) nb=3;
            else if ((c0 & 0xF8)==0xF0) nb=4;
            if (i+(size_t)nb > len) nb = (int)(len-i);
            rtfEsc(b, s+i, (size_t)nb);
            i += nb;
        }
    }
}

/* ── Helper functions ────────────────────────────────────────────────────── */
static int isBlank(const char *s) {
    while (*s)
        if (!isspace((unsigned char)*s++)) return 0;
    return 1;
}

static int isHR(const char *s, int n) {
    if (n < 3) return 0;
    char c = s[0];
    if (c!='-' && c!='*' && c!='_') return 0;
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        if (s[i]!=c && s[i]!=' ' && s[i]!='\t') return 0;
        if (s[i]==c) cnt++;
    }
    return cnt >= 3;
}

/* Returns 1 if every character in the string equals ch */
static int allChar(const char *s, int n, char ch) {
    for (int i = 0; i < n; i++)
        if (s[i] != ch) return 0;
    return n > 0;
}

/* Returns 1 if the line is a GFM table separator row (|---|---:|:---:|) */
static int isTableSep(const char *s, int n) {
    int hasDash = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c=='-') { hasDash=1; continue; }
        if (c=='|' || c==':' || c==' ' || c=='\t') continue;
        return 0;
    }
    return hasDash;
}

/* ── RTF preamble ────────────────────────────────────────────────────────── */
static const char *PREAMBLE =
    "{\\rtf1\\ansi\\ansicpg1252\\deff0\n"
    /* font table */
    "{\\fonttbl\n"
    "{\\f0\\froman\\fcharset0 Times New Roman;}\n"
    "{\\f1\\fswiss\\fcharset0 Helvetica;}\n"
    "{\\f2\\fmodern\\fcharset0 Courier New;}\n"
    "}\n"
    /* colour table */
    "{\\colortbl;\n"
    "\\red0\\green0\\blue0;\n"        /* 1 – black (body text)     */
    "\\red80\\green80\\blue80;\n"     /* 2 – grey  (blockquotes)   */
    "\\red0\\green0\\blue180;\n"      /* 3 – blue  (links)         */
    "\\red150\\green30\\blue30;\n"    /* 4 – red   (code)          */
    "\\red200\\green200\\blue200;\n"  /* 5 – light grey (hr)       */
    "}\n"
    /* page setup: A4 portrait, 2.5 cm margins */
    "\\paperw11907\\paperh16840\n"
    "\\margl1417\\margr1417\\margt1134\\margb1134\n"
    "\\widowctrl\\hyphauto\n"
    "\\f0\\fs24\\cf1\n";

/* ── main ────────────────────────────────────────────────────────────────── */
/* Derive the RTF output path from the input path:
   replace the .md extension (case-insensitive) with .rtf,
   or append .rtf if the extension is not .md */
static void makeRtfName(const char *src, char *dst, size_t dstsz) {
    size_t len = strlen(src);
    if (len >= 3 &&
        (src[len-3]=='.' ) &&
        (src[len-2]=='m' || src[len-2]=='M') &&
        (src[len-1]=='d' || src[len-1]=='D')) {
        size_t base = len - 3;
        if (base + 4 + 1 > dstsz) base = dstsz - 5;
        memcpy(dst, src, base);
        memcpy(dst + base, ".rtf", 5);
    } else {
        snprintf(dst, dstsz, "%s.rtf", src);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.md\n", argv[0]);
        return 1;
    }

    /* --- read source file --- */
    char outName[4096];
    makeRtfName(argv[1], outName, sizeof outName);

    FILE *fin = fopen(argv[1], "r");
    if (!fin) { perror(argv[1]); return 1; }
    fseek(fin, 0, SEEK_END);
    long fsz = ftell(fin);
    rewind(fin);
    char *raw = malloc((size_t)fsz + 2);
    size_t nr  = fread(raw, 1, (size_t)fsz, fin);
    fclose(fin);
    raw[nr]   = '\n';  /* ensure the last line is terminated */
    raw[nr+1] = '\0';

    /* --- split into lines --- */
    int cap = 512, n = 0;
    char **L = malloc((size_t)cap * sizeof *L);
    for (char *p = raw; *p; ) {
        char *nl = strchr(p, '\n');
        size_t ll = nl ? (size_t)(nl - p) : strlen(p);
        if (n == cap) { cap *= 2; L = realloc(L, (size_t)cap * sizeof *L); }
        L[n] = malloc(ll + 1);
        memcpy(L[n], p, ll);
        /* strip CR (Windows line endings) */
        if (ll > 0 && L[n][ll-1] == '\r') ll--;
        L[n][ll] = '\0';
        n++;
        p = nl ? nl + 1 : p + ll;
        if (!nl) break;
    }

    /* --- build RTF output --- */
    Buf out;
    buf_init(&out);
    bS(&out, PREAMBLE);

    int inCB   = 0;  /* inside a fenced code block */
    int inPara = 0;  /* inside an open paragraph   */
    int inList = 0;  /* 1=unordered, 2=ordered     */

#define CLOSE_PARA \
    do { if (inPara) { bS(&out, "\\par\n"); inPara = 0; } } while(0)
#define CLOSE_LIST \
    do { if (inList) { bS(&out, "\\pard\\f0\\fs24\\cf1\n"); inList = 0; } } while(0)
#define CLOSE_ALL \
    do { CLOSE_PARA; CLOSE_LIST; } while(0)

    for (int li = 0; li < n; li++) {
        char *ln = L[li];
        int   ll = (int)strlen(ln);

        /* ── fenced code block (backtick or tilde) ── */
        if (strncmp(ln, "```", 3)==0 || strncmp(ln, "~~~", 3)==0) {
            if (!inCB) {
                CLOSE_ALL;
                inCB = 1;
                bS(&out, "\\pard\\f2\\fs20\\cf4\\sb80\\sa0 ");
            } else {
                inCB = 0;
                bS(&out, "\\par\n\\pard\\f0\\fs24\\cf1\\sb0 ");
            }
            continue;
        }
        if (inCB) {
            rtfEsc(&out, ln, ll);
            bS(&out, "\\line\n");
            continue;
        }

        /* ── blank line ── */
        if (isBlank(ln)) { CLOSE_ALL; continue; }

        /* ── horizontal rule (only outside a paragraph) ── */
        if (!inPara && isHR(ln, ll)) {
            CLOSE_ALL;
            bS(&out,
               "\\pard\\brdrb\\brdrs\\brdrw10\\brsp20\\cf5 \\par\n"
               "\\pard\\f0\\fs24\\cf1\\sb0 ");
            continue;
        }

        /* ── ATX heading (# … ######) ── */
        if (ln[0] == '#') {
            int hl = 0;
            while (hl < ll && ln[hl] == '#') hl++;
            if (hl <= 6 && (ln[hl]==' ' || ln[hl]=='\t')) {
                CLOSE_ALL;
                const char *ht  = ln + hl + 1;
                int         hln = (int)strlen(ht);
                while (hln > 0 && ht[hln-1]=='#') hln--;
                while (hln > 0 && ht[hln-1]==' ') hln--;
                static const int FS[] = { 0, 48, 40, 32, 28, 26, 24 };
                bF(&out,
                   "\\pard\\f1\\b\\fs%d\\cf1\\sb280\\sa140 ", FS[hl]);
                inlineMD(&out, ht, (size_t)hln);
                bS(&out, "\\b0\\par\n\\pard\\f0\\fs24\\cf1 ");
                continue;
            }
        }

        /* ── setext heading (underline === or ---) ── */
        if (li+1 < n && !inPara) {
            char *nx  = L[li+1];
            int   nln = (int)strlen(nx);
            /* strip trailing whitespace before comparison */
            while (nln > 0 && isspace((unsigned char)nx[nln-1])) nln--;
            if (allChar(nx, nln, '=') && nln >= 1) {
                CLOSE_ALL;
                bS(&out, "\\pard\\f1\\b\\fs48\\cf1\\sb280\\sa140 ");
                inlineMD(&out, ln, (size_t)ll);
                bS(&out, "\\b0\\par\n\\pard\\f0\\fs24\\cf1 ");
                li++; continue;
            }
            if (allChar(nx, nln, '-') && nln >= 1) {
                CLOSE_ALL;
                bS(&out, "\\pard\\f1\\b\\fs40\\cf1\\sb280\\sa140 ");
                inlineMD(&out, ln, (size_t)ll);
                bS(&out, "\\b0\\par\n\\pard\\f0\\fs24\\cf1 ");
                li++; continue;
            }
        }

        /* ── blockquote ── */
        if (ln[0] == '>') {
            CLOSE_ALL;
            const char *qt = ln + 1;
            if (*qt == ' ') qt++;
            bS(&out,
               "\\pard\\li720\\ri720\\f0\\fs24\\cf2"
               "\\brdrl\\brdrw15\\brdrs\\brsp120 ");
            inlineMD(&out, qt, strlen(qt));
            bS(&out, "\\cf1\\par\n");
            continue;
        }

        /* ── indented code block (4 spaces or tab) ── */
        if (strncmp(ln, "    ", 4)==0 || ln[0]=='\t') {
            CLOSE_ALL;
            const char *ct = (ln[0]=='\t') ? ln+1 : ln+4;
            bS(&out, "\\pard\\li720\\f2\\fs20\\cf4\\sb0\\sa0 ");
            rtfEsc(&out, ct, strlen(ct));
            bS(&out, "\\par\n");
            continue;
        }

        /* ── GFM table ── */
        if (ln[0] == '|') {
            if (isTableSep(ln, ll)) continue;  /* skip separator row */
            CLOSE_ALL;
            /* count columns */
            int nc = 0;
            for (int k = 0; k < ll; k++) if (ln[k]=='|') nc++;
            if (nc >= 2) nc--;  /* cells = pipes − 1 */
            if (nc < 1)  nc = 1;
            int cw = 9000 / nc;
            bS(&out, "\\trowd\\trgaph108 ");
            for (int k = 0; k < nc; k++)
                bF(&out,
                   "\\clbrdrt\\brdrs\\brdrw10"
                   "\\clbrdrl\\brdrs\\brdrw10"
                   "\\clbrdrb\\brdrs\\brdrw10"
                   "\\clbrdrr\\brdrs\\brdrw10"
                   "\\cellx%d ", (k+1)*cw);
            /* cell contents */
            char *cp = ln + (ln[0]=='|' ? 1 : 0);
            for (int k = 0; k < nc; k++) {
                char *ep  = strchr(cp, '|');
                int   cl  = ep ? (int)(ep-cp) : (int)strlen(cp);
                while (cl>0 && *cp==' ')     { cp++; cl--; }
                while (cl>0 && cp[cl-1]==' ') cl--;
                bS(&out, "\\intbl\\f0\\fs24 ");
                inlineMD(&out, cp, (size_t)cl);
                bS(&out, "\\cell ");
                if (ep) cp = ep+1; else break;
            }
            bS(&out, "\\row\n");
            continue;
        }

        /* ── unordered list (-, *, +) ── */
        {
            int sp = 0;
            while (sp < ll && ln[sp]==' ') sp++;
            char m = ln[sp];
            if ((m=='-' || m=='*' || m=='+') &&
                sp+1 < ll && ln[sp+1]==' ') {
                CLOSE_PARA;
                int indent = 720 + (sp / 2) * 360;
                inList = 1;
                bF(&out,
                   "\\pard\\fi-360\\li%d\\f0\\fs24\\cf1\\sb40\\sa40 "
                   "{\\u8226?}\\tab ", indent);
                inlineMD(&out, ln+sp+2, (size_t)(ll-sp-2));
                bS(&out, "\\par\n");
                continue;
            }
        }

        /* ── ordered list (1. 2. …) ── */
        {
            int sp = 0;
            while (sp < ll && ln[sp]==' ') sp++;
            int k = sp;
            while (k < ll && isdigit((unsigned char)ln[k])) k++;
            if (k > sp && k < ll && ln[k]=='.' &&
                k+1 < ll && ln[k+1]==' ') {
                CLOSE_PARA;
                inList = 2;
                char num[32];
                int  nl2 = k - sp;
                if (nl2 > 31) nl2 = 31;
                memcpy(num, ln+sp, nl2);
                num[nl2] = '\0';
                bF(&out,
                   "\\pard\\fi-360\\li720\\f0\\fs24\\cf1\\sb40\\sa40 "
                   "%s.\\tab ", num);
                inlineMD(&out, ln+k+2, (size_t)(ll-k-2));
                bS(&out, "\\par\n");
                continue;
            }
        }

        /* ── paragraph ── */
        {
            /* hard line break: two trailing spaces */
            int hard = (ll >= 2 && ln[ll-1]==' ' && ln[ll-2]==' ');
            if (hard) ll -= 2;

            if (!inPara) {
                CLOSE_LIST;
                bS(&out, "\\pard\\f0\\fs24\\cf1\\sb0\\sa120 ");
                inPara = 1;
            } else {
                /* single newline = space in Markdown */
                bC(&out, ' ');
            }
            inlineMD(&out, ln, (size_t)ll);
            if (hard) bS(&out, "\\line\n");
        }
    }

    CLOSE_ALL;
    bS(&out, "}\n");

    /* --- write output file --- */
    FILE *fout = fopen(outName, "w");
    if (!fout) { perror(outName); return 1; }
    fwrite(out.d, 1, out.len, fout);
    fclose(fout);
    fprintf(stderr, "Written: %s\n", outName);

    /* --- free memory --- */
    free(raw);
    free(out.d);
    for (int i = 0; i < n; i++) free(L[i]);
    free(L);
    return 0;
}
