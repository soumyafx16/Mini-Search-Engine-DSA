#define _CRT_SECURE_NO_WARNINGS

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "index.h"

static const char *STOP[] = {
    "the", "and", "for", "are", "with", "that", "this", "from", "you",
    "was", "have", "not", "but", "all", "can", "its", "his", "her",
    "our", "has", "had", "who", "how", "what", "when", "where", "which"
};

static int is_stop(const char *w)
{
    size_t i;
    for (i = 0; i < sizeof(STOP) / sizeof(STOP[0]); i++)
        if (strcmp(w, STOP[i]) == 0)
            return 1;
    return 0;
}

/* djb2 hash */
static unsigned hash_word(const char *s)
{
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h % BUCKETS;
}

/* Reads the next lowercase word from p into w. Returns pointer after it, or NULL. */
static const char *next_word(const char *p, char *w, int size)
{
    int n = 0;

    while (*p && !isalnum((unsigned char)*p))
        p++;
    if (!*p)
        return NULL;

    while (isalnum((unsigned char)*p)) {
        if (n + 1 < size)
            w[n++] = (char)tolower((unsigned char)*p);
        p++;
    }
    w[n] = '\0';
    return p;
}

static Term *find_term(Index *ix, const char *w)
{
    Term *t = ix->buckets[hash_word(w)];
    while (t && strcmp(t->word, w) != 0)
        t = t->next;
    return t;
}

void index_init(Index *ix)
{
    memset(ix, 0, sizeof(*ix));
}

void index_free(Index *ix)
{
    int i;
    for (i = 0; i < BUCKETS; i++) {
        Term *t = ix->buckets[i];
        while (t) {
            Term *nt = t->next;
            Posting *po = t->head;
            while (po) {
                Posting *np = po->next;
                free(po);
                po = np;
            }
            free(t->word);
            free(t);
            t = nt;
        }
    }
    memset(ix, 0, sizeof(*ix));
}

/* Add one page. Pages must be added one at a time (doc 0, 1, 2...). */
void index_add(Index *ix, int doc, const char *text)
{
    char w[64];
    const char *p = text;

    if (doc < 0 || doc >= MAX_DOCS)
        return;
    if (doc >= ix->ndocs)
        ix->ndocs = doc + 1;

    while ((p = next_word(p, w, sizeof(w))) != NULL) {
        unsigned h;
        Term *t;

        if (strlen(w) < 2 || is_stop(w))
            continue;

        ix->doclen[doc]++;
        h = hash_word(w);
        t = find_term(ix, w);

        if (!t) {
            t = (Term *)calloc(1, sizeof(Term));
            if (!t)
                return;
            t->word = _strdup(w);
            t->next = ix->buckets[h];
            ix->buckets[h] = t;
        }

        /* newest page is always at the head of the list */
        if (t->head && t->head->doc == doc) {
            t->head->count++;
        } else {
            Posting *po = (Posting *)malloc(sizeof(Posting));
            if (!po)
                return;
            po->doc = doc;
            po->count = 1;
            po->next = t->head;
            t->head = po;
            t->df++;
        }
    }
}

/* TF-IDF score of every page for the query */
void index_score(Index *ix, const char *query, double scores[])
{
    char w[64];
    const char *p = query;
    int i;

    for (i = 0; i < ix->ndocs; i++)
        scores[i] = 0.0;

    while ((p = next_word(p, w, sizeof(w))) != NULL) {
        Term *t;
        Posting *po;
        double idf;

        if (strlen(w) < 2 || is_stop(w))
            continue;

        t = find_term(ix, w);
        if (!t)
            continue;

        idf = log(1.0 + (double)ix->ndocs / t->df);

        for (po = t->head; po; po = po->next)
            scores[po->doc] +=
                ((double)po->count / (ix->doclen[po->doc] + 1)) * idf;
    }
}