#include "common.hh"

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

static TQueryManagerConnection *g_QueryManagerConnection;

static bool ResolveHostName(const char *HostName, in_addr_t *OutAddr){
	ASSERT(HostName != NULL && OutAddr != NULL);
	addrinfo *Result = NULL;
	addrinfo Hints = {};
	Hints.ai_family = AF_INET;
	Hints.ai_socktype = SOCK_STREAM;
	int ErrCode = getaddrinfo(HostName, NULL, &Hints, &Result);
	if(ErrCode != 0){
		LOG_ERR("Failed to resolve hostname \"%s\": %s", HostName, gai_strerror(ErrCode));
		return false;
	}

	bool Resolved = false;
	for(addrinfo *AddrInfo = Result;
			AddrInfo != NULL;
			AddrInfo = AddrInfo->ai_next){
		if(AddrInfo->ai_family == AF_INET && AddrInfo->ai_socktype == SOCK_STREAM){
			ASSERT(AddrInfo->ai_addrlen == sizeof(sockaddr_in));
			*OutAddr = ((sockaddr_in*)AddrInfo->ai_addr)->sin_addr.s_addr;
			Resolved = true;
			break;
		}
	}
	freeaddrinfo(Result);
	return Resolved;
}

static bool WriteExact(int Fd, const uint8 *Buffer, int Size){
	int BytesToWrite = Size;
	const uint8 *WritePtr = Buffer;
	while(BytesToWrite > 0){
		int Ret = (int)write(Fd, WritePtr, BytesToWrite);
		if(Ret == -1){
			return false;
		}
		BytesToWrite -= Ret;
		WritePtr += Ret;
	}
	return true;
}

static bool ReadExact(int Fd, uint8 *Buffer, int Size){
	int BytesToRead = Size;
	uint8 *ReadPtr = Buffer;
	while(BytesToRead > 0){
		int Ret = (int)read(Fd, ReadPtr, BytesToRead);
		if(Ret == -1 || Ret == 0){
			return false;
		}
		BytesToRead -= Ret;
		ReadPtr += Ret;
	}
	return true;
}

bool Connect(TQueryManagerConnection *Connection){
	if(Connection->Socket != -1){
		LOG_ERR("Already connected");
		return false;
	}

	in_addr_t Addr;
	if(!ResolveHostName(g_Config.QueryManagerHost, &Addr)){
		LOG_ERR("Failed to resolve query manager's host name \"%s\"", g_Config.QueryManagerHost);
		return false;
	}

	Connection->Socket = socket(AF_INET, SOCK_STREAM, 0);
	if(Connection->Socket == -1){
		LOG_ERR("Failed to create socket: (%d) %s", errno, strerrordesc_np(errno));
		return false;
	}

	sockaddr_in QueryManagerAddress = {};
	QueryManagerAddress.sin_family = AF_INET;
	QueryManagerAddress.sin_port = htons((uint16)g_Config.QueryManagerPort);
	QueryManagerAddress.sin_addr.s_addr = Addr;
	if(connect(Connection->Socket, (sockaddr*)&QueryManagerAddress, sizeof(QueryManagerAddress)) == -1){
		LOG_ERR("Failed to connect: (%d) %s", errno, strerrordesc_np(errno));
		Disconnect(Connection);
		return false;
	}

	uint8 LoginBuffer[1024];
	TWriteBuffer WriteBuffer = PrepareQuery(QUERY_LOGIN, LoginBuffer, sizeof(LoginBuffer));
	WriteBuffer.Write8((uint8)APPLICATION_TYPE_LOGIN);
	WriteBuffer.WriteString(g_Config.QueryManagerPassword);
	int Status = ExecuteQuery(Connection, false, &WriteBuffer, NULL);
	if(Status != QUERY_STATUS_OK){
		LOG_ERR("Failed to login to query manager (%d)", Status);
		Disconnect(Connection);
		return false;
	}

	return true;
}

void Disconnect(TQueryManagerConnection *Connection){
	if(Connection->Socket != -1){
		close(Connection->Socket);
		Connection->Socket = -1;
	}
}

bool IsConnected(TQueryManagerConnection *Connection){
	return Connection->Socket != -1;
}

TWriteBuffer PrepareQuery(int QueryType, uint8 *Buffer, int BufferSize){
	TWriteBuffer WriteBuffer(Buffer, BufferSize);
	WriteBuffer.Write16(0); // Request Size
	WriteBuffer.Write8((uint8)QueryType);
	return WriteBuffer;
}

int ExecuteQuery(TQueryManagerConnection *Connection, bool AutoReconnect,
		TWriteBuffer *WriteBuffer, TReadBuffer *OutReadBuffer){
	// IMPORTANT(fusion): This is similar to the Go version where there is no
	// connection buffer, and the response is read into the same buffer used
	// by `WriteBuffer. This helps prevent allocating and moving data around
	// when reconnecting in the middle of a query.
	ASSERT(WriteBuffer != NULL && WriteBuffer->Position > 2);

	int RequestSize = WriteBuffer->Position - 2;
	if(RequestSize < 0xFFFF){
		WriteBuffer->Rewrite16(0, (uint16)RequestSize);
	}else{
		WriteBuffer->Rewrite16(0, 0xFFFF);
		WriteBuffer->Insert32(2, (uint32)RequestSize);
	}

	if(WriteBuffer->Overflowed()){
		LOG_ERR("Write buffer overflowed when writing request");
		return QUERY_STATUS_FAILED;
	}

	const int MaxAttempts = 2;
	uint8 *Buffer = WriteBuffer->Buffer;
	int BufferSize = WriteBuffer->Size;
	int WriteSize = WriteBuffer->Position;
	for(int Attempt = 1; true; Attempt += 1){
		if(!IsConnected(Connection) && (!AutoReconnect || !Connect(Connection))){
			return QUERY_STATUS_FAILED;
		}

		if(!WriteExact(Connection->Socket, Buffer, WriteSize)){
			Disconnect(Connection);
			if(Attempt >= MaxAttempts){
				LOG_ERR("Failed to write request");
				return QUERY_STATUS_FAILED;
			}
			continue;
		}

		uint8 Help[4];
		if(!ReadExact(Connection->Socket, Help, 2)){
			Disconnect(Connection);
			if(Attempt >= MaxAttempts){
				LOG_ERR("Failed to read response size");
				return QUERY_STATUS_FAILED;
			}
			continue;
		}

		int ResponseSize = BufferRead16LE(Help);
		if(ResponseSize == 0xFFFF){
			if(!ReadExact(Connection->Socket, Help, 4)){
				Disconnect(Connection);
				LOG_ERR("Failed to read response extended size");
				return QUERY_STATUS_FAILED;
			}
			ResponseSize = BufferRead32LE(Help);
		}

		if(ResponseSize <= 0 || ResponseSize > BufferSize){
			Disconnect(Connection);
			LOG_ERR("Invalid response size %d (BufferSize: %d)",
					ResponseSize, BufferSize);
			return QUERY_STATUS_FAILED;
		}

		if(!ReadExact(Connection->Socket, Buffer, ResponseSize)){
			Disconnect(Connection);
			LOG_ERR("Failed to read response");
			return QUERY_STATUS_FAILED;
		}

		TReadBuffer ReadBuffer(Buffer, ResponseSize);
		int Status = ReadBuffer.Read8();
		if(OutReadBuffer){
			*OutReadBuffer = ReadBuffer;
		}
		return Status;
	}
}

int LoginAccount(int AccountID, const char *Password, const char *IPAddress,
		int MaxCharacters, int *NumCharacters, TCharacterLoginData *Characters,
		int *PremiumDays){
	uint8 Buffer[KB(4)];
	TWriteBuffer WriteBuffer = PrepareQuery(QUERY_LOGIN_ACCOUNT, Buffer, sizeof(Buffer));
	WriteBuffer.Write32((uint32)AccountID);
	WriteBuffer.WriteString(Password);
	WriteBuffer.WriteString(IPAddress);

	TReadBuffer ReadBuffer;
	int Status = ExecuteQuery(g_QueryManagerConnection, true, &WriteBuffer, &ReadBuffer);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*NumCharacters = ReadBuffer.Read8();
		if(*NumCharacters > MaxCharacters){
			LOG_ERR("Too many characters");
			return -1;
		}

		for(int i = 0; i < *NumCharacters; i += 1){
			ReadBuffer.ReadString(Characters[i].Name, sizeof(Characters[i].Name));
			ReadBuffer.ReadString(Characters[i].WorldName, sizeof(Characters[i].WorldName));
			Characters[i].WorldAddress = ReadBuffer.Read32BE();
			Characters[i].WorldPort = ReadBuffer.Read16();
		}

		*PremiumDays = ReadBuffer.Read16();
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = ReadBuffer.Read8();
		if(ErrorCode >= 1 && ErrorCode <= 6){
			Result = ErrorCode;
		}else{
			LOG_ERR("Invalid error code %d", ErrorCode);
		}
	}else{
		LOG_ERR("Request failed");
	}
	return Result;
}

int GetWorld(const char *WorldName, TWorld *OutWorld){
	ASSERT(WorldName && OutWorld);
	uint8 Buffer[4096];
	TReadBuffer ReadBuffer;
	TWriteBuffer WriteBuffer = PrepareQuery(QUERY_GET_WORLDS, Buffer, sizeof(Buffer));
	int Status = ExecuteQuery(g_QueryManagerConnection, true, &WriteBuffer, &ReadBuffer);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	memset(OutWorld, 0, sizeof(TWorld));
	if(Status == QUERY_STATUS_OK){
		int NumWorlds = (int)ReadBuffer.Read8();
		for(int i = 0; i < NumWorlds; i += 1){
			TWorld World = {};
			ReadBuffer.ReadString(World.Name, sizeof(World.Name));
			World.Type = (int)ReadBuffer.Read8();
			World.NumPlayers = (int)ReadBuffer.Read16();
			World.MaxPlayers = (int)ReadBuffer.Read16();
			World.OnlinePeak = (int)ReadBuffer.Read16();
			World.OnlinePeakTimestamp = (int)ReadBuffer.Read32();
			World.LastStartup = (int)ReadBuffer.Read32();
			World.LastShutdown = (int)ReadBuffer.Read32();

			if(StringEmpty(WorldName)){
				// NOTE(fusion): Pick the world with the most players.
				if(i == 0 || World.NumPlayers > OutWorld->NumPlayers){
					*OutWorld = World;
				}
			}else if(StringEqCI(WorldName, World.Name)){
				*OutWorld = World;
				break;
			}
		}
	}else{
		LOG_ERR("Request failed");
	}
	return Result;
}

bool InitQuery(void){
	ASSERT(g_QueryManagerConnection == NULL);
	g_QueryManagerConnection = (TQueryManagerConnection*)calloc(1, sizeof(TQueryManagerConnection));
	g_QueryManagerConnection->Socket = -1;
	if(!Connect(g_QueryManagerConnection)){
		LOG_ERR("Failed to connect to query manager");
		return false;
	}
	return true;
}

void ExitQuery(void){
	if(g_QueryManagerConnection != NULL){
		Disconnect(g_QueryManagerConnection);
		free(g_QueryManagerConnection);
		g_QueryManagerConnection = NULL;
	}
}

