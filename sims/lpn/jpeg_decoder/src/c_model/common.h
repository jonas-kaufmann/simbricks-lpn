#ifndef __COMMON__
#define __COMMON__
// #define JPEGD_DEBUG
#ifdef JPEGD_DEBUG
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...)
#endif 

// #define ddprintf(...) printf(__VA_ARGS__)
#define ddprintf(...)
#endif