#ifndef WEB_H
#define WEB_H

#include <stddef.h>

#define MAX_WEB_RESULTS 15
#define MAX_WEB_TITLE 512
#define MAX_WEB_TEXT 2048
#define MAX_WEB_URL 2048

typedef struct {
    char title[MAX_WEB_TITLE];
    char snippet[MAX_WEB_TEXT];
    char url[MAX_WEB_URL];
} WebResult;

/* Web results (DuckDuckGo, falls back to Wikipedia so it never comes back empty) */
int web_search(const char *query, WebResult results[], int max_results);

/* Reads one page as organized text: lines starting with "# " are headings,
   "- " are bullets, everything else is a paragraph */
int web_fetch_page(const char *url, char *title, int title_size,
                   char *content, int content_size);

/* Quick checks used to hide results that have no information.
   web_is_junk_result: login walls / error pages, judged from title and address.
   web_page_check: 1 = real text, 0 = empty page, -1 = can't tell (keep it). */
int web_is_junk_result(const char *title, const char *url, const char *snippet);
int web_page_check(const char *url);

/* Short summary of the topic (a few sentences) */
int web_quick_answer(const char *query, char *title, int title_size,
                     char *text, int text_size);

/* Image links for the query. Returns how many were found. */
/* mode 0 = Wikimedia Commons photos, mode 1 = pictures of matching Wikipedia articles */
int web_image_search(const char *query, char urls[][MAX_WEB_URL], int max, int mode);

/* Downloads raw bytes (for images). Caller must free(). */
unsigned char *web_download(const char *url, size_t *len);

#endif