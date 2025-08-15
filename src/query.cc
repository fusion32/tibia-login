#include "login.hh"

// TODO(fusion): Support windows eventually?
#if OS_LINUX
#	include <errno.h>
#	include <netdb.h>
#	include <sys/socket.h>
#	include <unistd.h>
#else
#	error "Operating system not currently supported."
#endif

static TQueryManagerConnection *g_QueryManagerConnection;

bool ResolveHostName(const char *HostName, in_addr_t *OutAddr){
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

bool Connect(TQueryManagerConnection *Connection){
	if(Connection->Socket != -1){
		LOG_ERR("Already connected");
		return false;
	}

	in_addr_t Addr;
	if(!ResolveHostName(g_QueryManagerHost, &Addr)){
		LOG_ERR("Failed to resolve query manager's host name \"%s\"", g_QueryManagerHost);
		return false;
	}

	Connection->Socket = socket(AF_INET, SOCK_STREAM, 0);
	if(Connection->Socket == -1){
		LOG_ERR("Failed to create socket: (%d) %s", errno, strerrordesc_np(errno));
		return false;
	}

	sockaddr_in QueryManagerAddress = {};
	QueryManagerAddress.sin_family = AF_INET;
	QueryManagerAddress.sin_port = htons((uint16)g_QueryManagerPort);
	QueryManagerAddress.sin_addr.s_addr = Addr;
	if(connect(Connection->Socket, (sockaddr*)&QueryManagerAddress, sizeof(QueryManagerAddress)) == -1){
		LOG_ERR("Failed to connect: (%d) %s", errno, strerrordesc_np(errno));
		Disconnect(Connection);
		return false;
	}

	TWriteBuffer WriteBuffer = PrepareQuery(Connection, 0);
	WriteBuffer.Write8((uint8)APPLICATION_TYPE_LOGIN);
	WriteBuffer.WriteString(g_QueryManagerPassword);
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

TWriteBuffer PrepareQuery(TQueryManagerConnection *Connection, int QueryType){
	TWriteBuffer WriteBuffer(Connection->Buffer, sizeof(Connection->Buffer));
	WriteBuffer.Write16(0); // Request Size
	WriteBuffer.Write8((uint8)QueryType);
	return WriteBuffer;
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

int ExecuteQuery(TQueryManagerConnection *Connection, bool AutoReconnect,
		TWriteBuffer *WriteBuffer, TReadBuffer *OutReadBuffer){
	ASSERT(WriteBuffer != NULL
		&& WriteBuffer->Buffer == Connection->Buffer
		&& WriteBuffer->Size == sizeof(Connection->Buffer)
		&& WriteBuffer->Position > 2);

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
	for(int Attempt = 1; true; Attempt += 1){
		int WriteSize = WriteBuffer->Position;
		if(!IsConnected(Connection)){
			if(!AutoReconnect){
				return QUERY_STATUS_FAILED;
			}

			// IMPORTANT(fusion): There is no way around this. `Connect` will
			// use the connection buffer to send the login query so we need to
			// save and restore it to not lose any data. One improvement here
			// would be to use a stack or statically allocated buffer, although
			// using malloc/free should have no real impact on performance as
			// we're already doing BLOCKING I/O here.
			uint8 *TempBuffer = (uint8*)malloc(WriteSize);
			memcpy(TempBuffer, Connection->Buffer, WriteSize);
			bool Reconnected = Connect(Connection);
			memcpy(Connection->Buffer, TempBuffer, WriteSize);
			free(TempBuffer);

			if(!Reconnected){
				return QUERY_STATUS_FAILED;
			}
		}

		if(!WriteExact(Connection->Socket, Connection->Buffer, WriteSize)){
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

		if(ResponseSize <= 0 || ResponseSize > (int)sizeof(Connection->Buffer)){
			Disconnect(Connection);
			LOG_ERR("Invalid response size %d (BufferSize: %d)",
					ResponseSize, (int)sizeof(Connection->Buffer));
			return QUERY_STATUS_FAILED;
		}

		if(!ReadExact(Connection->Socket, Connection->Buffer, ResponseSize)){
			Disconnect(Connection);
			LOG_ERR("Failed to read response");
			return QUERY_STATUS_FAILED;
		}

		TReadBuffer ReadBuffer(Connection->Buffer, ResponseSize);
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
	TWriteBuffer WriteBuffer = PrepareQuery(g_QueryManagerConnection, 11);
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

bool InitQuery(void){
	ASSERT(g_QueryManagerConnection == NULL);

	LOG("QueryManagerHost: %s", g_QueryManagerHost);
	LOG("QueryManagerPort: %d", g_QueryManagerPort);

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
