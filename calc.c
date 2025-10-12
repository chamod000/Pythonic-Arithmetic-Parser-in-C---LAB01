// Chamod Chirantha 000000
// (Replace with your real Name Lastname StudentID)
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c

static const char *STUDENT_FIRST   = "Chamod";   // your first name
static const char *STUDENT_LAST    = "Chirantha";   // your last name
static const char *STUDENT_ID      = "000000";   // your student ID
static const char *SYSTEM_USERNAME = "CHAMOD CHIRANTHA";   // your OS username (for default folder name)
------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef _WIN32
#include <unistd.h>
#endif

// ---------- Small utilities ----------

static int ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lt = strlen(suffix);
    return ls >= lt && strcmp(s + (ls - lt), suffix) == 0;
}

static const char *path_basename(const char *path) {
    const char *p = strrchr(path, '/');
#ifdef _WIN32
    const char *q = strrchr(path, '\\');
    if (!p || (q && q > p)) p = q;
#endif
    return p ? p + 1 : path;
}

static void strip_extension(char *s) {
    char *dot = strrchr(s, '.');
    if (dot) *dot = '\0';
}

static int mkpath(const char *path) {
    // POSIX mkdir returns -1 if exists; treat as success if EEXIST
#ifdef _WIN32
    int rc = _mkdir(path);
#else
    int rc = mkdir(path, 0775);
#endif
    if (rc == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

static char *join3(const char *a, const char *b, const char *c) {
    size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
    char *out = (char *)malloc(la + lb + lc + 1);
    if (!out) return NULL;
    memcpy(out, a, la);
    memcpy(out + la, b, lb);
    memcpy(out + la + lb, c, lc);
    out[la + lb + lc] = '\0';
    return out;
}

static char *join_path3(const char *a, const char *b, const char *c) {
    // returns a + '/' + b + '/' + c (allocates)
    const char *sep = "/";
    size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
    char *out = (char *)malloc(la + 1 + lb + 1 + lc + 1);
    if (!out) return NULL;
    sprintf(out, "%s/%s/%s", a, b, c);
    return out;
}

static char *read_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    return buf;
}

// ---------- Tokenizer ----------

typedef enum {
    T_NUM, T_PLUS, T_MINUS, T_STAR, T_SLASH, T_POW, T_LPAREN, T_RPAREN, T_END
} TokenType;

typedef struct {
    TokenType type;
    double    num;   // valid if type==T_NUM
    size_t    pos;   // 1-based char index in whole file (start of token)
} Token;

typedef struct {
    Token *data; int count; int cap;
} TokenVec;

static int tv_push(TokenVec *tv, Token t) {
    if (tv->count == tv->cap) {
        int ncap = tv->cap ? tv->cap * 2 : 64;
        Token *nd = (Token *)realloc(tv->data, (size_t)ncap * sizeof(Token));
        if (!nd) return -1;
        tv->data = nd; tv->cap = ncap;
    }
    tv->data[tv->count++] = t; return 0;
}

typedef struct {
    size_t error_pos; // 0 if no error
} ScanError;

static void scan_tokens(const char *src, size_t len, TokenVec *out, ScanError *err) {
    size_t i = 0; size_t pos = 1; int at_line_start = 1;
    err->error_pos = 0;

    while (i < len) {
        // Handle line-start "#" comments (first non-space '#')
        if (at_line_start) {
            size_t j = i; size_t p2 = pos;
            while (j < len && (src[j] == ' ' || src[j] == '\t' || src[j] == '\r')) { j++; p2++; }
            if (j < len && src[j] == '#') {
                // skip to end of line (but keep counting positions)
                while (j < len && src[j] != '\n') { j++; p2++; }
                i = j; pos = p2; // newline (if any) handled below
            } else {
                at_line_start = 0;
            }
        }

        if (i >= len) break;
        char c = src[i];

        if (c == ' ' || c == '\t' || c == '\r') { i++; pos++; continue; }
        if (c == '\n') { i++; pos++; at_line_start = 1; continue; }

        // Number: digit or '.' followed by digit -> use strtod
        if (isdigit((unsigned char)c) || c == '.') {
            char *endp = NULL;
            errno = 0;
            double v = strtod(&((char*)src)[i], &endp);
            size_t consumed = (size_t)(endp - &src[i]);
            if (consumed == 0) {
                // e.g., lone '.' not a number
                if (!err->error_pos) err->error_pos = pos;
                return;
            }
            size_t start_pos = pos;
            i += consumed; pos += consumed;
            Token t = { T_NUM, v, start_pos };
            if (tv_push(out, t) != 0) { if (!err->error_pos) err->error_pos = start_pos; return; }
            continue;
        }

        // Operators and parentheses
        if (c == '+') { Token t = { T_PLUS, 0, pos }; tv_push(out, t); i++; pos++; continue; }
        if (c == '-') { Token t = { T_MINUS,0, pos }; tv_push(out, t); i++; pos++; continue; }
        if (c == '*') {
            size_t start_pos = pos;
            if (i + 1 < len && src[i+1] == '*') {
                Token t = { T_POW, 0, start_pos }; tv_push(out, t); i += 2; pos += 2; continue;
            } else {
                Token t = { T_STAR,0, start_pos }; tv_push(out, t); i++; pos++; continue;
            }
        }
        if (c == '/') { Token t = { T_SLASH,0, pos }; tv_push(out, t); i++; pos++; continue; }
        if (c == '(') { Token t = { T_LPAREN,0, pos }; tv_push(out, t); i++; pos++; continue; }
        if (c == ')') { Token t = { T_RPAREN,0, pos }; tv_push(out, t); i++; pos++; continue; }

        // Invalid character
        if (!err->error_pos) err->error_pos = pos;
        return;
    }

    Token endt = { T_END, 0, pos }; tv_push(out, endt);
}

// ---------- Parser / Evaluator (recursive descent) ----------

typedef struct {
    Token *toks; int idx; int n;
    size_t error_pos; // first error position (1-based); 0 if none
} Parser;

static Token *peek(Parser *p) { return &p->toks[p->idx]; }
static Token *advance(Parser *p) { if (p->idx < p->n-1) p->idx++; return &p->toks[p->idx-1]; }
static int match(Parser *p, TokenType tt, size_t *op_pos_out) {
    if (peek(p)->type == tt) { if (op_pos_out) *op_pos_out = peek(p)->pos; advance(p); return 1; } return 0;
}
static void fail(Parser *p, size_t pos) { if (p->error_pos == 0) p->error_pos = pos; }

static double parse_expr(Parser *p); // fwd
static double parse_term(Parser *p);
static double parse_power(Parser *p);
static double parse_unary(Parser *p);
static double parse_primary(Parser *p);

static double parse_expr(Parser *p) {
    double v = parse_term(p);
    while (p->error_pos == 0) {
        size_t op_pos = 0;
        if (match(p, T_PLUS, &op_pos)) {
            double rhs = parse_term(p); if (p->error_pos) return 0; v += rhs; continue;
        }
        if (match(p, T_MINUS, &op_pos)) {
            double rhs = parse_term(p); if (p->error_pos) return 0; v -= rhs; continue;
        }
        break;
    }
    return v;
}

static double parse_term(Parser *p) {
    double v = parse_power(p);
    while (p->error_pos == 0) {
        size_t op_pos = 0;
        if (match(p, T_STAR, &op_pos)) {
            double rhs = parse_power(p); if (p->error_pos) return 0; v *= rhs; continue;
        }
        if (match(p, T_SLASH, &op_pos)) {
            double rhs = parse_power(p); if (p->error_pos) return 0;
            // Division-by-zero detection: report at the '/' operator position (documented choice)
            if (fabs(rhs) < 1e-15) { fail(p, op_pos); return 0; }
            v /= rhs; continue;
        }
        break;
    }
    return v;
}

static double parse_power(Parser *p) {
    // Right-associative: power := unary ('**' power)?
    double base = parse_unary(p);
    if (p->error_pos) return 0;
    size_t op_pos = 0;
    if (match(p, T_POW, &op_pos)) {
        double expv = parse_power(p);
        if (p->error_pos) return 0;
        // pow behavior mirrors C/Python; domain errors (e.g., negative base with fractional exponent) yield NaN; allowed per assignment.
        base = pow(base, expv);
    }
    return base;
}

static double parse_unary(Parser *p) {
    size_t op_pos = 0;
    if (match(p, T_PLUS, &op_pos)) {
        return parse_unary(p);
    }
    if (match(p, T_MINUS, &op_pos)) {
        return -parse_unary(p);
    }
    return parse_primary(p);
}

static double parse_primary(Parser *p) {
    Token *t = peek(p);
    if (t->type == T_NUM) { advance(p); return t->num; }
    if (t->type == T_LPAREN) {
        advance(p);
        double v = parse_expr(p);
        if (p->error_pos) return 0;
        if (peek(p)->type != T_RPAREN) {
            // unmatched '(' -> error at current token (where ')' expected); if EOF, this will be EOF position
            fail(p, peek(p)->pos);
            return 0;
        }
        advance(p); // consume ')'
        return v;
    }
    // Expected a number or '('; report error at this token start
    fail(p, t->pos);
    return 0;
}

// ---------- Evaluation entry point ----------

typedef struct {
    int ok;            // 1 if success, 0 if error
    double value;      // valid if ok
    size_t error_pos;  // valid if !ok
} EvalResult;

static EvalResult evaluate_text(const char *src, size_t len) {
    TokenVec tv = {0};
    ScanError se = {0};
    scan_tokens(src, len, &tv, &se);
    if (se.error_pos) {
        EvalResult er = {0, 0.0, se.error_pos};
        free(tv.data);
        return er;
    }

    Parser p = { tv.data, 0, tv.count, 0 };
    double val = parse_expr(&p);

    if (p.error_pos == 0) {
        // After parsing, ensure we've consumed everything (next must be T_END)
        if (peek(&p)->type != T_END) {
            // Unexpected trailing token -> error at its position
            p.error_pos = peek(&p)->pos;
        }
    }

    EvalResult er;
    if (p.error_pos) {
        er.ok = 0; er.value = 0.0; er.error_pos = p.error_pos;
    } else {
        er.ok = 1; er.value = val; er.error_pos = 0;
    }
    free(tv.data);
    return er;
}

// ---------- Output helpers ----------

static void print_value(FILE *f, double v) {
    // If integral (within 1e-12), print as integer; else with %.15g
    double r = nearbyint(v);
    if (fabs(v - r) < 1e-12) {
        long long ll = llround(v);
        fprintf(f, "%lld\n", ll);
    } else {
        fprintf(f, "%.15g\n", v);
    }
}

static int write_result_file(const char *outdir, const char *input_base_noext,
                             const char *first, const char *last, const char *student_id,
                             const EvalResult *er) {
    // Build filename: <base>_<name>_<lastname>_<studentid>.txt
    char namebuf[512];
    snprintf(namebuf, sizeof(namebuf), "%s_%s_%s_%s.txt", input_base_noext, first, last, student_id);

    char *outpath = join_path3(outdir, "", namebuf); // will create double '/', acceptable
    if (!outpath) return -1;

    // Normalize path (remove the duplicate '/') by rebuilding
    char fixed[1024];
    snprintf(fixed, sizeof(fixed), "%s/%s", outdir, namebuf);

    FILE *f = fopen(fixed, "wb");
    if (!f) { free(outpath); return -1; }
    if (er->ok) {
        print_value(f, er->value);
    } else {
        fprintf(f, "ERROR:%zu\n", er->error_pos);
    }
    fclose(f);
    free(outpath);
    return 0;
}

// ---------- CLI & batch processing ----------

static char *derive_default_outdir_from_path(const char *path_base_component) {
    // <base>_<username>_<studentid>
    char buf[512];
    snprintf(buf, sizeof(buf), "%s_%s_%s", path_base_component, SYSTEM_USERNAME, STUDENT_ID);
    return strdup(buf);
}

static int process_one_file(const char *inpath, const char *outdir) {
    size_t len = 0; char *text = read_all(inpath, &len);
    if (!text) { fprintf(stderr, "Failed to read %s\n", inpath); return -1; }
    EvalResult er = evaluate_text(text, len);

    // Determine base name without extension
    char basebuf[512];
    snprintf(basebuf, sizeof(basebuf), "%s", path_basename(inpath));
    strip_extension(basebuf);

    int rc = write_result_file(outdir, basebuf, STUDENT_FIRST, STUDENT_LAST, STUDENT_ID, &er);
    if (rc != 0) fprintf(stderr, "Failed to write result for %s\n", inpath);

    free(text);
    return rc;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [-d DIR|--dir DIR] [-o OUTDIR|--output-dir OUTDIR] [input.txt]\n", argv0);
}

int main(int argc, char **argv) {
    const char *dir_opt = NULL;
    const char *outdir_opt = NULL;
    const char *file_arg = NULL;

    // Minimal flag parser
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            dir_opt = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output-dir") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            outdir_opt = argv[++i];
        } else if (argv[i][0] == '-') {
            usage(argv[0]); return 1;
        } else {
            file_arg = argv[i];
        }
    }

    // Decide mode
    int dir_mode = (dir_opt != NULL);
    if (!dir_mode && !file_arg) { usage(argv[0]); return 1; }

    // Determine default outdir if not provided
    char *outdir = NULL; int free_outdir = 0;
    if (!outdir_opt) {
        if (dir_mode) {
            // default outdir from directory base name
            char basebuf[512]; snprintf(basebuf, sizeof(basebuf), "%s", path_basename(dir_opt));
            outdir = derive_default_outdir_from_path(basebuf); free_outdir = 1;
        } else {
            char basebuf[512]; snprintf(basebuf, sizeof(basebuf), "%s", path_basename(file_arg));
            strip_extension(basebuf);
            outdir = derive_default_outdir_from_path(basebuf); free_outdir = 1;
        }
    } else {
        outdir = (char *)outdir_opt; free_outdir = 0;
    }

    if (mkpath(outdir) != 0) {
        fprintf(stderr, "Could not create or access output dir: %s\n", outdir);
        if (free_outdir) free(outdir);
        return 1;
    }

    int rc = 0;
    if (dir_mode) {
        DIR *d = opendir(dir_opt);
        if (!d) { fprintf(stderr, "Cannot open directory: %s\n", dir_opt); if (free_outdir) free(outdir); return 1; }
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_type == DT_DIR) continue; // ignore subfolders
            if (!ends_with(ent->d_name, ".txt")) continue;
            // Build full path
            char full[1024]; snprintf(full, sizeof(full), "%s/%s", dir_opt, ent->d_name);
            if (process_one_file(full, outdir) != 0) rc = 1; // keep going even if one fails
        }
        closedir(d);
    } else {
        if (process_one_file(file_arg, outdir) != 0) rc = 1;
    }

    if (free_outdir) free(outdir);
    return rc;
}
