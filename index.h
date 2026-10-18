#ifndef INDEX_H
#define INDEX_H

#define MAX_DOCS 16
#define BUCKETS 2048

/* One page that contains a word, and how many times */
typedef struct Posting {
    int doc;
    int count;
    struct Posting *next;
} Posting;

/* One word in the hash table (chained with a linked list) */
typedef struct Term {
    char *word;
    int df;              /* in how many pages the word appears */
    Posting *head;       /* linked list of pages */
    struct Term *next;   /* next word in the same bucket */
} Term;

typedef struct {
    Term *buckets[BUCKETS];
    int ndocs;
    int doclen[MAX_DOCS];
} Index;

void index_init(Index *ix);
void index_free(Index *ix);
void index_add(Index *ix, int doc, const char *text);
void index_score(Index *ix, const char *query, double scores[]);

#endif