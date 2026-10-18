#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "web.h"

#pragma comment(lib, "wininet.lib")

#define DOWNLOAD_LIMIT (1024 * 1024)
#define TIMEOUT_MS 6000

/* ---------- downloading ---------- */

static char *download_bytes(const char *url, size_t *out_len)
{
    static const char headers[] =
        "Accept: text/html,application/json,image/*;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-US,en;q=0.9\r\n";
    HINTERNET session = NULL;
    HINTERNET request = NULL;
    char *buffer = NULL;
    size_t used = 0;
    size_t capacity = 16384;
    DWORD received = 0;
    DWORD timeout = (url && strstr(url, "r.jina.ai")) ? 25000 : TIMEOUT_MS;
    DWORD status = 0, status_size = sizeof(status);

    if (out_len)
        *out_len = 0;

    if (!url || !*url)
        return NULL;

    buffer = (char *)malloc(capacity);
    if (!buffer)
        return NULL;

    session = InternetOpenA(
        (strstr(url, "wikimedia.org") || strstr(url, "wikipedia.org"))
            ? "MiniSearchEngine/2.0 (student DSA project; Windows desktop app) WinINet"
            : "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!session)
        goto fail;

    InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT,
                       &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT,
                       &timeout, sizeof(timeout));

    request = InternetOpenUrlA(
        session, url, headers, (DWORD)-1,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
        INTERNET_FLAG_NO_UI, 0);
    if (!request)
        goto fail;

    if (HttpQueryInfoA(request,
            HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
            &status, &status_size, NULL) && status >= 400)
        goto fail;

    while (used < DOWNLOAD_LIMIT) {
        DWORD amount;

        if (capacity - used < 8192) {
            size_t next = capacity * 2;
            char *temp;

            if (next > DOWNLOAD_LIMIT + 1)
                next = DOWNLOAD_LIMIT + 1;
            if (next <= capacity)
                break;

            temp = (char *)realloc(buffer, next);
            if (!temp)
                goto fail;
            buffer = temp;
            capacity = next;
        }

        amount = (DWORD)(capacity - used - 1);
        if (amount > 8192)
            amount = 8192;

        if (!InternetReadFile(request, buffer + used, amount, &received))
            goto fail;
        if (received == 0)
            break;
        used += received;
    }

    buffer[used] = '\0';

    InternetCloseHandle(request);
    InternetCloseHandle(session);

    if (used == 0) {
        free(buffer);
        return NULL;
    }
    if (out_len)
        *out_len = used;
    return buffer;

fail:
    if (request)
        InternetCloseHandle(request);
    if (session)
        InternetCloseHandle(session);
    free(buffer);
    return NULL;
}

static char *download_url(const char *url)
{
    return download_bytes(url, NULL);
}

unsigned char *web_download(const char *url, size_t *len)
{
    return (unsigned char *)download_bytes(url, len);
}

/* ---------- string helpers ---------- */

static int equal_ci(char a, char b)
{
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static int starts_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (!equal_ci(*s, *prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static const char *find_ci(const char *text, const char *pattern)
{
    size_t n;

    if (!text || !pattern)
        return NULL;
    n = strlen(pattern);
    if (!n)
        return text;
    for (; *text; text++)
        if (starts_ci(text, pattern))
            return text;
    return NULL;
}

static int ends_ci(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && starts_ci(s + a - b, suffix);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_encode(const char *src, char *dst, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;

    if (!dst || size == 0)
        return;
    dst[0] = '\0';

    while (src && *src && n + 4 < size) {
        unsigned char c = (unsigned char)*src++;

        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[n++] = (char)c;
        } else if (c == ' ') {
            dst[n++] = '+';
        } else {
            dst[n++] = '%';
            dst[n++] = hex[c >> 4];
            dst[n++] = hex[c & 15];
        }
    }
    dst[n] = '\0';
}

static void url_decode(char *s)
{
    char *src = s;
    char *dst = s;

    if (!s)
        return;

    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            int a = hex_digit(src[1]);
            int b = hex_digit(src[2]);

            if (a >= 0 && b >= 0) {
                *dst++ = (char)((a << 4) | b);
                src += 3;
                continue;
            }
        }
        *dst++ = (*src == '+') ? ' ' : *src;
        src++;
    }
    *dst = '\0';
}

/* writes one Unicode character as UTF-8 */
static void put_utf8(char *o, size_t *n, size_t size, unsigned cp)
{
    char b[4];
    size_t l;

    if (cp < 0x80) {
        b[0] = (char)cp; l = 1;
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 63)); l = 2;
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 63));
        b[2] = (char)(0x80 | (cp & 63)); l = 3;
    } else {
        b[0] = (char)(0xF0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 63));
        b[2] = (char)(0x80 | ((cp >> 6) & 63));
        b[3] = (char)(0x80 | (cp & 63)); l = 4;
    }
    if (*n + l < size) {
        memcpy(o + *n, b, l);
        *n += l;
    }
}

static unsigned entity_cp(const char *name)
{
    static const struct { const char *n; unsigned cp; } T[] = {
        {"amp", '&'}, {"quot", '"'}, {"lt", '<'}, {"gt", '>'},
        {"nbsp", ' '}, {"apos", '\''}, {"rsquo", 0x2019}, {"lsquo", 0x2018},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"ndash", 0x2013},
        {"mdash", 0x2014}, {"hellip", 0x2026}
    };
    size_t i;

    if (name[0] == '#') {
        unsigned long v = (name[1] == 'x' || name[1] == 'X')
            ? strtoul(name + 2, NULL, 16) : strtoul(name + 1, NULL, 10);
        return (v > 0 && v < 0x110000) ? (unsigned)v : 0;
    }
    for (i = 0; i < sizeof(T) / sizeof(T[0]); i++)
        if (strcmp(name, T[i].n) == 0)
            return T[i].cp;
    return 0;
}

/* removes tags, decodes entities, squeezes spaces (output is UTF-8) */
static void html_text(const char *src, char *dst, size_t size)
{
    size_t n = 0;
    int in_tag = 0;
    int space = 1;

    if (!dst || size == 0)
        return;
    dst[0] = '\0';
    if (!src)
        return;

    while (*src && n + 1 < size) {
        unsigned char c = (unsigned char)*src++;

        if (c == '<') { in_tag = 1; continue; }
        if (in_tag) {
            if (c == '>')
                in_tag = 0;
            continue;
        }

        if (c == '&') {
            const char *semi = strchr(src, ';');

            if (semi && semi - src <= 10) {
                char name[12];
                size_t l = (size_t)(semi - src);
                unsigned cp;

                memcpy(name, src, l);
                name[l] = '\0';
                cp = entity_cp(name);

                if (cp) {
                    src = semi + 1;
                    if (cp == ' ') {
                        c = ' ';
                    } else {
                        put_utf8(dst, &n, size, cp);
                        space = 0;
                        continue;
                    }
                }
            }
        }

        if (isspace(c)) {
            if (!space && n + 1 < size) {
                dst[n++] = ' ';
                space = 1;
            }
        } else {
            dst[n++] = (char)c;
            space = 0;
        }
    }

    if (n && dst[n - 1] == ' ')
        n--;
    dst[n] = '\0';
}

static void get_attribute(const char *tag, const char *attribute,
                          char *out, size_t size)
{
    const char *p;
    char quote;
    size_t n = 0;

    if (!out || size == 0)
        return;
    out[0] = '\0';
    if (!tag || !attribute)
        return;

    p = find_ci(tag, attribute);
    if (!p)
        return;
    p += strlen(attribute);

    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p++ != '=')
        return;
    while (*p && isspace((unsigned char)*p))
        p++;

    quote = (*p == '"' || *p == '\'') ? *p++ : '\0';

    while (*p && n + 1 < size) {
        if ((quote && *p == quote) ||
            (!quote && (isspace((unsigned char)*p) || *p == '>')))
            break;
        out[n++] = *p++;
    }
    out[n] = '\0';
}

static void clean_range(const char *from, const char *to,
                        char *dst, size_t size)
{
    size_t len;
    char *temp;

    if (!dst || size == 0)
        return;
    dst[0] = '\0';
    if (!from || !to || to <= from)
        return;

    len = (size_t)(to - from);
    temp = (char *)malloc(len + 1);
    if (!temp)
        return;
    memcpy(temp, from, len);
    temp[len] = '\0';
    html_text(temp, dst, size);
    free(temp);
}

/* ---------- tiny JSON helpers ---------- */

/* p points just after an opening quote. Decodes the string into out. */
static const char *json_str_ex(const char *p, char *out, size_t size, int keep_nl)
{
    size_t n = 0;

    if (size == 0)
        return p;
    out[0] = '\0';

    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (*p == 'u') {
                unsigned cp = 0;
                int i;

                for (i = 1; i <= 4; i++) {
                    int d = hex_digit(p[i]);
                    if (d < 0)
                        break;
                    cp = cp * 16 + (unsigned)d;
                }
                p += 4;

                if (cp >= 0xD800 && cp < 0xDC00 &&
                    p[1] == '\\' && p[2] == 'u') {
                    unsigned lo = 0;
                    for (i = 3; i <= 6; i++) {
                        int d = hex_digit(p[i]);
                        if (d < 0)
                            break;
                        lo = lo * 16 + (unsigned)d;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                }
                put_utf8(out, &n, size, cp);
            } else if (*p == 'n' || *p == 't' || *p == 'r') {
                put_utf8(out, &n, size, (keep_nl && *p == 'n') ? '\n' : ' ');
            } else if (*p) {
                put_utf8(out, &n, size, (unsigned char)*p);
            }
            if (*p)
                p++;
        } else {
            if (n + 1 < size)
                out[n++] = *p;
            p++;
        }
    }
    out[n] = '\0';
    return *p == '"' ? p + 1 : p;
}

static const char *json_str(const char *p, char *out, size_t size)
{
    return json_str_ex(p, out, size, 0);
}

/* ---------- Wikipedia (always-available source) ---------- */

static int wiki_search(const char *query, WebResult results[], int max)
{
    char enc[1024], addr[2048];
    char *json;
    const char *p;
    int count = 0;

    url_encode(query, enc, sizeof(enc));
    snprintf(addr, sizeof(addr),
        "https://en.wikipedia.org/w/api.php?action=query&list=search"
        "&srsearch=%s&srlimit=%d&format=json", enc, max);

    json = download_url(addr);
    if (!json)
        return 0;

    p = json;
    while (count < max && (p = strstr(p, "\"title\":\"")) != NULL) {
        char title[MAX_WEB_TITLE], raw[MAX_WEB_TEXT], slug[MAX_WEB_TITLE];
        char encslug[1536];
        const char *sn, *next;
        size_t i;

        p = json_str(p + 9, title, sizeof(title));

        sn = strstr(p, "\"snippet\":\"");
        next = strstr(p, "\"title\":\"");

        results[count].snippet[0] = '\0';
        if (sn && (!next || sn < next)) {
            json_str(sn + 11, raw, sizeof(raw));
            html_text(raw, results[count].snippet,
                      sizeof(results[count].snippet));
        }

        snprintf(slug, sizeof(slug), "%s", title);
        for (i = 0; slug[i]; i++)
            if (slug[i] == ' ')
                slug[i] = '_';
        url_encode(slug, encslug, sizeof(encslug));

        snprintf(results[count].title, sizeof(results[count].title),
                 "%s - Wikipedia", title);
        snprintf(results[count].url, sizeof(results[count].url),
                 "https://en.wikipedia.org/wiki/%s", encslug);
        count++;
    }

    free(json);
    return count;
}

/* "what is a duck?" -> "duck" */
static void clean_query(const char *q, char *out, size_t size)
{
    static const char *P[] = {"what is ", "what are ", "what's ", "whats ", "who is ",
        "who was ", "who are ", "define ", "meaning of ", "tell me about ",
        "explain ", "about "};
    static const char *ART[] = {"a ", "an ", "the "};
    const char *s = q;
    int again = 1, stripped = 0;
    size_t n, i;

    while (*s == ' ')
        s++;
    while (again) {
        again = 0;
        for (i = 0; i < sizeof(P) / sizeof(P[0]); i++) {
            size_t l = strlen(P[i]);
            if (starts_ci(s, P[i]) && s[l]) {
                s += l;
                while (*s == ' ')
                    s++;
                again = 1;
                stripped = 1;
                break;
            }
        }
    }
    /* "what is a duck" -> "duck", but "the matrix" stays "the matrix" */
    if (stripped) {
        for (i = 0; i < sizeof(ART) / sizeof(ART[0]); i++) {
            size_t l = strlen(ART[i]);
            if (starts_ci(s, ART[i]) && s[l]) {
                s += l;
                break;
            }
        }
    }
    snprintf(out, size, "%s", s);
    n = strlen(out);
    while (n && (out[n - 1] == ' ' || out[n - 1] == '?' || out[n - 1] == '.'))
        out[--n] = '\0';
    if (!out[0])
        snprintf(out, size, "%s", q);
}

static const char *last_key_before(const char *json, const char *key, const char *limit)
{
    const char *p = json, *last = NULL;
    while ((p = strstr(p, key)) != NULL && p < limit) {
        last = p;
        p += strlen(key);
    }
    return last;
}

/* downloads one Wikipedia extract URL; 1 if it gave a real answer */
static int wiki_extract(const char *addr, char *title, int tsize, char *text, int size)
{
    char *json = download_url(addr);
    const char *ex, *tt;

    if (!json)
        return 0;

    ex = strstr(json, "\"extract\":\"");
    if (!ex) {
        free(json);
        return 0;
    }
    tt = last_key_before(json, "\"title\":\"", ex);
    if (tt && title && tsize > 0)
        json_str(tt + 9, title, (size_t)tsize);
    json_str(ex + 11, text, (size_t)size);
    free(json);

    if (strlen(text) < 40 || strstr(text, "may refer to") ||
        strstr(text, "may also refer to")) {
        text[0] = '\0';
        return 0;
    }
    return 1;
}

int web_quick_answer(const char *query, char *title, int title_size,
                     char *text, int text_size)
{
    char clean[300], tcase[300], names[1000], enc[2048], addr[4096];
    size_t i, n;
    int up = 1;

    if (title && title_size > 0)
        title[0] = '\0';
    if (text && text_size > 0)
        text[0] = '\0';
    if (!query || !*query || !text || text_size < 2)
        return 0;

    clean_query(query, clean, sizeof(clean));

    /* Title Case version: "elon musk" -> "Elon Musk" */
    for (i = 0, n = 0; clean[i] && n + 1 < sizeof(tcase); i++) {
        tcase[n++] = up ? (char)toupper((unsigned char)clean[i]) : clean[i];
        up = (clean[i] == ' ');
    }
    tcase[n] = '\0';

    /* 1) exact article title (several spellings in ONE request) */
    snprintf(names, sizeof(names), "%s|%s", clean, tcase);
    if (strlen(clean) <= 5) {
        char caps[64];
        for (i = 0; clean[i] && i < sizeof(caps) - 1; i++)
            caps[i] = (char)toupper((unsigned char)clean[i]);
        caps[i] = '\0';
        n = strlen(names);
        snprintf(names + n, sizeof(names) - n, "|%s", caps);
    }
    url_encode(names, enc, sizeof(enc));
    snprintf(addr, sizeof(addr),
        "https://en.wikipedia.org/w/api.php?action=query&titles=%s"
        "&prop=extracts&exintro=1&explaintext=1&exsentences=3&redirects=1&format=json", enc);
    if (wiki_extract(addr, title, title_size, text, text_size))
        return 1;

    /* 2) best matching article for the words */
    url_encode(clean, enc, sizeof(enc));
    snprintf(addr, sizeof(addr),
        "https://en.wikipedia.org/w/api.php?action=query&generator=search"
        "&gsrsearch=%s&gsrlimit=1&prop=extracts&exintro=1&explaintext=1"
        "&exsentences=3&redirects=1&format=json", enc);
    return wiki_extract(addr, title, title_size, text, text_size);
}

int web_image_search(const char *query, char urls[][MAX_WEB_URL], int max, int mode)
{
    char q2[512], enc[1536], addr[3072];
    char *json;
    const char *p;
    int n = 0;

    snprintf(q2, sizeof(q2), "%s", query);
    url_encode(q2, enc, sizeof(enc));

    if (mode == 0) {
        /* Wikimedia Commons photos (500 = an allowed thumbnail width) */
        snprintf(addr, sizeof(addr),
            "https://commons.wikimedia.org/w/api.php?action=query"
            "&generator=search&gsrnamespace=6&gsrsearch=%s+filetype%%3Abitmap"
            "&gsrlimit=%d&prop=imageinfo&iiprop=url&iiurlwidth=500&format=json",
            enc, max + 8 > 20 ? 20 : max + 8);
    } else {
        /* main pictures of matching Wikipedia articles */
        snprintf(addr, sizeof(addr),
            "https://en.wikipedia.org/w/api.php?action=query&generator=search"
            "&gsrsearch=%s&gsrlimit=20&prop=pageimages&piprop=thumbnail"
            "&pithumbsize=500&pilimit=20&format=json", enc);
    }

    json = download_url(addr);
    if (!json)
        return 0;

    p = json;
    if (mode == 0) {
        while (n < max && (p = strstr(p, "\"thumburl\":\"")) != NULL) {
            p = json_str(p + 12, urls[n], MAX_WEB_URL);
            if (ends_ci(urls[n], ".jpg") || ends_ci(urls[n], ".jpeg") ||
                ends_ci(urls[n], ".png"))
                n++;
        }
    } else {
        while (n < max && (p = strstr(p, "\"source\":\"")) != NULL) {
            p = json_str(p + 10, urls[n], MAX_WEB_URL);
            if (ends_ci(urls[n], ".jpg") || ends_ci(urls[n], ".jpeg") ||
                ends_ci(urls[n], ".png"))
                n++;
        }
    }
    free(json);
    return n;
}

/* ---------- public: web search ---------- */

int web_search(const char *query, WebResult results[], int max_results)
{
    char encoded[2048];
    char address[4096];
    char *html;
    const char *p;
    int count = 0;

    if (!results || max_results <= 0)
        return 0;

    memset(results, 0, sizeof(*results) * (size_t)max_results);

    if (!query || !*query)
        return 0;

    url_encode(query, encoded, sizeof(encoded));
    snprintf(address, sizeof(address),
             "https://html.duckduckgo.com/html/?q=%s", encoded);

    html = download_url(address);

    p = html;
    while (html && count < max_results) {
        const char *mark = find_ci(p, "result__a");
        const char *tag, *end, *text_end;
        char tagbuf[4096];
        char url[MAX_WEB_URL] = "";
        char title[MAX_WEB_TITLE] = "";
        size_t tl;

        if (!mark)
            break;

        tag = mark;
        while (tag > html && *tag != '<')
            tag--;
        if (*tag != '<') {
            p = mark + 9;
            continue;
        }

        end = strchr(tag, '>');
        if (!end)
            break;

        tl = (size_t)(end - tag + 1);
        if (tl >= sizeof(tagbuf))
            tl = sizeof(tagbuf) - 1;
        memcpy(tagbuf, tag, tl);
        tagbuf[tl] = '\0';
        get_attribute(tagbuf, "href", url, sizeof(url));

        text_end = find_ci(end + 1, "</a>");
        clean_range(end + 1, text_end, title, sizeof(title));

        if (find_ci(url, "uddg=")) {
            char *d = (char *)find_ci(url, "uddg=") + 5;
            char *amp = strchr(d, '&');

            if (amp)
                *amp = '\0';
            url_decode(d);
            memmove(url, d, strlen(d) + 1);
        }

        if (strncmp(url, "//", 2) == 0) {
            char temp[MAX_WEB_URL];

            snprintf(temp, sizeof(temp), "https:%s", url);
            snprintf(url, sizeof(url), "%s", temp);
        }

        if (!find_ci(url, "://") || !title[0] || find_ci(url, "duckduckgo.com/y.js")) {
            p = end + 1;
            continue;
        }

        snprintf(results[count].url, sizeof(results[count].url), "%s", url);
        snprintf(results[count].title, sizeof(results[count].title), "%s", title);

        {
            const char *sn = find_ci(end, "result__snippet");
            const char *nx = find_ci(end + 1, "result__a");

            if (sn && (!nx || sn < nx)) {
                const char *gt = strchr(sn, '>');
                const char *close = gt ? find_ci(gt, "</a>") : NULL;

                if (close)
                    clean_range(gt + 1, close, results[count].snippet,
                                sizeof(results[count].snippet));
            }
        }

        count++;
        p = end + 1;
    }

    free(html);

    /* DuckDuckGo blocked us or returned nothing: use Wikipedia so a
       basic search like "duck" or "pond" always gets an answer */
    if (count == 0)
        count = wiki_search(query, results, max_results);

    return count;
}

/* ---------- page -> clean blocks (headings, paragraphs, bullets) ---------- */

typedef struct {
    char *out;
    size_t used, cap;
    char *line;
    size_t ln;
    const char *prefix;
    int relaxed;
    char last[300];
} Blocks;

#define LINE_MAX_CHARS 16000

static void flush_block(Blocks *b)
{
    char *dec;
    size_t len, minlen;

    if (b->ln == 0)
        return;
    b->line[b->ln] = '\0';
    b->ln = 0;

    dec = (char *)malloc(LINE_MAX_CHARS + 8);
    if (!dec)
        return;
    html_text(b->line, dec, LINE_MAX_CHARS + 8);
    len = strlen(dec);

    if (len == 0) {                 /* only spaces: keep waiting for real text */
        free(dec);
        return;
    }

    minlen = b->prefix[0] == '#' ? 3 : (b->prefix[0] == '-' ? 3 : (b->relaxed ? 10 : 25));
    if (len >= minlen && strncmp(dec, b->last, sizeof(b->last) - 1) != 0 &&
        b->used + len + 4 < b->cap) {
        size_t pl = strlen(b->prefix);
        memcpy(b->out + b->used, b->prefix, pl);
        b->used += pl;
        memcpy(b->out + b->used, dec, len);
        b->used += len;
        b->out[b->used++] = '\n';
        b->out[b->used] = '\0';
        snprintf(b->last, sizeof(b->last), "%s", dec);
    }
    b->prefix = "";
    free(dec);
}

static int name_in(const char *name, const char *list)
{
    /* list is like "|p|div|br|" */
    char key[24];
    snprintf(key, sizeof(key), "|%s|", name);
    return strstr(list, key) != NULL;
}

static int eq_ci(const char *a, const char *b);

/* value of one attribute (class / id) inside a single tag */
static void tag_attr(const char *tag, const char *name, char *out, size_t size)
{
    const char *p = tag;
    size_t nl = strlen(name);

    out[0] = '\0';
    while ((p = find_ci(p, name)) != NULL) {
        const char *q = p + nl;
        if (p > tag && isspace((unsigned char)p[-1])) {
            while (isspace((unsigned char)*q))
                q++;
            if (*q == '=') {
                char quote;
                size_t n = 0;
                q++;
                while (isspace((unsigned char)*q))
                    q++;
                quote = (*q == '"' || *q == '\'') ? *q++ : '\0';
                while (*q && n + 1 < size &&
                       !(quote ? *q == quote : (isspace((unsigned char)*q) || *q == '>')))
                    out[n++] = *q++;
                out[n] = '\0';
                return;
            }
        }
        p = q;
    }
}

/* class / id names that mark menus, language lists, ads, share bars, cookie notices... */
static int attr_noise(const char *v)
{
    static const char *SUB[] = {"interlanguage", "p-lang", "portlet-lang", "vector-menu",
        "vector-dropdown", "vector-toc", "vector-page-toolbar", "vector-sticky",
        "mw-editsection", "navbox", "breadcrumb", "cookie", "consent", "gdpr", "newsletter",
        "advert", "sponsor", "skip-link", "printfooter", "catlinks", "mw-jump",
        "social-share", "share-button", "related-post", "comment-list", "comments-area",
        "site-footer", "site-header", "back-to-top", "mw-navigation", "mw-panel", "mw-head",
        "sitenotice", "mw-indicators"};
    static const char *TOK[] = {"toc", "nav", "navbar", "menu", "footer", "ads", "banner",
        "share", "social", "comments", "related", "breadcrumbs", "popup", "modal", "widget",
        "pagination", "sidebar"};
    char tok[64];
    const char *p = v;
    size_t i;

    for (i = 0; i < sizeof(SUB) / sizeof(SUB[0]); i++)
        if (find_ci(v, SUB[i]))
            return 1;
    while (*p) {
        size_t n = 0;
        while (*p && isspace((unsigned char)*p))
            p++;
        while (*p && !isspace((unsigned char)*p) && n + 1 < sizeof(tok))
            tok[n++] = *p++;
        tok[n] = '\0';
        for (i = 0; n && i < sizeof(TOK) / sizeof(TOK[0]); i++)
            if (eq_ci(tok, TOK[i]))
                return 1;
    }
    return 0;
}

static int tag_noise(const char *tag)
{
    char v[400];
    tag_attr(tag, "class", v, sizeof(v));
    if (v[0] && attr_noise(v))
        return 1;
    tag_attr(tag, "id", v, sizeof(v));
    return v[0] && attr_noise(v);
}

/* p is just after an opening <name ...>; returns the point after its matching </name> */
static const char *skip_subtree(const char *p, const char *name)
{
    int depth = 1;
    size_t nl = strlen(name);

    while ((p = strchr(p, '<')) != NULL) {
        int closing = (p[1] == '/');
        const char *q = p + 1 + closing;
        if (starts_ci(q, name) && !isalnum((unsigned char)q[nl])) {
            const char *e = strchr(q, '>');
            if (!e)
                return NULL;
            if (closing) {
                if (--depth == 0)
                    return e + 1;
            } else if (e[-1] != '/') {
                depth++;
            }
            p = e + 1;
        } else {
            p++;
        }
    }
    return NULL;
}

static int html_to_blocks(const char *html, int from_main, int relaxed, char *out, size_t cap)
{
    static const char SKIP[]   = "|script|style|nav|footer|aside|noscript|svg|button|select|iframe|head|template|";
    static const char BLOCK[]  = "|p|div|br|tr|section|article|blockquote|pre|ul|ol|table|dd|dt|main|header|figure|figcaption|";
    static const char INLINE[] = "|a|span|b|i|em|strong|u|small|sup|sub|code|mark|abbr|cite|q|font|time|";
    Blocks b;
    const char *p, *start;

    out[0] = '\0';
    memset(&b, 0, sizeof(b));
    b.out = out;
    b.cap = cap;
    b.prefix = "";
    b.relaxed = relaxed;
    b.line = (char *)malloc(LINE_MAX_CHARS + 8);
    if (!b.line)
        return 0;

    start = NULL;
    if (from_main) {
        start = find_ci(html, "<article");
        if (!start) start = find_ci(html, "<main");
    }
    p = start ? start : html;

    while (*p && b.used + 200 < cap) {
        if (*p == '<') {
            const char *end, *q;
            char name[16];
            int closing = 0, k = 0;

            if (strncmp(p, "<!--", 4) == 0) {
                const char *e = strstr(p + 4, "-->");
                if (!e)
                    break;
                p = e + 3;
                continue;
            }
            end = strchr(p, '>');
            if (!end)
                break;

            q = p + 1;
            if (*q == '/') { closing = 1; q++; }
            while (isalnum((unsigned char)*q) && k < 15)
                name[k++] = (char)tolower((unsigned char)*q++);
            name[k] = '\0';

            if (!closing && k && name_in(name, SKIP)) {
                char pat[24];
                const char *e;
                flush_block(&b);
                snprintf(pat, sizeof(pat), "</%s", name);
                e = find_ci(end + 1, pat);
                if (e) {
                    const char *ee = strchr(e, '>');
                    p = ee ? ee + 1 : e + 1;
                } else {
                    p = end + 1;
                }
                continue;
            }

            /* menus, language lists, share bars, cookie notices: skip the whole section */
            if (!closing && k && end[-1] != '/' &&
                name_in(name, "|div|section|ul|ol|aside|table|form|details|dl|span|header|figure|li|a|")) {
                char tg[1024];
                size_t tl = (size_t)(end - p + 1);
                if (tl >= sizeof(tg))
                    tl = sizeof(tg) - 1;
                memcpy(tg, p, tl);
                tg[tl] = '\0';
                if (tag_noise(tg)) {
                    const char *after = skip_subtree(end + 1, name);
                    if (after) {
                        flush_block(&b);
                        p = after;
                        continue;
                    }
                }
            }

            if (k == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
                flush_block(&b);
                if (!closing)
                    b.prefix = "# ";
            } else if (k == 2 && name[0] == 'l' && name[1] == 'i') {
                flush_block(&b);
                if (!closing)
                    b.prefix = "- ";
            } else if (k && name_in(name, BLOCK)) {
                flush_block(&b);
            } else if (k && !name_in(name, INLINE)) {
                if (b.ln && b.ln < LINE_MAX_CHARS)
                    b.line[b.ln++] = ' ';
            }
            p = end + 1;
            continue;
        }

        if (b.ln < LINE_MAX_CHARS)
            b.line[b.ln++] = *p;
        p++;
    }
    flush_block(&b);
    free(b.line);
    return out[0] != '\0';
}

/* ---------- reader-service text (markdown) -> blocks ---------- */

/* removes links, images and emphasis marks from one markdown line */
static void md_inline(const char *src, char *dst, size_t size)
{
    size_t n = 0;

    while (*src && n + 1 < size) {
        if (src[0] == '[' && src[1] == '!') {              /* [![alt](img)](url) */
            const char *p = strchr(src, ')');
            if (p && p[1] == ']' && p[2] == '(') {
                const char *e = strchr(p + 3, ')');
                if (e) { src = e + 1; continue; }
            }
        }
        if (src[0] == '!' && src[1] == '[') {              /* ![alt](img) */
            const char *c = strchr(src, ']');
            if (c && c[1] == '(') {
                const char *e = strchr(c + 2, ')');
                if (e) { src = e + 1; continue; }
            }
        }
        if (*src == '[') {                                   /* [text](url) */
            const char *c = strchr(src, ']');
            if (c && c[1] == '(') {
                const char *e = strchr(c + 2, ')');
                if (e) {
                    const char *q;
                    for (q = src + 1; q < c && n + 1 < size; q++)
                        dst[n++] = *q;
                    src = e + 1;
                    continue;
                }
            }
        }
        if ((src[0] == '*' && src[1] == '*') || (src[0] == '_' && src[1] == '_')) {
            src += 2;
            continue;
        }
        if (*src == '`') { src++; continue; }
        dst[n++] = *src++;
    }
    dst[n] = '\0';
}

static void markdown_to_blocks(const char *md, char *out, size_t cap)
{
    const char *p = md;
    const char *mc = strstr(md, "Markdown Content:");
    size_t used = 0;
    char last[300];

    last[0] = '\0';
    out[0] = '\0';
    if (mc)
        p = mc + 17;

    while (*p && used + 300 < cap) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[4096], txt[4096];
        char *t = line;
        const char *prefix = "";
        size_t minlen = 30, tl;

        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = e ? e + 1 : p + len;

        while (*t == ' ' || *t == '\t')
            t++;
        tl = strlen(t);
        while (tl && (t[tl - 1] == '\r' || t[tl - 1] == ' '))
            t[--tl] = '\0';

        if (!*t || *t == '|' || strncmp(t, "---", 3) == 0 || strncmp(t, "===", 3) == 0)
            continue;

        if (*t == '#') {
            while (*t == '#')
                t++;
            while (*t == ' ')
                t++;
            prefix = "# ";
            minlen = 3;
        } else if ((*t == '*' || *t == '-' || *t == '+') && t[1] == ' ') {
            t += 2;
            while (*t == ' ')
                t++;
            prefix = "- ";
            minlen = 6;
        } else if (*t == '>') {
            t++;
            while (*t == ' ')
                t++;
        }

        md_inline(t, txt, sizeof(txt));
        t = txt;
        while (*t == ' ')
            t++;
        tl = strlen(t);
        while (tl && t[tl - 1] == ' ')
            t[--tl] = '\0';

        if (tl < minlen || strncmp(t, "http", 4) == 0 || strcmp(t, last) == 0)
            continue;

        if (used + strlen(prefix) + tl + 2 >= cap)
            break;
        memcpy(out + used, prefix, strlen(prefix));
        used += strlen(prefix);
        memcpy(out + used, t, tl);
        used += tl;
        out[used++] = '\n';
        out[used] = '\0';
        snprintf(last, sizeof(last), "%s", t);
    }
}

/* ---------- filtering out results with no information ---------- */

/* login walls, error pages, app-store listings... (decided from title and address only) */
int web_is_junk_result(const char *title, const char *url, const char *snippet)
{
    static const char *T[] = {"just a moment", "access denied", "403 forbidden", "404",
        "page not found", "sign in", "log in", "login", "attention required",
        "are you a robot", "not found", "service unavailable", "error 4", "error 5"};
    static const char *U[] = {"/login", "/signin", "/sign-in", "/sign_in", "/account/",
        "/cart", "play.google.com/store", "apps.apple.com", "/auth/", "accounts.google.com",
        "facebook.com", "instagram.com", "tiktok.com", "pinterest.", "linkedin.com",
        "snapchat.com", "twitter.com", "//x.com/", "//www.x.com/", "youtube.com/watch",
        "youtu.be/", "//t.me/"};
    size_t i;

    (void)snippet;
    for (i = 0; title && i < sizeof(T) / sizeof(T[0]); i++)
        if (starts_ci(title, T[i]) || (strlen(title) < 16 && find_ci(title, T[i])))
            return 1;
    for (i = 0; url && i < sizeof(U) / sizeof(U[0]); i++)
        if (find_ci(url, U[i]))
            return 1;
    return 0;
}

/* 1 = has real text, 0 = page is empty or nearly empty, -1 = can't tell */
static int classify_html(const char *html)
{
    char *tmp = (char *)malloc(60000);
    size_t best = 0;
    int attempt, scripts = 0;
    const char *p;

    if (!tmp)
        return -1;
    for (attempt = 0; attempt < 2; attempt++) {
        size_t len;
        html_to_blocks(html, attempt == 0, 0, tmp, 60000);
        len = strlen(tmp);
        if (len > best)
            best = len;
        if (best >= 300)
            break;
    }
    free(tmp);
    if (best >= 300)
        return 1;

    /* little text: is it an empty page, or one that is built by JavaScript / blocking us? */
    p = html;
    while ((p = find_ci(p, "<script")) != NULL && scripts < 20) {
        scripts++;
        p += 7;
    }
    if (scripts >= 8 || find_ci(html, "id=\"root\"") || find_ci(html, "id=\"app\"") ||
        find_ci(html, "__NEXT_DATA__") || find_ci(html, "just a moment") ||
        find_ci(html, "enable javascript"))
        return -1;
    return 0;
}

int web_page_check(const char *url)
{
    char *html = download_url(url);
    int verdict;

    if (!html)
        return -1;                      /* blocked or offline: keep it, the reader will try */
    if (strncmp(html, "%PDF", 4) == 0) {
        free(html);
        return -1;
    }
    verdict = classify_html(html);
    free(html);
    return verdict;
}

/* ---------- public: read one page ---------- */

static void meta_description(const char *html, char *out, size_t size)
{
    const char *p = html;
    out[0] = '\0';

    while ((p = find_ci(p, "<meta")) != NULL) {
        const char *end = strchr(p, '>');
        char tag[2048], val[1500];
        size_t tl;

        if (!end)
            break;
        tl = (size_t)(end - p + 1);
        if (tl >= sizeof(tag))
            tl = sizeof(tag) - 1;
        memcpy(tag, p, tl);
        tag[tl] = '\0';

        if (find_ci(tag, "description")) {
            get_attribute(tag, "content", val, sizeof(val));
            if (strlen(val) > 30) {
                html_text(val, out, size);
                return;
            }
        }
        p = end + 1;
    }
}

/* ---------- polish: remove menus, cookie notices, share buttons, bylines ---------- */

static int eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (!equal_ci(*a, *b))
            return 0;
        a++;
        b++;
    }
    return !*a && !*b;
}

static int line_noise(const char *t)
{
    static const char *EQ[] = {"menu", "main menu", "home", "about", "contact", "search",
        "contents", "table of contents", "navigation", "toggle navigation", "share",
        "tweet", "print", "email", "comments", "advertisement", "close"};
    static const char *START[] = {"skip to", "sign up", "sign in", "log in", "login",
        "subscribe", "share this", "share on", "follow us", "advertisement",
        "leave a comment", "posted in", "posted on", "tags:", "categories:",
        "related posts", "published", "last updated", "updated on", "written by",
        "reviewed by", "fact checked", "fact-checked", "jump to", "accept cookies",
        "we use cookies", "this website uses", "this site uses", "read more",
        "from wikipedia", "jump to navigation", "this article is about", "for other uses",
        "not to be confused with", "this article needs", "this article may", "main article:"};
    static const char *HAS[] = {"cookie policy", "use cookies", "uses cookies", "accept all",
        "privacy policy", "all rights reserved", "terms of use", "terms and conditions",
        "enable javascript", "your email address", "min read", "minute read", "newsletter",
        "cookie settings", "cookie preferences"};
    size_t len = strlen(t), i;

    if (len >= 120)
        return 0;
    for (i = 0; i < sizeof(EQ) / sizeof(EQ[0]); i++)
        if (eq_ci(t, EQ[i]))
            return 1;
    for (i = 0; i < sizeof(START) / sizeof(START[0]); i++)
        if (starts_ci(t, START[i]))
            return 1;
    if (len < 60 && starts_ci(t, "by "))
        return 1;
    for (i = 0; i < sizeof(HAS) / sizeof(HAS[0]); i++)
        if (find_ci(t, HAS[i]))
            return 1;
    return 0;
}

/* removes runs of short one-word bullets: language menus, link lists */
static void drop_short_lists(char *s)
{
    static char *st[2500];
    static unsigned char kind[2500], drop[2500], hasdig[2500], nonasc[2500], shortl[2500];
    int n = 0, i, firstpara;
    char *p = s, *w = s;

    while (*p && n < 2500) {
        char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p), off = 0, k;

        st[n] = p;
        kind[n] = 0; hasdig[n] = 0; nonasc[n] = 0; drop[n] = 0;
        if (l > 1 && p[0] == '#' && p[1] == ' ') { kind[n] = 1; off = 2; }
        else if (l > 1 && p[0] == '-' && p[1] == ' ') { kind[n] = 2; off = 2; }
        shortl[n] = (l - off) <= 40;
        for (k = off; k < l; k++) {
            if (p[k] >= '0' && p[k] <= '9') hasdig[n] = 1;
            if ((unsigned char)p[k] >= 0x80) nonasc[n] = 1;
        }
        if (kind[n] == 0 && (l - off) >= 70 && l > 0) kind[n] = 3;   /* real paragraph */
        n++;
        if (!e)
            break;
        p = e + 1;
    }

    firstpara = n;
    for (i = 0; i < n; i++)
        if (kind[i] == 3) { firstpara = i; break; }

    for (i = 0; i < n; ) {
        int j = i, run, digs = 0, nas = 0, allshort = 1;

        if (kind[i] != 2) { i++; continue; }
        while (j < n && kind[j] == 2) {
            if (hasdig[j]) digs++;
            if (nonasc[j]) nas++;
            if (!shortl[j]) allshort = 0;
            j++;
        }
        run = j - i;

        if (allshort && ((run >= 8 && nas >= 3) ||
            (run >= 6 && i <= 2 && digs * 10 < run * 3 && firstpara < j + 12 && firstpara >= j))) {
            int k;
            for (k = i; k < j; k++)
                drop[k] = 1;
            if (i > 0 && kind[i - 1] == 1 && i - 1 > 0)   /* orphan section heading */
                drop[i - 1] = 1;
        }
        i = j;
    }

    for (i = 0; i < n; i++) {
        char *e = strchr(st[i], '\n');
        size_t l = e ? (size_t)(e - st[i]) : strlen(st[i]);
        if (drop[i])
            continue;
        memmove(w, st[i], l);
        w += l;
        if (e)
            *w++ = '\n';
    }
    *w = '\0';
}

static void polish_text(char *s)
{
    char *r = s, *w = s, *p, *startk = NULL;
    int count = 0;

    /* 1) drop junk lines everywhere */
    while (*r) {
        char *e = strchr(r, '\n');
        size_t l = e ? (size_t)(e - r) : strlen(r);
        const char *t = r;
        char tmp[256];
        size_t tl;

        if (l > 1 && (r[0] == '#' || r[0] == '-') && r[1] == ' ')
            t = r + 2;
        tl = l - (size_t)(t - r);
        if (tl > 0 && tl < sizeof(tmp)) {
            memcpy(tmp, t, tl);
            tmp[tl] = '\0';
            if (line_noise(tmp)) {
                r = e ? e + 1 : r + l;
                continue;
            }
        }
        memmove(w, r, l);
        w += l;
        if (e)
            *w++ = '\n';
        r = e ? e + 1 : r + l;
    }
    *w = '\0';

    /* 1b) language menus and link lists */
    drop_short_lists(s);

    /* 2) cut short clutter above the real start (first heading or first real paragraph) */
    p = s;
    while (*p && count < 15) {
        char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        int heading = (l > 1 && p[0] == '#' && p[1] == ' ');
        size_t off = (heading || (l > 1 && p[0] == '-' && p[1] == ' ')) ? 2 : 0;

        if (l > 0) {
            if (heading || l - off >= 70) {
                startk = p;
                break;
            }
            count++;
        }
        p = e ? e + 1 : p + l;
    }
    if (startk && startk != s)
        memmove(s, startk, strlen(startk) + 1);

    /* 3) a long run of tiny list items right at the top is a menu (language list, link list) */
    {
        char *q = s, *runstart = NULL;
        int run = 0;

        while (*q) {
            char *e = strchr(q, '\n');
            size_t l = e ? (size_t)(e - q) : strlen(q);
            char *next = e ? e + 1 : q + l;

            if (l == 0) {
                q = next;
                continue;
            }
            if (l > 1 && q[0] == '-' && q[1] == ' ' && l - 2 <= 30) {
                if (!run)
                    runstart = q;
                run++;
                q = next;
                continue;
            }
            if (!run && l > 1 && q[0] == '#' && q[1] == ' ') {
                q = next;
                continue;
            }
            break;
        }
        if (run >= 15 && runstart)
            memmove(runstart, q, strlen(q) + 1);
    }
}

/* ---------- Wikipedia articles: clean text, no menus ---------- */

static int wiki_text_to_blocks(const char *text, char *out, size_t cap)
{
    static const char *STOP[] = {"see also", "references", "external links", "further reading",
        "notes", "bibliography", "sources", "footnotes", "citations"};
    const char *p = text;
    size_t used = 0;

    out[0] = '\0';
    while (*p && used + 300 < cap) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p), tl, i;
        char line[2048];
        char *t = line;
        const char *prefix = "";
        size_t minlen = 20;

        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = e ? e + 1 : p + len;

        while (*t == ' ')
            t++;
        tl = strlen(t);
        while (tl && (t[tl - 1] == ' ' || t[tl - 1] == '\r'))
            t[--tl] = '\0';
        if (!tl)
            continue;

        if (*t == '=') {
            int stop = 0;
            while (*t == '=')
                t++;
            while (*t == ' ')
                t++;
            tl = strlen(t);
            while (tl && (t[tl - 1] == '=' || t[tl - 1] == ' '))
                t[--tl] = '\0';
            for (i = 0; i < sizeof(STOP) / sizeof(STOP[0]); i++)
                if (eq_ci(t, STOP[i]))
                    stop = 1;
            if (stop)
                break;
            prefix = "# ";
            minlen = 2;
        }
        if (tl < minlen)
            continue;
        if (used + strlen(prefix) + tl + 2 >= cap)
            break;
        memcpy(out + used, prefix, strlen(prefix));
        used += strlen(prefix);
        memcpy(out + used, t, tl);
        used += tl;
        out[used++] = '\n';
        out[used] = '\0';
    }
    return used > 0;
}

static int wiki_page_text(const char *url, char *out, size_t cap)
{
    const char *p = find_ci(url, "wikipedia.org/wiki/");
    const char *host;
    char sub[48], title[600], addr[1024];
    char *json, *buf;
    const char *ex;
    size_t n, i;
    int ok = 0;

    if (!p)
        return 0;
    host = strstr(url, "://");
    host = host ? host + 3 : url;
    n = (size_t)(p - host);
    if (n == 0 || n >= sizeof(sub)) {
        snprintf(sub, sizeof(sub), "en");
    } else {
        memcpy(sub, host, n);
        sub[n] = '\0';
        while (n && sub[n - 1] == '.')
            sub[--n] = '\0';
        if (n >= 2 && strcmp(sub + n - 2, ".m") == 0)
            sub[n - 2] = '\0';
        if (!sub[0])
            snprintf(sub, sizeof(sub), "en");
    }

    p += strlen("wikipedia.org/wiki/");
    for (i = 0; p[i] && p[i] != '#' && p[i] != '?' && i < sizeof(title) - 1; i++)
        title[i] = p[i];
    title[i] = '\0';
    if (!title[0])
        return 0;

    snprintf(addr, sizeof(addr),
        "https://%s.wikipedia.org/w/api.php?action=query&prop=extracts&explaintext=1"
        "&redirects=1&titles=%s&format=json", sub, title);

    json = download_url(addr);
    if (!json)
        return 0;
    ex = strstr(json, "\"extract\":\"");
    buf = (char *)malloc(cap);
    if (ex && buf) {
        json_str_ex(ex + 11, buf, cap, 1);
        ok = wiki_text_to_blocks(buf, out, cap);
    }
    free(buf);
    free(json);
    return ok;
}

int web_fetch_page(const char *url, char *title, int title_size,
                   char *content, int content_size)
{
    char *html, *tmp;
    int attempt;
    size_t best = 0;

    if (title && title_size > 0)
        title[0] = '\0';
    if (content && content_size > 0)
        content[0] = '\0';
    if (!url || !content || content_size < 2)
        return 0;

    /* Wikipedia: take the clean article text directly (no menus, no language list) */
    if (wiki_page_text(url, content, (size_t)content_size) && strlen(content) >= 200) {
        polish_text(content);
        return 1;
    }
    content[0] = '\0';

    tmp = (char *)malloc((size_t)content_size);
    if (!tmp)
        return 0;

    /* 1) read the page ourselves */
    html = download_url(url);
    if (html) {
        if (title && title_size > 0) {
            const char *s = find_ci(html, "<title");
            const char *e = s ? strchr(s, '>') : NULL;

            if (e)
                clean_range(e + 1, find_ci(e + 1, "</title>"),
                            title, (size_t)title_size);
        }

        /* main article only, whole page, whole page with short lines kept */
        for (attempt = 0; attempt < 3; attempt++) {
            size_t len;
            html_to_blocks(html, attempt == 0, attempt == 2, tmp, (size_t)content_size);
            len = strlen(tmp);
            if (len > best) {
                memcpy(content, tmp, len + 1);
                best = len;
            }
            if (best >= 400)
                break;
        }
    }

    /* 2) the site blocked us or has almost no text: use a reader service
          (it opens the page like a real browser and returns clean text) */
    if (best < 400) {
        char *jurl = (char *)malloc(strlen(url) + 32);
        char *md = NULL;

        if (jurl) {
            snprintf(jurl, strlen(url) + 32, "https://r.jina.ai/%s", url);
            md = download_url(jurl);
            free(jurl);
        }
        if (md) {
            size_t len;

            if (title && title_size > 0 && !title[0]) {
                const char *t = strstr(md, "Title:");
                if (t && t < md + 200) {
                    const char *e = strchr(t, '\n');
                    t += 6;
                    while (*t == ' ')
                        t++;
                    clean_range(t, e ? e : t + strlen(t), title, (size_t)title_size);
                }
            }
            markdown_to_blocks(md, tmp, (size_t)content_size);
            len = strlen(tmp);
            if (len > best) {
                memcpy(content, tmp, len + 1);
                best = len;
            }
            free(md);
        }
    }

    /* 3) still nothing: use the page's own description */
    if (best < 60 && html) {
        char desc[1600];
        meta_description(html, desc, sizeof(desc));
        if (desc[0])
            snprintf(content, (size_t)content_size, "%s%s\n", best ? content : "", desc);
    }

    free(tmp);
    free(html);
    polish_text(content);
    return content[0] != '\0';
}