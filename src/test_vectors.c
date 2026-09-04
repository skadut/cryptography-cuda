#include "gpuseal/test_vectors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int hexval(int c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

gpuseal_status gpuseal_hex_decode(const char *hex, uint8_t **out, size_t *out_len)
{
    if (!hex || !out || !out_len) { return GPUSEAL_ERR_ARG; }
    const size_t len = strlen(hex);
    if (len % 2 != 0) { return GPUSEAL_ERR_ARG; }

    const size_t n = len / 2;
    /* A zero-length field is legal in CAVP (empty PT or AAD). Represent it as a
       1-byte allocation so callers never see NULL for a valid parse. */
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1);
    if (!buf) { return GPUSEAL_ERR_ALLOC; }

    for (size_t i = 0; i < n; i++) {
        const int hi = hexval((unsigned char)hex[2 * i]);
        const int lo = hexval((unsigned char)hex[2 * i + 1]);
        if (hi < 0 || lo < 0) { free(buf); return GPUSEAL_ERR_ARG; }
        buf[i] = (uint8_t)((hi << 4) | lo);
    }
    *out = buf;
    *out_len = n;
    return GPUSEAL_OK;
}

void gpuseal_hex_encode(const uint8_t *buf, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = d[buf[i] >> 4];
        out[2 * i + 1] = d[buf[i] & 0x0f];
    }
    out[2 * n] = '\0';
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) { s++; }
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) { *--e = '\0'; }
    return s;
}

static gpuseal_status set_push(gpuseal_vector_set *s, const gpuseal_vector *v)
{
    if (s->n == s->cap) {
        const size_t cap = s->cap ? s->cap * 2 : 64;
        gpuseal_vector *p = (gpuseal_vector *)realloc(s->v, cap * sizeof(*p));
        if (!p) { return GPUSEAL_ERR_ALLOC; }
        s->v = p;
        s->cap = cap;
    }
    s->v[s->n++] = *v;
    return GPUSEAL_OK;
}

static void vector_free(gpuseal_vector *v)
{
    free(v->key); free(v->iv); free(v->pt); free(v->aad); free(v->ct); free(v->tag);
    memset(v, 0, sizeof(*v));
}

/* Assigns a hex field into the vector, replacing any previous value so a
   malformed file with duplicate keys cannot leak. */
static gpuseal_status assign(uint8_t **dst, size_t *dst_len, const char *hex)
{
    uint8_t *buf = NULL;
    size_t n = 0;
    const gpuseal_status st = gpuseal_hex_decode(hex, &buf, &n);
    if (st != GPUSEAL_OK) { return st; }
    free(*dst);
    *dst = buf;
    *dst_len = n;
    return GPUSEAL_OK;
}

gpuseal_status gpuseal_vectors_load(const char *path, gpuseal_vector_set *out)
{
    if (!path || !out) { return GPUSEAL_ERR_ARG; }
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "r");
    if (!f) { return GPUSEAL_ERR_IO; }

    gpuseal_status st = GPUSEAL_OK;
    gpuseal_vector cur;
    memset(&cur, 0, sizeof(cur));
    cur.count = -1;

    /* Section headers like [Keylen = 256] gate which vectors we keep. */
    int sect_keylen = 0, sect_ivlen = 0, sect_taglen = 0;
    int have_pending = 0;
    char line[16384];

    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#') { continue; }

        if (*p == '[') {
            char *close = strchr(p, ']');
            if (!close) { continue; }
            *close = '\0';
            char *body = trim(p + 1);
            char *eq = strchr(body, '=');
            if (!eq) { continue; }
            *eq = '\0';
            const char *k = trim(body);
            const int val = atoi(trim(eq + 1));
            if (strcmp(k, "Keylen") == 0)      { sect_keylen = val; }
            else if (strcmp(k, "IVlen") == 0)  { sect_ivlen = val; }
            else if (strcmp(k, "Taglen") == 0) { sect_taglen = val; }
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) {
            /* A bare FAIL line marks an expected-reject decrypt vector. */
            if (strcmp(p, "FAIL") == 0) { cur.fail = 1; have_pending = 1; }
            continue;
        }
        *eq = '\0';
        const char *key = trim(p);
        const char *val = trim(eq + 1);

        if (strcmp(key, "Count") == 0) {
            /* A new Count starts a record; flush the previous one. */
            if (have_pending) {
                if (sect_keylen == 256 && sect_ivlen == 96) {
                    cur.tag_len = cur.tag ? cur.tag_len : 0;
                    st = set_push(out, &cur);
                    if (st != GPUSEAL_OK) { vector_free(&cur); goto done; }
                } else {
                    vector_free(&cur);
                }
                memset(&cur, 0, sizeof(cur));
            }
            cur.count = atoi(val);
            have_pending = 1;
            continue;
        }

        if      (strcmp(key, "Key") == 0) { st = assign(&cur.key, &cur.key_len, val); }
        else if (strcmp(key, "IV")  == 0) { st = assign(&cur.iv,  &cur.iv_len,  val); }
        else if (strcmp(key, "PT")  == 0) { st = assign(&cur.pt,  &cur.pt_len,  val); }
        else if (strcmp(key, "AAD") == 0) { st = assign(&cur.aad, &cur.aad_len, val); }
        else if (strcmp(key, "CT")  == 0) { st = assign(&cur.ct,  &cur.ct_len,  val); }
        else if (strcmp(key, "Tag") == 0) { st = assign(&cur.tag, &cur.tag_len, val); }
        else { continue; }

        if (st != GPUSEAL_OK) { vector_free(&cur); goto done; }
        have_pending = 1;
    }

    if (have_pending) {
        if (sect_keylen == 256 && sect_ivlen == 96) {
            st = set_push(out, &cur);
            if (st != GPUSEAL_OK) { vector_free(&cur); goto done; }
        } else {
            vector_free(&cur);
        }
    }

    (void)sect_taglen;

done:
    fclose(f);
    if (st != GPUSEAL_OK) { gpuseal_vectors_free(out); }
    return st;
}

void gpuseal_vectors_free(gpuseal_vector_set *s)
{
    if (!s) { return; }
    for (size_t i = 0; i < s->n; i++) { vector_free(&s->v[i]); }
    free(s->v);
    memset(s, 0, sizeof(*s));
}
