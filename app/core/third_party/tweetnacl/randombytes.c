/* randombytes for TweetNaCl (public domain). Only signature *verification* is used by the app, which never needs randomness;
   this exists so the library links. Reads /dev/urandom (iOS/macOS/Linux) or uses arc4random_buf where available. */
#include <stdio.h>
#include <stdlib.h>
#if defined(__APPLE__)
void randombytes (unsigned char* x, unsigned long long n) { arc4random_buf (x, (size_t) n); }
#elif defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
void randombytes (unsigned char* x, unsigned long long n) { BCryptGenRandom (NULL, x, (ULONG) n, BCRYPT_USE_SYSTEM_PREFERRED_RNG); }
#else
void randombytes (unsigned char* x, unsigned long long n) { FILE* f = fopen ("/dev/urandom", "rb"); if (! f || fread (x, 1, (size_t) n, f) != (size_t) n) abort(); fclose (f); }
#endif
