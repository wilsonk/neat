#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct String
{
    size_t length;
    char *ptr;
    void *base;
};

struct StringArray
{
    size_t length;
    struct String *ptr;
    void *base;
};

struct String string_alloc(size_t length) {
    void *memory = malloc(sizeof(size_t) + length + 1);
    *(size_t*) memory = 1; // references
    return (struct String) { length, memory + sizeof(size_t), memory };
}

void print(struct String str) { printf("%.*s\n", (int) str.length, str.ptr); }
void assert(int test) {
    if (!test) {
        fprintf(stderr, "Assertion failed! Aborting.\n");
        exit(1);
    }
}
int cxruntime_ptr_test(void* ptr) { return !!ptr; }
int _arraycmp(void* a, void* b, size_t la, size_t lb, size_t sz) {
    if (la != lb) return 0;
    return memcmp(a, b, la * sz) == 0;
}
static char* toStringz(struct String str) {
    char *buffer = malloc(str.length + 1);
    strncpy(buffer, str.ptr, str.length);
    buffer[str.length] = 0;
    return buffer;
}
int cxruntime_atoi(struct String str) {
    char *temp = toStringz(str);
    int res = atoi(temp);
    free(temp);
    // printf("atoi(%.*s) = %i\n", str.length, str.ptr, res);
    return res;
}
float cxruntime_atof(struct String str) {
    char *temp = toStringz(str);
    float res = atof(temp);
    free(temp);
    // printf("atof(%.*s) = %f\n", str.length, str.ptr, res);
    return res;
}
struct String cxruntime_itoa(int i) {
    int len = snprintf(NULL, 0, "%i", i);
    struct String res = string_alloc(len);
    snprintf(res.ptr, len + 1, "%i", i);
    return res;
}
struct String cxruntime_ltoa(long long l) {
    int len = snprintf(NULL, 0, "%lld", l);
    struct String res = string_alloc(len);
    snprintf(res.ptr, len + 1, "%lld", l);
    return res;
}
struct String cxruntime_ftoa(float f) {
    int len = snprintf(NULL, 0, "%f", f);
    struct String res = string_alloc(len);
    snprintf(res.ptr, len + 1, "%f", f);
    return res;
}
struct String cxruntime_ftoa_hex(float f) {
    double d = f;
    int len = snprintf(NULL, 0, "%llx", *(long long int*) &d);
    struct String res = string_alloc(len);
    snprintf(res.ptr, len + 1, "%llx", *(long long int*) &d);
    return res;
}
struct String cxruntime_ptr_id(void* ptr) {
    int len = snprintf(NULL, 0, "%p", ptr);
    struct String res = string_alloc(len);
    snprintf(res.ptr, len + 1, "%p", ptr);
    return res;
}
int cxruntime_toInt(float f) { return (int) f; }

int cxruntime_linenr(struct String haystack, struct String needle, int* linep, int* columnp) {
    if (needle.ptr < haystack.ptr && needle.ptr >= haystack.ptr + haystack.length)
        return false;
    size_t lineStart = 0, lineEnd = 0, lineNr = 0;
    while (lineStart < haystack.length)
    {
        while (lineEnd < haystack.length && haystack.ptr[lineEnd] != '\n') lineEnd++;
        if (lineEnd < haystack.length && haystack.ptr[lineEnd] == '\n') lineEnd++;
        struct String line = { lineEnd - lineStart, haystack.ptr + lineStart };

        if (needle.ptr >= line.ptr && needle.ptr <= line.ptr + line.length)
        {
            *linep = (int) lineNr;
            *columnp = (int) (needle.ptr - line.ptr - 1);
            return true;
        }
        lineNr++;
        lineStart = lineEnd;
    }
    abort();
}
int cxruntime_isAlpha(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}
int cxruntime_isDigit(char ch) {
    return ch >= '0' && ch <= '9';
}
int cxruntime_file_exists(struct String file) {
    char *fn = toStringz(file);
    int ret = access(fn, F_OK) != -1;
    free(fn);
    return ret;
}
struct String cxruntime_file_read(struct String file) {
    // thanks,
    // https://stackoverflow.com/questions/14002954/c-programming-how-to-read-the-whole-file-contents-into-a-buffer
    char *fn = toStringz(file);
    FILE *f = fopen(fn, "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);  /* same as rewind(f); */

    char *buffer = malloc(fsize);
    fread(buffer, 1, fsize, f);
    fclose(f);
    free(fn);

    return (struct String) { fsize, buffer };
}

void cxruntime_file_write(struct String file, struct String content) {
    char *fn = toStringz(file);
    FILE *f = fopen(fn, "wb");
    fwrite(content.ptr, 1, content.length, f);
    fclose(f);
    free(fn);
}

void cxruntime_system(struct String command) {
    char *cmd = toStringz(command);
    int ret = system(cmd);
    if (ret != 0) fprintf(stderr, "command failed with %i\n", ret);
    assert(ret == 0);
    free(cmd);
}

int cxruntime_execbg(struct String command, struct StringArray arguments) {
    int ret = fork();
    if (ret != 0) return ret;
    char *cmd = toStringz(command);
    char **args = malloc(sizeof(char*) * (arguments.length + 2));
    args[0] = cmd;
    for (int i = 0; i < arguments.length; i++) {
        args[1 + i] = toStringz(arguments.ptr[i]);
    }
    args[1 + arguments.length] = NULL;
    return execvp(cmd, args);
}

bool cxruntime_waitpid(int pid) {
    int wstatus;
    int ret = waitpid(pid, &wstatus, 0);
    if (ret == -1) fprintf(stderr, "waitpid() failed: %s\n", strerror(errno));
    return WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;
}

// No idea why this is necessary.
__attribute__((optnone))
bool cxruntime_symbol_defined_in_main(struct String symbol) {
    // even if a DL is loaded with RTLD_GLOBAL, main symbols are special.
    // so we want to avoid redefining symbols that are in the main program.
    void *main = dlopen(NULL, RTLD_LAZY);
    char *symbolPtr = toStringz(symbol);
    void *sym = dlsym(main, symbolPtr);
    free(symbolPtr);
    dlclose(main);
    return sym ? true : false;
}

void cxruntime_dlcall(struct String dlfile, struct String fun, void* arg) {
    void *handle = dlopen(toStringz(dlfile), RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) fprintf(stderr, "can't open %.*s - %s\n", (int) dlfile.length, dlfile.ptr, dlerror());
    assert(!!handle);
    void *sym = dlsym(handle, toStringz(fun));
    if (!sym) fprintf(stderr, "can't load symbol '%.*s'\n", (int) fun.length, fun.ptr);
    assert(!!sym);

    ((void(*)(void*)) sym)(arg);
}

void *cxruntime_alloc(size_t size) {
    return calloc(1, size);
}

void _main(struct StringArray args);

int main(int argc, char **argv) {
    struct StringArray args = (struct StringArray) {
        argc - 1,
        malloc(sizeof(struct String) * (argc - 1))
    };
    for (int i = 0; i < argc - 1; i++) {
        args.ptr[i] = (struct String) { strlen(argv[i + 1]), argv[i + 1] };
    }
    _main(args);
    free(args.ptr);
    return 0;
}

//
// fnv hash
//

typedef long long int FNVState;

void *fnv_init()
{
    void *ret = malloc(sizeof(FNVState));
    *(long long int*) ret = 14695981039346656037UL; // offset basis
    return ret;
}

void fnv_add_string(void *state, struct String s)
{
#define HASH (*(long long int*) state)
    for (int i = 0; i < s.length; i++) {
        HASH = HASH ^ s.ptr[i];
        HASH = HASH * 1099511628211;
    }
#undef HASH
}

void fnv_add_long(void *state, long long int value)
{
#define HASH (*(long long int*) state)
    for (int i = 0; i < sizeof(long long int); i++) {
        HASH = HASH ^ (value & 0xff);
        HASH = HASH * 1099511628211;
        value >>= 8;
    }
#undef HASH
}

struct String fnv_hex_value(void *state)
{
    char *ptr = malloc(sizeof(FNVState) + 1);
    snprintf(ptr, sizeof(FNVState) + 1, "%.*llX", (int) sizeof(FNVState), *(long long int*) state);
    return (struct String) { .length = sizeof(FNVState), .ptr = ptr };
}

//
// polynomial hash
//

#define PRIME 1099511628211

typedef struct {
    long long add, mult;
} PolyHashState;

PolyHashState *poly_init()
{
    // hopefully copied from fnv
    PolyHashState *result = malloc(sizeof(PolyHashState));
    *result = (PolyHashState) {
        .add = 14695981039346656037UL, // offset basis
        .mult = 1,
    };
    return result;
}

void poly_apply_hash(PolyHashState *left, PolyHashState *right)
{
    left->add = left->add * right->mult + right->add;
    left->mult *= right->mult;
}

PolyHashState poly_hash_string(struct String s)
{
    // in a polynomial hash, we apply a string by computing h * p^(s.length) + ((s[0]*p + s[1])*p + s[2])*p...
    // iow, h * p^(s.length) + s[0] * p^(s.length - 1) + s[1] * p^(s.length - 2) + ...
    // p^(s.length) can be efficiently determined by counting along
    PolyHashState result = (PolyHashState) { .add = 0, .mult = 1 };
    // INVERSE index cause we're counting up factors
    for (size_t i = 0; i < s.length; i++) {
        result.add += s.ptr[s.length - 1 - i] * result.mult;
        result.mult *= PRIME;
    }
    return result;
}

void poly_add_string(PolyHashState *state, struct String s)
{
    PolyHashState right = poly_hash_string(s);
    poly_apply_hash(state, &right);
}

PolyHashState poly_hash_long(long long int value)
{
    PolyHashState result = (PolyHashState) { .add = 0, .mult = 1 };
    for (size_t i = 0; i < sizeof(long long int); i++) {
        result.add += ((value >> (8 * i)) & 0xff) * result.mult;
        result.mult *= PRIME;
    }
    return result;
}

void poly_add_long(void *state, long long int value)
{
    PolyHashState right = poly_hash_long(value);
    poly_apply_hash(state, &right);
}

#undef PRIME

struct String poly_hex_value(PolyHashState *state)
{
    char *ptr = malloc(sizeof(state->add) + 1);
    snprintf(ptr, sizeof(state->add) + 1, "%.*llX", (int) sizeof(state->add), state->add);
    return (struct String) { .length = sizeof(state->add), .ptr = ptr };
}

long long int poly_hash_whole_string(struct String s)
{
    PolyHashState state = {
        .add = 14695981039346656037UL, // offset basis
        .mult = 1,
    };
    poly_add_string(&state, s);
    return state.add;
}

// for debug breaks
void debug() { }

void print_backtrace()
{
    void *array[20];
    int size = backtrace(array, 20);
    char **strings = backtrace_symbols(array, size);
    if (strings != NULL) {
        printf("Backtrace:\n");
        for (int i = 0; i < size; i++)
        printf("  %i: %s\n", i, strings[i]);
    }
    free(strings);
}

void cxruntime_refcount_violation(struct String s, void *ptr)
{
    printf("<%.*s: refcount logic violated: %lld at %p\n", (int) s.length, s.ptr, *(long long int*) ptr, ptr);
    print_backtrace();
}

struct Cache
{
    size_t length;
    void **entries;
};

__thread struct Cache cxruntime_cache = {0};

int cxruntime_cache_isset(int key)
{
    if (key >= cxruntime_cache.length)
        return false;
    return cxruntime_cache.entries[key] != NULL;
}

void *cxruntime_cache_get(int key)
{
    return cxruntime_cache.entries[key];
}

void cxruntime_cache_set(int key, void *ptr)
{
    assert(ptr != NULL);
    if (key >= cxruntime_cache.length) {
        size_t oldlen = cxruntime_cache.length;
        size_t newlen = key + 1;
        cxruntime_cache.entries = realloc(cxruntime_cache.entries, sizeof(void*) * newlen);
        memset(cxruntime_cache.entries + oldlen, 0, sizeof(void*) * (newlen - oldlen));
        cxruntime_cache.length = newlen;
    }
    cxruntime_cache.entries[key] = ptr;
}
