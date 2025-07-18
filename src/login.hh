#ifndef TIBIA_LOGIN_HH_
#define TIBIA_LOGIN_HH_ 1

#include "common.hh"

// Shutdown Signal
extern int  g_ShutdownSignal;

// Time
extern int  g_MonotonicTimeMS;

// Config
extern char g_Motd[256];
extern int  g_UpdateRate;
extern int  g_LoginPort;
extern int  g_MaxConnections;
extern int  g_LoginTimeout;
extern char g_QueryManagerHost[100];
extern int  g_QueryManagerPort;
extern char g_QueryManagerPassword[30];

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

	void ReadString(char *Dest, int DestCapacity){
		int Length = (int)this->Read16();
		if(Length == 0xFFFF){
			Length = (int)this->Read32();
		}

		if(Dest != NULL && DestCapacity > 0){
			if(Length < DestCapacity && this->CanRead(Length)){
				memcpy(Dest, this->Buffer + this->Position, Length);
				Dest[Length] = 0;
			}else{
				Dest[0] = 0;
			}
		}

		this->Position += Length;
	}

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

	void Rewrite16(int Position, uint16 Value){
		if((Position + 2) <= this->Position){
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
enum : int {
	APPLICATION_TYPE_GAME	= 1,
	APPLICATION_TYPE_LOGIN	= 2,
	APPLICATION_TYPE_WEB	= 3,
};

enum : int {
	QUERY_STATUS_OK			= 0,
	QUERY_STATUS_ERROR		= 1,
	QUERY_STATUS_FAILED		= 3,
};

struct TCharacterLoginData{
	char Name[30];
	char WorldName[30];
	int WorldAddress;
	int WorldPort;
};

struct TQueryManagerConnection{
	int Socket;
	uint8 Buffer[KB(16)];
};

bool Connect(TQueryManagerConnection *Connection);
void Disconnect(TQueryManagerConnection *Connection);
bool IsConnected(TQueryManagerConnection *Connection);
TWriteBuffer PrepareQuery(TQueryManagerConnection *Connection, int QueryType);
int ExecuteQuery(TQueryManagerConnection *Connection, bool AutoReconnect,
		TWriteBuffer *WriteBuffer, TReadBuffer *OutReadBuffer);
int LoginAccount(int AccountID, const char *Password, const char *IPAddress,
		int MaxCharacters, int *NumCharacters, TCharacterLoginData *Characters,
		int *PremiumDays);
bool InitQuery(void);
void ExitQuery(void);

// connections.cc
//==============================================================================
enum ConnectionState{
	CONNECTION_FREE			= 0,
	CONNECTION_HANDSHAKE	= 1,
	CONNECTION_LOGIN		= 2,
	CONNECTION_WRITE		= 3,
};

struct TConnection{
	ConnectionState State;
	int Socket;
	int StartTime;
	int RWSize;
	int RWPosition;
	uint32 RandomSeed;
	uint32 XTEA[4];
	char IPAddress[16];
	char RemoteAddress[30];
	uint8 Buffer[KB(2)];
};

int ListenerBind(uint16 Port);
int ListenerAccept(int Listener, uint32 *OutAddr, uint16 *OutPort);
void CloseConnection(TConnection *Connection);
TConnection *AssignConnection(int Socket, uint32 Addr, uint16 Port);
void ReleaseConnection(TConnection *Connection);
void ProcessLoginRequest(TConnection *Connection);
void CheckConnectionInput(TConnection *Connection, int Events);
void CheckConnectionOutput(TConnection *Connection, int Events);
void CheckConnection(TConnection *Connection, int Events);
void ProcessConnections(void);
bool InitConnections(void);
void ExitConnections(void);

TWriteBuffer PrepareResponse(TConnection *Connection);
void SendResponse(TConnection *Connection, TWriteBuffer *WriteBuffer);
void SendLoginError(TConnection *Connection, const char *Message);
void SendCharacterList(TConnection *Connection, int NumCharacters,
		TCharacterLoginData *Characters, int PremiumDays);
void ProcessLoginRequest(TConnection *Connection);

#endif //TIBIA_LOGIN_HH_
