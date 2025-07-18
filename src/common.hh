#ifndef TIBIA_LOGIN_COMMON_HH_
#define TIBIA_LOGIN_COMMON_HH_ 1

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef size_t usize;

#define STATIC_ASSERT(expr) static_assert((expr), "static assertion failed: " #expr)
#define NARRAY(arr) (int)(sizeof(arr) / sizeof(arr[0]))
#define ISPOW2(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define KB(x) ((usize)(x) << 10)
#define MB(x) ((usize)(x) << 20)
#define GB(x) ((usize)(x) << 30)

#if defined(_WIN32)
#	define OS_WINDOWS 1
#elif defined(__linux__) || defined(__gnu_linux__)
#	define OS_LINUX 1
#else
#	error "Operating system not supported."
#endif

#if defined(_MSC_VER)
#	define COMPILER_MSVC 1
#elif defined(__GNUC__)
#	define COMPILER_GCC 1
#elif defined(__clang__)
#	define COMPILER_CLANG 1
#endif

#if COMPILER_GCC || COMPILER_CLANG
#	define ATTR_FALLTHROUGH __attribute__((fallthrough))
#	define ATTR_PRINTF(x, y) __attribute__((format(printf, x, y)))
#else
#	define ATTR_FALLTHROUGH
#	define ATTR_PRINTF(x, y)
#endif

#if COMPILER_MSVC
#	define TRAP() __debugbreak()
#elif COMPILER_GCC || COMPILER_CLANG
#	define TRAP() __builtin_trap()
#else
#	define TRAP() abort()
#endif

#define ASSERT_ALWAYS(expr) if(!(expr)) { TRAP(); }
#if BUILD_DEBUG
#	define ASSERT(expr) ASSERT_ALWAYS(expr)
#else
#	define ASSERT(expr) ((void)(expr))
#endif

#define LOG(...)		LogAdd("INFO", __VA_ARGS__)
#define LOG_WARN(...)	LogAddVerbose("WARN", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERR(...)	LogAddVerbose("ERR", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define PANIC(...)																\
	do{																			\
		LogAddVerbose("PANIC", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);	\
		TRAP();																	\
	}while(0)

void LogAdd(const char *Prefix, const char *Format, ...) ATTR_PRINTF(2, 3);
void LogAddVerbose(const char *Prefix, const char *Function,
		const char *File, int Line, const char *Format, ...) ATTR_PRINTF(5, 6);

struct tm GetLocalTime(time_t t);
int64 GetClockMonotonicMS(void);
void SleepMS(int64 DurationMS);

bool StringEq(const char *A, const char *B);
bool StringEqCI(const char *A, const char *B);
bool StringCopyN(char *Dest, int DestCapacity, const char *Src, int SrcLength);
bool StringCopy(char *Dest, int DestCapacity, const char *Src);
bool EscapeString(char *Dest, int DestCapacity, const char *Src);
uint32 HashString(const char *String);

bool ParseIPAddress(int *Dest, const char *String);
bool ParseBoolean(bool *Dest, const char *String);
bool ParseInteger(int *Dest, const char *String);
bool ParseDuration(int *Dest, const char *String);
bool ParseSize(int *Dest, const char *String);
bool ParseString(char *Dest, int DestCapacity, const char *String);

typedef void ConfigKVCallback(const char *Key, const char *Val);
bool ReadConfig(const char *FileName, ConfigKVCallback *KVCallback);

#endif //TIBIA_LOGIN_COMMON_HH_
