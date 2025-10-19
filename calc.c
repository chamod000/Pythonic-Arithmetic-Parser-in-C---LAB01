// Name Lastname StudentID
// GitHub repository: (optional link)
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <sys/types.h>

#ifndef _WIN32
#include <unistd.h>
#endif

// ---------- Small utilities ----------

// Check if string ends with suffix
static int ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lt = strlen(suffix);
    return ls >= lt && strcmp(s + (ls - lt), suffix) == 0;
}

// Get base name of a path
static const char *path_basename(const char *path) {
    const char *p = strrchr(path, '/');
#ifdef _WIN32
    const char *q = strrchr(path, '\\');
    if (!p || (q && q > p)) p = q;
#endif
    return p ? p + 1 : path;
}

// Get directory prefix (including trailing slash/backslash if present). Caller must free.
static char *path_dirprefix(const char *path) {
    const char *p = strrchr(path, '/');
#ifdef _WIN32
    const char *q = strrchr(path, '\\');
    if (!p || (q && q > p)) p = q;
#endif
    if (!p) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty; // no directory part
    }
    size_t dirlen = (size_t)(p - path + 1); // include the separator
    char *dir = (char *)malloc(dirlen + 1);
    if (!dir) return NULL;
    memcpy(dir, path, dirlen);
    dir[dirlen] = '\0';
    return dir;
}

// Strip the extension from a string (in place)
static void strip_extension(char *s) {
    char *dot = strrchr(s, '.');
    if (dot) *dot = '\0';
}

// Create a directory if it does not exist
static int mkpath(const char *path) {
#ifdef _WIN32
    int rc = _mkdir(path);
#else
    int rc = mkdir(path, 0775);
#endif
    if (rc == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

// Join three strings
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

// Read all file content into a buffer
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
    size_t    pos;   // 1-based char index in the input file
} Token;

typedef struct {
    Token *data; int count; int cap;
} TokenVec;

// Push a token to the token vector
static int tv_push(TokenVec *tv, Token t) {
    if (tv->count == tv->cap) {
        int ncap = tv->cap ? tv->cap * 2 : 64;
        Token *nd = (Token *)realloc(tv->data, (size_t)ncap * sizeof(Token));
        if (!nd) return -1;
        tv->data = nd; tv->cap = ncap;
    }
    tv->data[tv->count++] = t; return 0;
}

// Tokenize the input source
static void scan_tokens(const char *src, size_t len, TokenVec *out) {
    size_t i = 0; size_t pos = 1;
    while (i < len) {
        char c = src[i];

        // Skip whitespace
        if (isspace((unsigned char)c)) { i++; pos++; continue; }

        // Handle comments (to end of line)
        if (c == '#') {
            while (i < len && src[i] != '\n') { i++; pos++; }
            continue;
        }

        // Handle numbers
        if (isdigit((unsigned char)c) || c == '.') {
            char *endp = NULL;
            double v = strtod(&src[i], &endp);
            size_t consumed = (size_t)(endp - &src[i]);
            Token t = (Token){ T_NUM, v, pos };
            tv_push(out, t);
            i += consumed; pos += consumed;
            continue;
        }

        // Handle exponentiation (**) before single '*'
        if (c == '*' && (i + 1) < len && src[i + 1] == '*') {
            Token t = (Token){ T_POW, 0.0, pos };
            tv_push(out, t);
            i += 2; pos += 2;
            continue;
        }

        // Handle single-character operators and parens
        if (c == '+') { Token t = { T_PLUS, 0, pos }; tv_push(out, t); i++; pos++; continue; }
        if (c == '-') { Token t = { T_MINUS, 0, pos }; tv_push(out, t); i++; pos++; continue; }
        if (c == '*') { Token t = { T_STAR, 0, pos }; tv_push(out, t); i++; pos++; continue; }
        if (c == '/') { Token t = { T_SLASH, 0, pos }; tv_push(out, t); i++; pos++; continue; }
        if (c == '(') { Token t = { T_LPAREN, 0, pos }; tv_push(out, t); i++; pos++; continue; }
        if (c == ')') { Token t = { T_RPAREN, 0, pos }; tv_push(out, t); i++; pos++; continue; }

        // Invalid character
        fprintf(stderr, "ERROR: Invalid character at position %zu\n", pos);
        exit(1);
    }

    Token endt = { T_END, 0, pos };
    tv_push(out, endt);
}

// ---------- Parser ----------

typedef struct {
    Token *toks; int idx; int n;
    size_t error_pos; // first error position (1-based)
} Parser;

// Helper functions for parsing
static Token *peek(Parser *p) { return &p->toks[p->idx]; }
static Token *advance(Parser *p) { if (p->idx < p->n-1) p->idx++; return &p->toks[p->idx-1]; }
static int match(Parser *p, TokenType tt) {
    if (peek(p)->type == tt) { advance(p); return 1; } return 0;
}

// Forward decls
static double parse_expr(Parser *p);
static double parse_term(Parser *p);
static double parse_power(Parser *p);
static double parse_primary(Parser *p);

// Expression parsing: Addition/Subtraction
static double parse_expr(Parser *p) {
    double v = parse_term(p);
    while (p->error_pos == 0) {
        if (match(p, T_PLUS)) { v += parse_term(p); continue; }
        if (match(p, T_MINUS)) { v -= parse_term(p); continue; }
        break;
    }
    return v;
}

// Term parsing: Multiplication/Division
static double parse_term(Parser *p) {
    double v = parse_power(p);
    while (p->error_pos == 0) {
        if (match(p, T_STAR)) { v *= parse_power(p); continue; }
        if (match(p, T_SLASH)) {
            double rhs = parse_power(p);
            if (rhs == 0) {
                fprintf(stderr, "ERROR: Division by zero at position %zu\n", peek(p)->pos);
                exit(1);
            }
            v /= rhs; continue;
        }
        break;
    }
    return v;
}

// Power parsing: right-associative exponentiation (a ** b ** c => a ** (b ** c))
static double parse_power(Parser *p) {
    // To make it right-associative, parse a primary, and if '**' follows,
    // evaluate primary ** parse_power() rather than primary ** parse_primary()
    double base = parse_primary(p);
    if (match(p, T_POW)) {
        double exponent = parse_power(p);
        base = pow(base, exponent);
    }
    return base;
}

// Primary parsing: Handles numbers and parentheses
static double parse_primary(Parser *p) {
    Token *t = peek(p);
    if (t->type == T_NUM) { advance(p); return t->num; }
    if (t->type == T_LPAREN) {
        advance(p);
        double v = parse_expr(p);
        if (peek(p)->type != T_RPAREN) {
            fprintf(stderr, "ERROR: Expected ')' at position %zu\n", peek(p)->pos);
            exit(1);
        }
        advance(p);
        return v;
    }
    fprintf(stderr, "ERROR: Expected number or '(' at position %zu\n", peek(p)->pos);
    exit(1);
}

// ---------- Main Function ----------
int main(int argc, char **argv) {
    // Parsing command line arguments
    if (argc < 2) {
        printf("Usage: %s <input-file>\n", argv[0]);
        return 1;
    }
    const char *input_file = argv[1];
    size_t len = 0;
    char *src = read_all(input_file, &len);
    if (!src) {
        fprintf(stderr, "ERROR: Could not read input file: %s\n", input_file);
        return 1;
    }

    TokenVec tokens = {0};
    scan_tokens(src, len, &tokens);

    Parser parser = { tokens.data, 0, tokens.count, 0 };
    double result = parse_expr(&parser);

    // --- Write to output file with "<base>output.txt" in same directory ---
    char *dirprefix = path_dirprefix(input_file);
    const char *base = path_basename(input_file);

    char *base_copy = (char *)malloc(strlen(base) + 1);
    if (!base_copy || !dirprefix) {
        fprintf(stderr, "ERROR: Memory allocation failed\n");
        free(tokens.data);
        free(src);
        free(dirprefix);
        free(base_copy);
        return 1;
    }
    strcpy(base_copy, base);
    strip_extension(base_copy);  // "file1.txt" -> "file1"

    char *outfile = join3(dirprefix, base_copy, "output.txt");
    if (!outfile) {
        fprintf(stderr, "ERROR: Memory allocation failed\n");
        free(tokens.data);
        free(src);
        free(dirprefix);
        free(base_copy);
        return 1;
    }

    FILE *of = fopen(outfile, "w");
    if (!of) {
        fprintf(stderr, "ERROR: Could not open output file for writing: %s\n", outfile);
        free(tokens.data);
        free(src);
        free(dirprefix);
        free(base_copy);
        free(outfile);
        return 1;
    }

    // Write the result (same text as stdout, consistent with previous behavior)
    fprintf(of, "Result: %.15g\n", result);
    fclose(of);

    // Also print to console (optional for user feedback)
    printf("Result: %.15g\n", result);
    printf("Wrote output to: %s\n", outfile);

    // cleanup
    free(tokens.data);
    free(src);
    free(dirprefix);
    free(base_copy);
    free(outfile);

    return 0;
}
