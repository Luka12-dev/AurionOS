#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* String Operations */

/* Calculate string length */
size_t strlen(const char *str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

/* Copy string from src to dest */
char* strcpy(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *ptr = dest;
    while ((*ptr++ = *src++));
    return dest;
}

/* Copy at most n characters */
char* strncpy(char *dest, const char *src, size_t n) {
    if (!dest || !src) return dest;
    size_t i;
    for (i = 0; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

/* Compare two strings */
int strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return 0;
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/* Compare first n characters */
int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    return (n == 0) ? 0 : (*(unsigned char *)s1 - *(unsigned char *)s2);
}

/* Concatenate src to dest */
char* strcat(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *ptr = dest;
    while (*ptr) ptr++;
    while ((*ptr++ = *src++));
    return dest;
}

/* Concatenate at most n characters */
char* strncat(char *dest, const char *src, size_t n) {
    if (!dest || !src) return dest;
    char *ptr = dest;
    while (*ptr) ptr++;
    while (n-- > 0 && (*ptr++ = *src++));
    if (n == 0) *ptr = '\0';
    return dest;
}

/* Find character in string */
char* strchr(const char *str, int c) {
    if (!str) return NULL;
    while (*str) {
        if (*str == (char)c) return (char*)str;
        str++;
    }
    return (c == '\0') ? (char*)str : NULL;
}

/* Find last occurrence of character */
char* strrchr(const char *str, int c) {
    if (!str) return NULL;
    const char *last = NULL;
    while (*str) {
        if (*str == (char)c) last = str;
        str++;
    }
    return (c == '\0') ? (char*)str : (char*)last;
}

/* Find substring in string */
char* strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;
    
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

/* Reverse a string in place */
char* strrev(char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    if (len == 0) return str;
    
    char *start = str;
    char *end = str + len - 1;
    while (end > start) {
        char tmp = *start;
        *start++ = *end;
        *end-- = tmp;
    }
    return str;
}

/* Convert string to uppercase */
char* strupr(char *str) {
    if (!str) return NULL;
    char *ptr = str;
    while (*ptr) {
        if (*ptr >= 'a' && *ptr <= 'z') 
            *ptr -= 32;
        ptr++;
    }
    return str;
}

/* Convert string to lowercase */
char* strlwr(char *str) {
    if (!str) return NULL;
    char *ptr = str;
    while (*ptr) {
        if (*ptr >= 'A' && *ptr <= 'Z')
            *ptr += 32;
        ptr++;
    }
    return str;
}

/* Duplicate string (requires kmalloc) */
extern void* kmalloc(uint32_t size);
extern void kfree(void* ptr);

char* strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char *dup = (char*)kmalloc(len);
    if (dup) {
        strcpy(dup, str);
    }
    return dup;
}

/* Character Classification */

int isalpha(int c) {
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

int toupper(int c) {
    return islower(c) ? (c - 32) : c;
}

int tolower(int c) {
    return isupper(c) ? (c + 32) : c;
}

/* Conversion Functions */

/* Convert integer to string */
char* itoa(int32_t value, char *str, int base) {
    if (!str || base < 2 || base > 36) return NULL;
    
    char *ptr = str;
    int32_t num = value;
    int is_negative = 0;

    if (value == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return str;
    }

    if (value < 0 && base == 10) {
        is_negative = 1;
        num = -value;
    } else if (value < 0) {
        /* For non-decimal bases, treat as unsigned */
        num = (uint32_t)value;
    }

    while (num != 0) {
        int32_t rem = num % base;
        *ptr++ = (rem > 9) ? (rem - 10) + 'A' : rem + '0';
        num /= base;
    }

    if (is_negative) *ptr++ = '-';
    *ptr = '\0';

    return strrev(str);
}

/* Convert unsigned integer to string */
char* utoa(uint32_t value, char *str, int base) {
    if (!str || base < 2 || base > 36) return NULL;
    
    char *ptr = str;

    if (value == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return str;
    }

    while (value != 0) {
        uint32_t rem = value % base;
        *ptr++ = (rem > 9) ? (rem - 10) + 'A' : rem + '0';
        value /= base;
    }

    *ptr = '\0';
    return strrev(str);
}

/* Convert string to integer */
int32_t atoi(const char *str) {
    if (!str) return 0;
    
    int32_t result = 0;
    int sign = 1;
    
    /* Skip whitespace */
    while (isspace(*str)) str++;
    
    /* Handle sign */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    /* Convert digits */
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

/* Convert string to long */
int32_t atol(const char *str) {
    return atoi(str); /* Same as atoi for 32-bit system */
}

/* Convert hexadecimal string to integer */
uint32_t htoi(const char *str) {
    if (!str) return 0;
    
    uint32_t result = 0;
    
    /* Skip whitespace */
    while (isspace(*str)) str++;
    
    /* Skip 0x or 0X prefix */
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }
    
    while (*str) {
        char c = *str++;
        uint32_t value;
        
        if (c >= '0' && c <= '9')
            value = c - '0';
        else if (c >= 'a' && c <= 'f')
            value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            value = c - 'A' + 10;
        else
            break;
            
        result = (result << 4) | value;
    }
    
    return result;
}

/* Convert binary string to integer */
uint32_t btoi(const char *str) {
    if (!str) return 0;
    
    uint32_t result = 0;
    
    /* Skip whitespace */
    while (isspace(*str)) str++;
    
    /* Skip 0b or 0B prefix */
    if (str[0] == '0' && (str[1] == 'b' || str[1] == 'B')) {
        str += 2;
    }
    
    while (*str == '0' || *str == '1') {
        result = (result << 1) + (*str++ - '0');
    }
    
    return result;
}

/* Memory Operations */

/* Copy memory */
void* memcpy(void *dest, const void *src, size_t n) {
    if (!dest || !src) return dest;
    
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    
    while (n--) {
        *d++ = *s++;
    }
    
    return dest;
}

/* Copy memory (handles overlapping regions) */
void* memmove(void *dest, const void *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    
    if (d < s) {
        /* Copy forward */
        while (n--) {
            *d++ = *s++;
        }
    } else {
        /* Copy backward */
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    
    return dest;
}

/* Set memory */
void* memset(void *ptr, int value, size_t n) {
    if (!ptr) return ptr;
    
    uint8_t *p = (uint8_t*)ptr;
    uint8_t val = (uint8_t)value;
    
    while (n--) {
        *p++ = val;
    }
    
    return ptr;
}

/* Compare memory */
int memcmp(const void *s1, const void *s2, size_t n) {
    if (!s1 || !s2) return 0;
    
    const uint8_t *p1 = (const uint8_t*)s1;
    const uint8_t *p2 = (const uint8_t*)s2;
    
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    
    return 0;
}

/* Find byte in memory */
void* memchr(const void *ptr, int value, size_t n) {
    if (!ptr) return NULL;
    
    const uint8_t *p = (const uint8_t*)ptr;
    uint8_t val = (uint8_t)value;
    
    while (n--) {
        if (*p == val) {
            return (void*)p;
        }
        p++;
    }
    
    return NULL;
}

/* Math Functions */

/* Minimum of two integers */
int32_t min(int32_t a, int32_t b) {
    return (a < b) ? a : b;
}

/* Maximum of two integers */
int32_t max(int32_t a, int32_t b) {
    return (a > b) ? a : b;
}

/* Clamp value between min and max */
int32_t clamp(int32_t val, int32_t min_val, int32_t max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

/* Absolute value */
int32_t abs(int32_t value) {
    return (value < 0) ? -value : value;
}

/* Power function (integer) */
int32_t pow(int32_t base, int32_t exp) {
    if (exp < 0) return 0;
    if (exp == 0) return 1;
    
    int32_t result = 1;
    while (exp > 0) {
        if (exp & 1) {
            result *= base;
        }
        base *= base;
        exp >>= 1;
    }
    
    return result;
}

/* Square root (integer approximation) */
uint32_t sqrt(uint32_t n) {
    if (n == 0) return 0;
    if (n == 1) return 1;
    
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    
    return x;
}

/* Random Number Generation */

static uint32_t rng_seed = 1;

/* Seed the random number generator */
void srand(uint32_t seed) {
    rng_seed = seed;
}

/* Generate pseudo-random number (LCG algorithm) */
uint32_t rand(void) {
    rng_seed = rng_seed * 1103515245 + 12345;
    return (rng_seed / 65536) % 32768;
}

/* Generate random number in range [min, max] */
int32_t rand_range(int32_t min_val, int32_t max_val) {
    if (min_val >= max_val) return min_val;
    return min_val + (rand() % (max_val - min_val + 1));
}

/* Bit Manipulation */

/* Count set bits (population count) */
int popcount(uint32_t n) {
    int count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

/* Count leading zeros */
int clz(uint32_t n) {
    if (n == 0) return 32;
    int count = 0;
    if ((n & 0xFFFF0000) == 0) { count += 16; n <<= 16; }
    if ((n & 0xFF000000) == 0) { count += 8;  n <<= 8;  }
    if ((n & 0xF0000000) == 0) { count += 4;  n <<= 4;  }
    if ((n & 0xC0000000) == 0) { count += 2;  n <<= 2;  }
    if ((n & 0x80000000) == 0) { count += 1;  }
    return count;
}

/* Count trailing zeros */
int ctz(uint32_t n) {
    if (n == 0) return 32;
    int count = 0;
    while ((n & 1) == 0) {
        count++;
        n >>= 1;
    }
    return count;
}

/* Check if power of 2 */
bool is_power_of_2(uint32_t n) {
    return n && !(n & (n - 1));
}

/* Round up to next power of 2 */
uint32_t next_power_of_2(uint32_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/* Tokenization */

/* Tokenize string by delimiters (strtok replacement) */
static char *strtok_saveptr = NULL;

char* strtok(char *str, const char *delim) {
    if (str) {
        strtok_saveptr = str;
    }
    
    if (!strtok_saveptr || !delim) {
        return NULL;
    }
    
    /* Skip leading delimiters */
    while (*strtok_saveptr && strchr(delim, *strtok_saveptr)) {
        strtok_saveptr++;
    }
    
    if (!*strtok_saveptr) {
        strtok_saveptr = NULL;
        return NULL;
    }
    
    char *start = strtok_saveptr;
    
    /* Find next delimiter */
    while (*strtok_saveptr && !strchr(delim, *strtok_saveptr)) {
        strtok_saveptr++;
    }
    
    if (*strtok_saveptr) {
        *strtok_saveptr++ = '\0';
    } else {
        strtok_saveptr = NULL;
    }
    
    return start;
}

/* Reentrant version of strtok */
char* strtok_r(char *str, const char *delim, char **saveptr) {
    if (!saveptr || !delim) return NULL;
    
    if (str) {
        *saveptr = str;
    }
    
    if (!*saveptr) {
        return NULL;
    }
    
    /* Skip leading delimiters */
    while (**saveptr && strchr(delim, **saveptr)) {
        (*saveptr)++;
    }
    
    if (!**saveptr) {
        *saveptr = NULL;
        return NULL;
    }
    
    char *start = *saveptr;
    
    /* Find next delimiter */
    while (**saveptr && !strchr(delim, **saveptr)) {
        (*saveptr)++;
    }
    
    if (**saveptr) {
        *(*saveptr)++ = '\0';
    } else {
        *saveptr = NULL;
    }
    
    return start;
}

/* Utility Functions */

/* Swap two integers */
void swap(int32_t *a, int32_t *b) {
    if (!a || !b) return;
    int32_t temp = *a;
    *a = *b;
    *b = temp;
}

/* Reverse an array */
void reverse_array(int32_t *arr, size_t n) {
    if (!arr || n == 0) return;
    size_t i = 0;
    size_t j = n - 1;
    while (i < j) {
        swap(&arr[i], &arr[j]);
        i++;
        j--;
    }
}

/*  Sleep/Delay Functions */

/* Sleep for specified milliseconds - simple busy-wait */
void sleep_ms(uint32_t ms) {
    if (ms == 0) return;
    
    // Simple delay loop - approximately 1ms per iteration
    // This is a safe fallback that doesn't depend on system calls
    for (uint32_t i = 0; i < ms; i++) {
        // Approximate 1ms delay (adjust based on CPU speed)
        // ~1000 iterations per millisecond at typical speeds
        for (volatile uint32_t j = 0; j < 1000; j++) {
            __asm__ volatile("nop");
        }
    }
}

/* Keyboard input wrappers */
extern uint16_t sys_getkey(void);
extern int sys_kb_hit(void);
extern volatile int terminal_last_char;

int c_kb_hit(void) {
    if (terminal_last_char != 0) {
        return 1;
    }
    return sys_kb_hit();
}

uint16_t c_getkey(void) {
    if (terminal_last_char != 0) {
        uint16_t ret = (uint16_t)terminal_last_char;
        terminal_last_char = 0;
        return ret;
    }
    return sys_getkey();
}

uint16_t c_getkey_nonblock(void) {
    if (c_kb_hit()) {
        return c_getkey();
    }
    return 0;
}

/* Convert unsigned integer to decimal string */
void int_to_str(uint32_t n, char *buf) {
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[12];
    int i = 0;
    while (n > 0) {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}
