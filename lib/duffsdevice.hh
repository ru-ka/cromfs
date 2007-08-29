/* Note: These Duff's Devices don't make the assumption
 * that the loop count (n) is greater than 0.
 */

#if 0

/* These are tidier, but do not handle negative loop counts: */

#define DuffsDevice4(n, condition, code) do { \
    switch( (n) % 4 ) \
    { \
        case 0: while((condition)) { \
                   code; \
        case 3:    code; \
        case 2:    code; \
        case 1:    code; } \
    } } while(0)

#define DuffsDevice8(n, condition, code) do { \
    switch( (n) % 8 ) \
    { \
        case 0: while((condition)) { \
                   code; \
        case 7:    code; \
        case 6:    code; \
        case 5:    code; \
        case 4:    code; \
        case 3:    code; \
        case 2:    code; \
        case 1:    code; } \
    } } while(0)

#define DuffsDevice16(n, condition, code) do { \
    switch( (n) % 16 ) \
    { \
        case 0:  while((condition)) { \
                   code; \
        case 15:   code; \
        case 14:   code; \
        case 13:   code; \
        case 12:   code; \
        case 11:   code; \
        case 10:   code; \
        case 9:    code; \
        case 8:    code; \
        case 7:    code; \
        case 6:    code; \
        case 5:    code; \
        case 4:    code; \
        case 3:    code; \
        case 2:    code; \
        case 1:    code; } \
    } } while(0)

#else

/* These work just as specified. */

#define DuffsDevice4(n, condition, code) do { if(likely((condition))) { \
    switch( (n) % 4 ) \
    { \
        case 0: do { code; \
        case 3:      code; \
        case 2:      code; \
        case 1:      code; } while((condition)); \
    } } } while(0)

#define DuffsDevice8(n, condition, code) do { if(likely((condition))) { \
    switch( (n) % 8 ) \
    { \
        case 0: do { code; \
        case 7:      code; \
        case 6:      code; \
        case 5:      code; \
        case 4:      code; \
        case 3:      code; \
        case 2:      code; \
        case 1:      code; } while((condition)); \
    } } } while(0)

#define DuffsDevice16(n, condition, code) do { if(likely((condition))) { \
    switch( (n) % 16 ) \
    { \
        case 0: do { code; \
        case 15:     code; \
        case 14:     code; \
        case 13:     code; \
        case 12:     code; \
        case 11:     code; \
        case 10:     code; \
        case 9:      code; \
        case 8:      code; \
        case 7:      code; \
        case 6:      code; \
        case 5:      code; \
        case 4:      code; \
        case 3:      code; \
        case 2:      code; \
        case 1:      code; } while((condition)); \
    } } } while(0)

#endif

#define DuffsDevice8_once(n, code) do { \
    switch( (n) ) \
    { \
        case 8:      code; \
        case 7:      code; \
        case 6:      code; \
        case 5:      code; \
        case 4:      code; \
        case 3:      code; \
        case 2:      code; \
        case 1:      code; \
    } } while(0)

#define DuffsDevice4_once(n, code) do { \
    switch( (n) ) \
    { \
        case 4:      code; \
        case 3:      code; \
        case 2:      code; \
        case 1:      code; \
    } } while(0)

#define DuffsDevice3_once(n, code) do { \
    switch( (n) ) \
    { \
        case 3:      code; \
        case 2:      code; \
        case 1:      code; \
    } } while(0)
