/* Phase 1 gate P1.6: the harness output must match an INDEPENDENT AES-256-GCM
   implementation byte-for-byte.

   test_cpu_baseline.c compares the threaded harness against our own single-shot
   EVP call. That catches sharding bugs but not a shared misuse of EVP -- both
   sides would be wrong together. This test shells out to the `openssl enc` CLI
   as a separate process, so agreement means two independent code paths agree.

   Default payload is 64 MiB to keep `ctest` fast. Set GPUSEAL_P16_BYTES to raise
   it; the gate table specifies 1 GiB (1073741824) for the recorded run, which
   needs roughly 3 GiB of free RAM plus 2 GiB of scratch disk.

   Exits 77 (CTest "skipped") when the `openssl` CLI is absent or too old to
   support -aead style GCM via `enc`.

   ponytail: uses temp files and popen rather than a pipe-fed subprocess, because
   `openssl enc` will not stream GCM output without buffering the tag anyway.
   Upgrade path: link libcrypto twice against different versions if you ever need
   to compare implementations rather than processes. */

#include "gpuseal/cpu_baseline.h"
#include "gpuseal/test_vectors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP_EXIT 77
#define DEFAULT_BYTES (64u << 20)

#if defined(_WIN32)
#define POPEN  _popen
#define PCLOSE _pclose
#define DEVNULL "NUL"
#else
#define POPEN  popen
#define PCLOSE pclose
#define DEVNULL "/dev/null"
#endif

static int failures = 0;

#define CHECK(cond, ...) do {                                  \
    if (!(cond)) {                                             \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);   \
        fprintf(stderr, __VA_ARGS__);                          \
        fprintf(stderr, "\n");                                 \
        failures++;                                            \
    }                                                          \
} while (0)

/* Removes a file, ignoring "was not there" so cleanup paths stay simple. */
static void unlink_quiet(const char *path)
{
    (void)remove(path);
}

static int openssl_cli_present(void)
{
    FILE *p = POPEN("openssl version", "r");
    if (!p) { return 0; }
    char line[256] = {0};
    const int got = fgets(line, sizeof(line), p) != NULL;
    const int rc = PCLOSE(p);
    if (!got || rc != 0) { return 0; }
    printf("external oracle: %s", line);
    return 1;
}

/* `openssl enc` does not expose GCM in all builds; some refuse aes-256-gcm with
   "AEAD ciphers not supported". Probe once so a refusal reads as SKIP, not FAIL. */
static int openssl_cli_supports_gcm(void)
{
    FILE *p = POPEN("openssl enc -aes-256-gcm -list 2>&1", "r");
    if (!p) { return 0; }
    char buf[512] = {0};
    (void)fgets(buf, sizeof(buf), p);
    PCLOSE(p);
    /* A build lacking AEAD support says so on stderr, which we merged into stdout. */
    return strstr(buf, "not supported") == NULL && strstr(buf, "AEAD") == NULL;
}

static size_t payload_bytes(void)
{
    const char *env = getenv("GPUSEAL_P16_BYTES");
    if (!env) { return DEFAULT_BYTES; }
    char *end = NULL;
    const unsigned long long v = strtoull(env, &end, 10);
    if (!end || *end != '\0' || v < 16) {
        fprintf(stderr, "ignoring bad GPUSEAL_P16_BYTES=%s\n", env);
        return DEFAULT_BYTES;
    }
    return (size_t)v;
}

/* Writes n deterministic bytes so a failure is reproducible from the seed alone. */
static void fill_pattern(uint8_t *buf, size_t n)
{
    uint32_t x = 0x9E3779B9u;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        buf[i] = (uint8_t)x;
    }
}

static int write_file(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { return 0; }
    const size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    return w == n;
}

static uint8_t *read_file(const char *path, size_t *out_n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    const long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)len ? (size_t)len : 1);
    if (!buf) { fclose(f); return NULL; }
    const size_t r = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (r != (size_t)len) { free(buf); return NULL; }
    *out_n = r;
    return buf;
}

int main(void)
{
    if (!openssl_cli_present()) {
        printf("SKIP: `openssl` CLI not on PATH; P1.6 needs an external oracle\n");
        return SKIP_EXIT;
    }
    if (!openssl_cli_supports_gcm()) {
        printf("SKIP: this `openssl enc` build refuses AEAD/GCM ciphers\n");
        return SKIP_EXIT;
    }

    const size_t n = payload_bytes();
    printf("P1.6 payload: %zu bytes (set GPUSEAL_P16_BYTES to change; "
           "gate table specifies 1073741824)\n", n);

    uint8_t key[GPUSEAL_AES256_KEY_BYTES];
    uint8_t iv[GPUSEAL_GCM_IV_BYTES];
    for (size_t i = 0; i < sizeof(key); i++) { key[i] = (uint8_t)(i * 7 + 3); }
    for (size_t i = 0; i < sizeof(iv); i++)  { iv[i]  = (uint8_t)(i * 5 + 1); }

    char key_hex[GPUSEAL_AES256_KEY_BYTES * 2 + 1];
    char iv_hex[GPUSEAL_GCM_IV_BYTES * 2 + 1];
    gpuseal_hex_encode(key, sizeof(key), key_hex);
    gpuseal_hex_encode(iv, sizeof(iv), iv_hex);

    uint8_t *pt = (uint8_t *)malloc(n);
    uint8_t *ours = (uint8_t *)malloc(n);
    if (!pt || !ours) {
        fprintf(stderr, "FAIL: cannot allocate 2 x %zu bytes\n", n);
        free(pt); free(ours);
        return 1;
    }
    fill_pattern(pt, n);

    const char *pt_path = "p16_plain.bin";
    const char *ct_path = "p16_cli.bin";

    uint8_t *theirs = NULL;
    size_t theirs_n = 0;

    if (!write_file(pt_path, pt, n)) {
        fprintf(stderr, "FAIL: cannot write %s\n", pt_path);
        failures++;
        goto cleanup;
    }

    /* Our side. Single-shot, so this also pins the oracle used by P1.7. */
    uint8_t tag[GPUSEAL_GCM_TAG_BYTES];
    const gpuseal_status st =
        gpuseal_cpu_gcm_encrypt(key, iv, NULL, 0, pt, n, ours, tag);
    CHECK(st == GPUSEAL_OK, "our encrypt returned %s", gpuseal_strerror(st));
    if (st != GPUSEAL_OK) { goto cleanup; }

    /* Their side. -K/-iv take hex; -nopad because GCM is a stream cipher mode.
       The CLI appends nothing to the ciphertext body -- the tag is separate --
       so the file should be exactly n bytes. */
    {
        char cmd[1024];
        const int written = snprintf(cmd, sizeof(cmd),
            "openssl enc -aes-256-gcm -K %s -iv %s -nopad -in %s -out %s 2>%s",
            key_hex, iv_hex, pt_path, ct_path, DEVNULL);
        CHECK(written > 0 && (size_t)written < sizeof(cmd), "command truncated");
        if (written <= 0 || (size_t)written >= sizeof(cmd)) { goto cleanup; }

        const int rc = system(cmd);
        if (rc != 0) {
            printf("SKIP: `openssl enc` exited %d; this build cannot drive "
                   "raw-key GCM from the CLI\n", rc);
            free(pt); free(ours);
            unlink_quiet(pt_path);
            unlink_quiet(ct_path);
            return SKIP_EXIT;
        }
    }

    theirs = read_file(ct_path, &theirs_n);
    CHECK(theirs != NULL, "cannot read %s", ct_path);
    if (!theirs) { goto cleanup; }

    CHECK(theirs_n == n, "CLI ciphertext is %zu bytes, expected %zu", theirs_n, n);
    if (theirs_n == n) {
        const int same = memcmp(ours, theirs, n) == 0;
        CHECK(same, "ciphertext differs from `openssl enc` output");
        if (!same) {
            /* Report the first divergence: a length-1 mismatch and a total
               mismatch have very different causes. */
            for (size_t i = 0; i < n; i++) {
                if (ours[i] != theirs[i]) {
                    fprintf(stderr, "  first difference at byte %zu: "
                                    "ours=0x%02x theirs=0x%02x\n",
                            i, ours[i], theirs[i]);
                    break;
                }
            }
        }
    }

cleanup:
    free(pt);
    free(ours);
    free(theirs);
    unlink_quiet(pt_path);
    unlink_quiet(ct_path);

    if (failures) {
        fprintf(stderr, "\nP1.6 FAILED with %d failure(s)\n", failures);
        return 1;
    }
    printf("PASS P1.6: %zu bytes match `openssl enc` byte-for-byte\n", n);
    return 0;
}
