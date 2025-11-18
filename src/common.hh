#ifndef TIBIA_COMMON_HH_
#define TIBIA_COMMON_HH_ 1

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;

#define STATIC_ASSERT(expr) static_assert((expr), "static assertion failed: " #expr)
#define NARRAY(arr) (int)(sizeof(arr) / sizeof(arr[0]))
#define ISPOW2(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define KB(x) ((x) * 1024)

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
#if ENABLE_ASSERTIONS
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

struct TConfig {
	// Service Config
	int LoginPort;
	int ConnectionTimeout;
	int MaxConnections;
	int MaxStatusRecords;
	int MinStatusInterval;
	char QueryManagerHost[100];
	int QueryManagerPort;
	char QueryManagerPassword[30];

	// Service Info
	char StatusWorld[30];
	char Url[100];
	char Location[30];
	char ServerType[30];
	char ServerVersion[30];
	char ClientVersion[30];
	char Motd[256];
};

extern TConfig g_Config;

void LogAdd(const char *Prefix, const char *Format, ...) ATTR_PRINTF(2, 3);
void LogAddVerbose(const char *Prefix, const char *Function,
		const char *File, int Line, const char *Format, ...) ATTR_PRINTF(5, 6);

struct tm GetLocalTime(time_t t);
struct tm GetGMTime(time_t t);
int64 GetClockMonotonicMS(void);
int GetMonotonicUptime(void);

bool StringEmpty(const char *String);
bool StringEq(const char *A, const char *B);
bool StringEqCI(const char *A, const char *B);
bool StringCopy(char *Dest, int DestCapacity, const char *Src);
bool StringCopyN(char *Dest, int DestCapacity, const char *Src, int SrcLength);
bool StringFormat(char *Dest, int DestCapacity, const char *Format, ...) ATTR_PRINTF(3, 4);
bool StringFormatTime(char *Dest, int DestCapacity, const char *Format, int Timestamp);
void StringClear(char *Dest, int DestCapacity);
uint32 StringHash(const char *String);
bool StringEscape(char *Dest, int DestCapacity, const char *Src);

int UTF8SequenceSize(uint8 LeadingByte);
bool UTF8IsTrailingByte(uint8 Byte);
int UTF8EncodedSize(int Codepoint);
int UTF8FindNextLeadingByte(const char *Src, int SrcLength);
int UTF8DecodeOne(const uint8 *Src, int SrcLength, int *OutCodepoint);
int UTF8EncodeOne(uint8 *Dest, int DestCapacity, int Codepoint);
int UTF8ToLatin1(char *Dest, int DestCapacity, const char *Src, int SrcLength);
int Latin1ToUTF8(char *Dest, int DestCapacity, const char *Src, int SrcLength);

bool ParseBoolean(bool *Dest, const char *String);
bool ParseInteger(int *Dest, const char *String);
bool ParseDuration(int *Dest, const char *String);
bool ParseSize(int *Dest, const char *String);
bool ParseString(char *Dest, int DestCapacity, const char *String);
void ParseMotd(char *Dest, int DestCapacity, const char *String);
bool ReadConfig(const char *FileName, TConfig *Config);

// IMPORTANT(fusion): These macros should only be used when `Dest` is a char array
// to simplify the call to `StringCopy` where we'd use `sizeof(Dest)` to determine
// the size of the destination anyways.
#define StringBufCopy(Dest, Src)             StringCopy(Dest, sizeof(Dest), Src)
#define StringBufCopyN(Dest, Src, SrcLength) StringCopyN(Dest, sizeof(Dest), Src, SrcLength)
#define StringBufFormat(Dest, ...)           StringFormat(Dest, sizeof(Dest), __VA_ARGS__)
#define StringBufFormatTime(Dest, Format, Timestamp) \
		StringFormatTime(Dest, sizeof(Dest), Format, Timestamp)
#define StringBufClear(Dest)                 StringClear(Dest, sizeof(Dest));
#define StringBufEscape(Dest, Src)           StringEscape(Dest, sizeof(Dest), Src)
#define ParseStringBuf(Dest, String)         ParseString(Dest, sizeof(Dest), String)

// Buffer Utility
//==============================================================================
inline uint8 BufferRead8(const uint8 *Buffer){
	return Buffer[0];
}

inline uint16 BufferRead16LE(const uint8 *Buffer){
	return (uint16)Buffer[0]
		| ((uint16)Buffer[1] << 8);
}

inline uint16 BufferRead16BE(const uint8 *Buffer){
	return ((uint16)Buffer[0] << 8)
		| (uint16)Buffer[1];
}

inline uint32 BufferRead32LE(const uint8 *Buffer){
	return (uint32)Buffer[0]
		| ((uint32)Buffer[1] << 8)
		| ((uint32)Buffer[2] << 16)
		| ((uint32)Buffer[3] << 24);
}

inline uint32 BufferRead32BE(const uint8 *Buffer){
	return ((uint32)Buffer[0] << 24)
		| ((uint32)Buffer[1] << 16)
		| ((uint32)Buffer[2] << 8)
		| (uint32)Buffer[3];
}

inline uint64 BufferRead64LE(const uint8 *Buffer){
	return (uint64)Buffer[0]
		| ((uint64)Buffer[1] << 8)
		| ((uint64)Buffer[2] << 16)
		| ((uint64)Buffer[3] << 24)
		| ((uint64)Buffer[4] << 32)
		| ((uint64)Buffer[5] << 40)
		| ((uint64)Buffer[6] << 48)
		| ((uint64)Buffer[7] << 56);
}

inline uint64 BufferRead64BE(const uint8 *Buffer){
	return ((uint64)Buffer[0] << 56)
		| ((uint64)Buffer[1] << 48)
		| ((uint64)Buffer[2] << 40)
		| ((uint64)Buffer[3] << 32)
		| ((uint64)Buffer[4] << 24)
		| ((uint64)Buffer[5] << 16)
		| ((uint64)Buffer[6] << 8)
		| (uint64)Buffer[7];
}

inline void BufferWrite8(uint8 *Buffer, uint8 Value){
	Buffer[0] = Value;
}

inline void BufferWrite16LE(uint8 *Buffer, uint16 Value){
	Buffer[0] = (uint8)(Value >> 0);
	Buffer[1] = (uint8)(Value >> 8);
}

inline void BufferWrite16BE(uint8 *Buffer, uint16 Value){
	Buffer[0] = (uint8)(Value >> 8);
	Buffer[1] = (uint8)(Value >> 0);
}

inline void BufferWrite32LE(uint8 *Buffer, uint32 Value){
	Buffer[0] = (uint8)(Value >>  0);
	Buffer[1] = (uint8)(Value >>  8);
	Buffer[2] = (uint8)(Value >> 16);
	Buffer[3] = (uint8)(Value >> 24);
}

inline void BufferWrite32BE(uint8 *Buffer, uint32 Value){
	Buffer[0] = (uint8)(Value >> 24);
	Buffer[1] = (uint8)(Value >> 16);
	Buffer[2] = (uint8)(Value >>  8);
	Buffer[3] = (uint8)(Value >>  0);
}

inline void BufferWrite64LE(uint8 *Buffer, uint64 Value){
	Buffer[0] = (uint8)(Value >>  0);
	Buffer[1] = (uint8)(Value >>  8);
	Buffer[2] = (uint8)(Value >> 16);
	Buffer[3] = (uint8)(Value >> 24);
	Buffer[4] = (uint8)(Value >> 32);
	Buffer[5] = (uint8)(Value >> 40);
	Buffer[6] = (uint8)(Value >> 48);
	Buffer[7] = (uint8)(Value >> 56);
}

inline void BufferWrite64BE(uint8 *Buffer, uint64 Value){
	Buffer[0] = (uint8)(Value >> 56);
	Buffer[1] = (uint8)(Value >> 48);
	Buffer[2] = (uint8)(Value >> 40);
	Buffer[3] = (uint8)(Value >> 32);
	Buffer[4] = (uint8)(Value >> 24);
	Buffer[5] = (uint8)(Value >> 16);
	Buffer[6] = (uint8)(Value >>  8);
	Buffer[7] = (uint8)(Value >>  0);
}

struct TReadBuffer{
	uint8 *Buffer;
	int Size;
	int Position;

	TReadBuffer(void) : TReadBuffer(NULL, 0) {}
	TReadBuffer(uint8 *Buffer, int Size)
		: Buffer(Buffer), Size(Size), Position(0) {}

	bool CanRead(int Bytes){
		return (this->Position + Bytes) <= this->Size;
	}

	bool Overflowed(void){
		return this->Position > this->Size;
	}

	bool ReadFlag(void){
		return this->Read8() != 0x00;
	}

	uint8 Read8(void){
		uint8 Result = 0;
		if(this->CanRead(1)){
			Result = BufferRead8(this->Buffer + this->Position);
		}
		this->Position += 1;
		return Result;
	}

	uint16 Read16(void){
		uint16 Result = 0;
		if(this->CanRead(2)){
			Result = BufferRead16LE(this->Buffer + this->Position);
		}
		this->Position += 2;
		return Result;
	}

	uint16 Read16BE(void){
		uint16 Result = 0;
		if(this->CanRead(2)){
			Result = BufferRead16BE(this->Buffer + this->Position);
		}
		this->Position += 2;
		return Result;
	}

	uint32 Read32(void){
		uint32 Result = 0;
		if(this->CanRead(4)){
			Result = BufferRead32LE(this->Buffer + this->Position);
		}
		this->Position += 4;
		return Result;
	}

	uint32 Read32BE(void){
		uint32 Result = 0;
		if(this->CanRead(4)){
			Result = BufferRead32BE(this->Buffer + this->Position);
		}
		this->Position += 4;
		return Result;
	}

#if CLIENT_ENCODING_UTF8
	void ReadString(char *Dest, int DestCapacity){
		int Length = (int)this->Read16();
		if(Length == 0xFFFF){
			Length = (int)this->Read32();
		}

		if(Dest != NULL && DestCapacity > 0){
			int Written = 0;
			if(this->CanRead(Length) && Length < DestCapacity){
				memcpy(Dest, this->Buffer + this->Position, Length);
				Written = Length;
			}
			memset((Dest + Written), 0, (DestCapacity - Written));
		}

		this->Position += Length;
	}
#else
	void ReadString(char *Dest, int DestCapacity){
		int Length = (int)this->Read16();
		if(Length == 0xFFFF){
			Length = (int)this->Read32();
		}

		if(Dest != NULL && DestCapacity > 0){
			int Written = 0;
			if(this->CanRead(Length)){
				const char *Src = (const char*)(this->Buffer + this->Position);
				Written = Latin1ToUTF8(Dest, DestCapacity, Src, Length);
				if(Written >= DestCapacity){
					Written = 0;
				}
			}

			memset((Dest + Written), 0, (DestCapacity - Written));
		}

		this->Position += Length;
	}
#endif

	void ReadBytes(uint8 *Buffer, int Count){
		if(this->CanRead(Count)){
			memcpy(Buffer, this->Buffer + this->Position, Count);
		}
		this->Position += Count;
	}
};

struct TWriteBuffer{
	uint8 *Buffer;
	int Size;
	int Position;

	TWriteBuffer(void) : TWriteBuffer(NULL, 0) {}
	TWriteBuffer(uint8 *Buffer, int Size)
		: Buffer(Buffer), Size(Size), Position(0) {}

	bool CanWrite(int Bytes){
		return (this->Position + Bytes) <= this->Size;
	}

	bool Overflowed(void){
		return this->Position > this->Size;
	}

	void WriteFlag(bool Value){
		this->Write8(Value ? 0x01 : 0x00);
	}

	void Write8(uint8 Value){
		if(this->CanWrite(1)){
			BufferWrite8(this->Buffer + this->Position, Value);
		}
		this->Position += 1;
	}

	void Write16(uint16 Value){
		if(this->CanWrite(2)){
			BufferWrite16LE(this->Buffer + this->Position, Value);
		}
		this->Position += 2;
	}

	void Write16BE(uint16 Value){
		if(this->CanWrite(2)){
			BufferWrite16BE(this->Buffer + this->Position, Value);
		}
		this->Position += 2;
	}

	void Write32(uint32 Value){
		if(this->CanWrite(4)){
			BufferWrite32LE(this->Buffer + this->Position, Value);
		}
		this->Position += 4;
	}

	void Write32BE(uint32 Value){
		if(this->CanWrite(4)){
			BufferWrite32BE(this->Buffer + this->Position, Value);
		}
		this->Position += 4;
	}

#if CLIENT_ENCODING_UTF8
	void WriteString(const char *String){
		int StringLength = 0;
		if(String != NULL){
			StringLength = (int)strlen(String);
		}

		if(StringLength < 0xFFFF){
			this->Write16((uint16)StringLength);
		}else{
			this->Write16(0xFFFF);
			this->Write32((uint32)StringLength);
		}

		if(StringLength > 0 && this->CanWrite(StringLength)){
			memcpy(this->Buffer + this->Position, String, StringLength);
		}

		this->Position += StringLength;
	}
#else
	void WriteString(const char *String){
		int StringLength = 0;
		int OutputLength = 0;
		if(String != NULL){
			StringLength = (int)strlen(String);
			OutputLength = UTF8ToLatin1(NULL, 0, String, (int)strlen(String));
		}

		if(OutputLength < 0xFFFF){
			this->Write16((uint16)OutputLength);
		}else{
			this->Write16(0xFFFF);
			this->Write32((uint32)OutputLength);
		}

		if(OutputLength > 0 && this->CanWrite(OutputLength)){
			int Written = UTF8ToLatin1((char*)(this->Buffer + this->Position),
					(this->Size - this->Position), String, StringLength);
			ASSERT(Written == OutputLength);
		}

		this->Position += OutputLength;
	}
#endif

	void Rewrite16(int Position, uint16 Value){
		if((Position + 2) <= this->Position && !this->Overflowed()){
			BufferWrite16LE(this->Buffer + Position, Value);
		}
	}

	void Insert32(int Position, uint32 Value){
		if(Position <= this->Position){
			if(this->CanWrite(4)){
				memmove(this->Buffer + Position + 4,
						this->Buffer + Position,
						this->Position - Position);
				BufferWrite32LE(this->Buffer + Position, Value);
			}

			this->Position += 4;
		}
	}
};

// crypto.cc
//==============================================================================
typedef void RSAKey;
RSAKey *RSALoadPEM(const char *FileName);
void RSAFree(RSAKey *Key);
bool RSADecrypt(RSAKey *Key, uint8 *Data, int Size);
void XTEAEncrypt(const uint32 *Key, uint8 *Data, int Size);
void XTEADecrypt(const uint32 *Key, uint8 *Data, int Size);

// query.cc
//==============================================================================
enum {
	APPLICATION_TYPE_GAME	= 1,
	APPLICATION_TYPE_LOGIN	= 2,
	APPLICATION_TYPE_WEB	= 3,
};

enum {
	QUERY_STATUS_OK			= 0,
	QUERY_STATUS_ERROR		= 1,
	QUERY_STATUS_FAILED		= 3,
};

enum {
	QUERY_LOGIN				= 0,
	QUERY_LOGIN_ACCOUNT		= 11,
	QUERY_GET_WORLDS		= 150,
};

struct TQueryManagerConnection{
	int Socket;
};

struct TCharacterLoginData{
	char Name[30];
	char WorldName[30];
	int WorldAddress;
	int WorldPort;
};

struct TWorld {
	char Name[30];
	int Type;
	int NumPlayers;
	int MaxPlayers;
	int OnlinePeak;
	int OnlinePeakTimestamp;
	int LastStartup;
	int LastShutdown;
};

bool Connect(TQueryManagerConnection *Connection);
void Disconnect(TQueryManagerConnection *Connection);
bool IsConnected(TQueryManagerConnection *Connection);
TWriteBuffer PrepareQuery(int QueryType, uint8 *Buffer, int BufferSize);
int ExecuteQuery(TQueryManagerConnection *Connection, bool AutoReconnect,
		TWriteBuffer *WriteBuffer, TReadBuffer *OutReadBuffer);
int LoginAccount(int AccountID, const char *Password, const char *IPAddress,
		int MaxCharacters, int *NumCharacters, TCharacterLoginData *Characters,
		int *PremiumDays);
int GetWorld(const char *WorldName, TWorld *OutWorld);
bool InitQuery(void);
void ExitQuery(void);

// status.cc
//==============================================================================
const char *GetStatusString(void);

// connections.cc
//==============================================================================
enum ConnectionState {
	CONNECTION_FREE			= 0,
	CONNECTION_READING		= 1,
	CONNECTION_PROCESSING	= 2,
	CONNECTION_WRITING		= 3,
};

struct TConnection {
	ConnectionState State;
	int Socket;
	int IPAddress;
	int StartTime;
	int RWSize;
	int RWPosition;
	uint32 RandomSeed;
	uint32 XTEA[4];
	char RemoteAddress[32];
	uint8 Buffer[KB(2)];
};

struct TStatusRecord {
	int IPAddress;
	int Timestamp;
};

void ProcessConnections(void);
bool InitConnections(void);
void ExitConnections(void);
void ProcessLoginRequest(TConnection *Connection);
void ProcessStatusRequest(TConnection *Connection);

#endif //TIBIA_COMMON_H_
