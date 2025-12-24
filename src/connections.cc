#include "common.hh"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#if TIBIA772
static const int TERMINALVERSION[] = {772, 772, 772};
#else
static const int TERMINALVERSION[] = {770, 770, 770};
#endif

static RSAKey *g_PrivateKey;

static int g_Listener = -1;
static TConnection *g_Connections;
static int g_MaxConnections;

static TStatusRecord *g_StatusRecords;
static int g_MaxStatusRecords;

// Connection Handling
//==============================================================================
static int ListenerBind(uint16 Port){
	int Socket = socket(AF_INET, SOCK_STREAM, 0);
	if(Socket == -1){
		LOG_ERR("Failed to create listener socket: (%d) %s", errno, strerrordesc_np(errno));
		return -1;
	}

	int ReuseAddr = 1;
	if(setsockopt(Socket, SOL_SOCKET, SO_REUSEADDR, &ReuseAddr, sizeof(ReuseAddr)) == -1){
		LOG_ERR("Failed to set SO_REUSADDR: (%d) %s", errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	int Flags = fcntl(Socket, F_GETFL);
	if(Flags == -1){
		LOG_ERR("Failed to get socket flags: (%d) %s", errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	if(fcntl(Socket, F_SETFL, Flags | O_NONBLOCK) == -1){
		LOG_ERR("Failed to set socket flags: (%d) %s", errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	sockaddr_in Addr = {};
	Addr.sin_family = AF_INET;
	Addr.sin_port = htons(Port);
	Addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(Socket, (sockaddr*)&Addr, sizeof(Addr)) == -1){
		LOG_ERR("Failed to bind socket to port %d: (%d) %s", Port, errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	if(listen(Socket, 128) == -1){
		LOG_ERR("Failed to listen to port %d: (%d) %s", Port, errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	return Socket;
}

static int ListenerAccept(int Listener, uint32 *OutAddr, uint16 *OutPort){
	while(true){
		sockaddr_in SocketAddr = {};
		socklen_t SocketAddrLen = sizeof(SocketAddr);
		int Socket = accept(Listener, (sockaddr*)&SocketAddr, &SocketAddrLen);
		if(Socket == -1){
			if(errno != EAGAIN){
				LOG_ERR("Failed to accept connection: (%d) %s", errno, strerrordesc_np(errno));
			}
			return -1;
		}

		int Flags = fcntl(Socket, F_GETFL);
		if(Flags == -1){
			LOG_ERR("Failed to get socket flags: (%d) %s", errno, strerrordesc_np(errno));
			close(Socket);
			continue;
		}

		if(fcntl(Socket, F_SETFL, Flags | O_NONBLOCK) == -1){
			LOG_ERR("Failed to set socket flags: (%d) %s", errno, strerrordesc_np(errno));
			close(Socket);
			continue;
		}

		if(OutAddr){
			*OutAddr = ntohl(SocketAddr.sin_addr.s_addr);
		}

		if(OutPort){
			*OutPort = ntohs(SocketAddr.sin_port);
		}

		return Socket;
	}
}

static void CloseConnection(TConnection *Connection){
	if(Connection->Socket != -1){
		close(Connection->Socket);
		Connection->Socket = -1;
	}
}

static TConnection *AssignConnection(int Socket, uint32 Addr, uint16 Port){
	int ConnectionIndex = -1;
	for(int i = 0; i < g_Config.MaxConnections; i += 1){
		if(g_Connections[i].State == CONNECTION_FREE){
			ConnectionIndex = i;
			break;
		}
	}

	TConnection *Connection = NULL;
	if(ConnectionIndex != -1){
		Connection = &g_Connections[ConnectionIndex];
		Connection->State = CONNECTION_READING;
		Connection->Socket = Socket;
		Connection->IPAddress = (int)Addr;
		Connection->StartTime = GetMonotonicUptime();
		Connection->RandomSeed = (uint32)rand();
		StringBufFormat(Connection->RemoteAddress,
				"%d.%d.%d.%d:%d",
				((Connection->IPAddress >> 24) & 0xFF),
				((Connection->IPAddress >> 16) & 0xFF),
				((Connection->IPAddress >>  8) & 0xFF),
				((Connection->IPAddress >>  0) & 0xFF),
				(int)Port);
		LOG("Connection %s assigned to slot %d",
				Connection->RemoteAddress, ConnectionIndex);
	}
	return Connection;
}

static void ReleaseConnection(TConnection *Connection){
	if(Connection->State != CONNECTION_FREE){
		LOG("Connection %s released", Connection->RemoteAddress);
		CloseConnection(Connection);
		memset(Connection, 0, sizeof(TConnection));
		Connection->State = CONNECTION_FREE;
	}
}

static void CheckConnectionInput(TConnection *Connection, int Events){
	if(Connection->Socket == -1 || (Events & POLLIN) == 0){
		return;
	}

	if(Connection->State != CONNECTION_READING){
		LOG_ERR("Connection %s (State: %d) sending out-of-order data",
				Connection->RemoteAddress, Connection->State);
		CloseConnection(Connection);
		return;
	}

	while(true){
		int ReadSize = (Connection->RWSize > 0 ? Connection->RWSize : 2);
		int BytesRead = (int)read(Connection->Socket,
				(Connection->Buffer + Connection->RWPosition),
				(ReadSize           - Connection->RWPosition));
		if(BytesRead == -1){
			if(errno != EAGAIN){
				// NOTE(fusion): Connection error.
				CloseConnection(Connection);
			}
			break;
		}else if(BytesRead == 0){
			// NOTE(fusion): Graceful close.
			CloseConnection(Connection);
			break;
		}

		Connection->RWPosition += BytesRead;
		if(Connection->RWPosition >= ReadSize){
			if(Connection->RWSize != 0){
				Connection->State = CONNECTION_PROCESSING;
				break;
			}else if(Connection->RWPosition == 2){
				int PayloadSize = (int)BufferRead16LE(Connection->Buffer);
				if(PayloadSize <= 0 || PayloadSize > (int)sizeof(Connection->Buffer)){
					CloseConnection(Connection);
					break;
				}

				Connection->RWSize = PayloadSize;
				Connection->RWPosition = 0;
			}else{
				PANIC("Invalid input state (State: %d, RWSize: %d, RWPosition: %d)",
						Connection->State, Connection->RWSize, Connection->RWPosition);
			}
		}
	}
}

static void CheckConnectionRequest(TConnection *Connection){
	if(Connection->Socket == -1){
		return;
	}

	if(Connection->State != CONNECTION_PROCESSING){
		return;
	}

	// PARANOID(fusion): A non-empty payload is guaranteed, due to how we parse
	// input in `CheckConnectionInput` just above.
	ASSERT(Connection->RWSize > 0);

	int Command = Connection->Buffer[0];
	if(Command == 1){
		ProcessLoginRequest(Connection);
	}else if(Command == 255){
		ProcessStatusRequest(Connection);
	}else{
		LOG_ERR("Invalid command %d from %s (expected 1 or 255)",
				Command, Connection->RemoteAddress);
		CloseConnection(Connection);
	}
}

static void CheckConnectionOutput(TConnection *Connection, int Events){
	// NOTE(fusion): We're only polling `POLLOUT` when the connection is WRITING,
	// but we want to allow requests to complete in a single cycle, so we always
	// check for output if the connection is WRITING.
	(void)Events;

	if(Connection->Socket == -1){
		return;
	}

	if(Connection->State != CONNECTION_WRITING){
		return;
	}

	while(true){
		int BytesWritten = (int)write(Connection->Socket,
				(Connection->Buffer + Connection->RWPosition),
				(Connection->RWSize - Connection->RWPosition));
		if(BytesWritten == -1){
			if(errno != EAGAIN){
				CloseConnection(Connection);
			}
			break;
		}

		Connection->RWPosition += BytesWritten;
		if(Connection->RWPosition >= Connection->RWSize){
			CloseConnection(Connection);
			break;
		}
	}
}

static void CheckConnection(TConnection *Connection, int Events){
	ASSERT((Events & POLLNVAL) == 0);

	if((Events & (POLLERR | POLLHUP)) != 0){
		CloseConnection(Connection);
	}

	if(g_Config.ConnectionTimeout > 0){
		int ElapsedTime = GetMonotonicUptime() - Connection->StartTime;
		if(ElapsedTime >= g_Config.ConnectionTimeout){
			LOG_WARN("Connection %s TIMEDOUT (ElapsedTime: %ds, Timeout: %ds)",
					Connection->RemoteAddress, ElapsedTime, g_Config.ConnectionTimeout);
			CloseConnection(Connection);
		}
	}

	if(Connection->Socket == -1){
		ReleaseConnection(Connection);
	}
}

static void AcceptConnections(int Events){
	ASSERT(g_Listener != -1);
	if((Events & POLLIN) == 0){
		return;
	}

	while(true){
		uint32 Addr;
		uint16 Port;
		int Socket = ListenerAccept(g_Listener, &Addr, &Port);
		if(Socket == -1){
			break;
		}

		if(AssignConnection(Socket, Addr, Port) == NULL){
			LOG_ERR("Rejecting connection %08X:%d:"
					" max number of connections reached (%d)",
					Addr, Port, g_Config.MaxConnections);
			close(Socket);
		}
	}
}

void ProcessConnections(void){
	int NumFds = 0;
	int MaxFds = g_MaxConnections + 1;
	pollfd *Fds = (pollfd*)alloca(MaxFds * sizeof(pollfd));
	int *ConnectionIndices = (int*)alloca(MaxFds * sizeof(int));

	if(g_Listener != -1){
		Fds[NumFds].fd = g_Listener;
		Fds[NumFds].events = POLLIN;
		Fds[NumFds].revents = 0;
		ConnectionIndices[NumFds] = -1;
		NumFds += 1;
	}

	for(int i = 0; i < g_Config.MaxConnections; i += 1){
		if(g_Connections[i].State == CONNECTION_FREE){
			continue;
		}

		Fds[NumFds].fd = g_Connections[i].Socket;
		Fds[NumFds].events = POLLIN;
		if(g_Connections[i].State == CONNECTION_WRITING){
			Fds[NumFds].events |= POLLOUT;
		}
		Fds[NumFds].revents = 0;
		ConnectionIndices[NumFds] = i;
		NumFds += 1;
	}

	// NOTE(fusion): Block for 1 second at most, so we can properly timeout
	// idle connections.
	ASSERT(NumFds > 0);
	int NumEvents = poll(Fds, NumFds, 1000);
	if(NumEvents == -1){
		if(errno != ETIMEDOUT && errno != EINTR){
			LOG_ERR("Failed to poll connections: (%d) %s",
					errno, strerrordesc_np(errno));
		}
		return;
	}

	// NOTE(fusion): Process connections.
	for(int i = 0; i < NumFds; i += 1){
		int Index = ConnectionIndices[i];
		int Events = (int)Fds[i].revents;
		if(Index >= 0 && Index < g_Config.MaxConnections){
			TConnection *Connection = &g_Connections[Index];
			CheckConnectionInput(Connection, Events);
			CheckConnectionRequest(Connection);
			CheckConnectionOutput(Connection, Events);
			CheckConnection(Connection, Events);
		}else if(Index == -1 && Fds[i].fd == g_Listener){
			AcceptConnections(Events);
		}else{
			LOG_ERR("Unknown connection index %d", Index);
		}
	}
}

bool InitConnections(void){
	ASSERT(g_PrivateKey == NULL);
	ASSERT(g_Listener == -1);
	ASSERT(g_Connections == NULL);
	ASSERT(g_StatusRecords == NULL);

	g_PrivateKey = RSALoadPEM("tibia.pem");
	if(g_PrivateKey == NULL){
		LOG_ERR("Failed to load RSA key");
		return false;
	}

	g_Listener = ListenerBind((uint16)g_Config.LoginPort);
	if(g_Listener == -1){
		LOG_ERR("Failed to bind listener to port %d", g_Config.LoginPort);
		return false;
	}

	g_MaxConnections = g_Config.MaxConnections;
	g_Connections = (TConnection*)calloc(
			g_MaxConnections, sizeof(TConnection));
	for(int i = 0; i < g_MaxConnections; i += 1){
		g_Connections[i].State = CONNECTION_FREE;
	}

	g_MaxStatusRecords = g_Config.MaxStatusRecords;
	g_StatusRecords = (TStatusRecord*)calloc(
			g_MaxStatusRecords, sizeof(TStatusRecord));

	return true;
}

void ExitConnections(void){
	if(g_PrivateKey != NULL){
		RSAFree(g_PrivateKey);
		g_PrivateKey = NULL;
	}

	if(g_Listener != -1){
		close(g_Listener);
		g_Listener = -1;
	}

	if(g_Connections != NULL){
		for(int i = 0; i < g_MaxConnections; i += 1){
			ReleaseConnection(&g_Connections[i]);
		}

		free(g_Connections);
		g_Connections = NULL;
	}

	if(g_StatusRecords != NULL){
		free(g_StatusRecords);
		g_StatusRecords = NULL;
	}
}

// Login Request
//==============================================================================
static TWriteBuffer PrepareXTEAResponse(TConnection *Connection){
	if(Connection->State != CONNECTION_PROCESSING){
		LOG_ERR("Connection %s is not PROCESSING (State: %d)",
				Connection->RemoteAddress, Connection->State);
		CloseConnection(Connection);
		return TWriteBuffer(NULL, 0);
	}

	TWriteBuffer WriteBuffer(Connection->Buffer, sizeof(Connection->Buffer));
	WriteBuffer.Write16(0); // Encrypted Size
	WriteBuffer.Write16(0); // Data Size
	return WriteBuffer;
}

static void SendXTEAResponse(TConnection *Connection, TWriteBuffer *WriteBuffer){
	if(Connection->State != CONNECTION_PROCESSING){
		LOG_ERR("Connection %s is not PROCESSING (State: %d)",
				Connection->RemoteAddress, Connection->State);
		CloseConnection(Connection);
		return;
	}

	ASSERT(WriteBuffer != NULL
		&& WriteBuffer->Buffer == Connection->Buffer
		&& WriteBuffer->Size == sizeof(Connection->Buffer)
		&& WriteBuffer->Position > 4);

	int DataSize = WriteBuffer->Position - 4;
	int EncryptedSize = WriteBuffer->Position - 2;
	while((EncryptedSize % 8) != 0){
		WriteBuffer->Write8(rand_r(&Connection->RandomSeed));
		EncryptedSize += 1;
	}

	if(WriteBuffer->Overflowed()){
		LOG_ERR("Write buffer overflowed when writing response to %s",
				Connection->RemoteAddress);
		CloseConnection(Connection);
		return;
	}

	WriteBuffer->Rewrite16(0, EncryptedSize);
	WriteBuffer->Rewrite16(2, DataSize);
	XTEAEncrypt(Connection->XTEA,
			WriteBuffer->Buffer + 2,
			WriteBuffer->Position - 2);
	Connection->State = CONNECTION_WRITING;
	Connection->RWSize = WriteBuffer->Position;
	Connection->RWPosition = 0;
}

static void SendLoginError(TConnection *Connection, const char *Message){
	TWriteBuffer WriteBuffer = PrepareXTEAResponse(Connection);
	WriteBuffer.Write8(10); // LOGIN_ERROR
	WriteBuffer.WriteString(Message);
	SendXTEAResponse(Connection, &WriteBuffer);
}

static void SendCharacterList(TConnection *Connection, int NumCharacters,
		TCharacterLoginData *Characters, int PremiumDays){
	TWriteBuffer WriteBuffer = PrepareXTEAResponse(Connection);

	if(g_Config.Motd[0] != 0){
		WriteBuffer.Write8(20); // MOTD
		WriteBuffer.WriteString(g_Config.Motd);
	}

	WriteBuffer.Write8(100); // CHARACTER_LIST
	if(NumCharacters > UINT8_MAX){
		NumCharacters = UINT8_MAX;
	}
	WriteBuffer.Write8(NumCharacters);
	for(int i = 0; i < NumCharacters; i += 1){
		WriteBuffer.WriteString(Characters[i].Name);
		WriteBuffer.WriteString(Characters[i].WorldName);
		WriteBuffer.Write32BE((uint32)Characters[i].WorldAddress);
		WriteBuffer.Write16((uint16)Characters[i].WorldPort);
	}
	WriteBuffer.Write16((uint16)PremiumDays);

	SendXTEAResponse(Connection, &WriteBuffer);
}

void ProcessLoginRequest(TConnection *Connection){
	if(Connection->RWSize != 145){
		LOG_ERR("Invalid login request size from %s (expected 145, got %d)",
				Connection->RemoteAddress, Connection->RWSize);
		CloseConnection(Connection);
		return;
	}

	TReadBuffer ReadBuffer(Connection->Buffer, Connection->RWSize);
	ReadBuffer.Read8(); // always 1 for a login request
	int TerminalType = ReadBuffer.Read16();
	int TerminalVersion = ReadBuffer.Read16();
	ReadBuffer.Read32(); // DATSIGNATURE
	ReadBuffer.Read32(); // SPRSIGNATURE
	ReadBuffer.Read32(); // PICSIGNATURE

	uint8 AsymmetricData[128];
	ReadBuffer.ReadBytes(AsymmetricData, sizeof(AsymmetricData));
	if(ReadBuffer.Overflowed()){
		LOG_ERR("Input buffer overflowed while reading login command from %s",
				Connection->RemoteAddress);
		CloseConnection(Connection);
		return;
	}

	// IMPORTANT(fusion): Without a checksum, there is no way of validating
	// the asymmetric data. The best we can do is to verify that the first
	// plaintext byte is ZERO, but that alone isn't enough.
	if(!RSADecrypt(g_PrivateKey, AsymmetricData, sizeof(AsymmetricData)) || AsymmetricData[0] != 0){
		LOG_ERR("Failed to decrypt asymmetric data from %s",
				Connection->RemoteAddress);
		CloseConnection(Connection);
		return;
	}

	ReadBuffer = TReadBuffer(AsymmetricData, sizeof(AsymmetricData));
	ReadBuffer.Read8(); // always zero
	Connection->XTEA[0] = ReadBuffer.Read32();
	Connection->XTEA[1] = ReadBuffer.Read32();
	Connection->XTEA[2] = ReadBuffer.Read32();
	Connection->XTEA[3] = ReadBuffer.Read32();

	char Password[30];
	int AccountID = ReadBuffer.Read32();
	ReadBuffer.ReadString(Password, sizeof(Password));
	if(ReadBuffer.Overflowed()){
		LOG_ERR("Malformed asymmetric data from %s", Connection->RemoteAddress);
		CloseConnection(Connection);
		return;
	}

	if(AccountID <= 0){
		SendLoginError(Connection, "You must enter an account number.");
		return;
	}

	if(TerminalType < 0 || TerminalType >= NARRAY(TERMINALVERSION)
			|| TERMINALVERSION[TerminalType] != TerminalVersion){
		SendLoginError(Connection,
				"Your terminal version is too old.\n"
				"Please get a new version at\n"
				"http://www.tibia.com.");
		return;
	}

	char IPString[16];
	StringBufFormat(IPString, "%d.%d.%d.%d",
			((Connection->IPAddress >> 24) & 0xFF),
			((Connection->IPAddress >> 16) & 0xFF),
			((Connection->IPAddress >>  8) & 0xFF),
			((Connection->IPAddress >>  0) & 0xFF));

	int NumCharacters = 0;
	int PremiumDays = 0;
	TCharacterLoginData Characters[50];
	int LoginCode = LoginAccount(AccountID, Password, IPString,
			NARRAY(Characters), &NumCharacters, Characters, &PremiumDays);
	switch(LoginCode){
		case 0:{
			SendCharacterList(Connection, NumCharacters, Characters, PremiumDays);
			break;
		}

		case 1:		// Invalid account number
		case 2:{	// Invalid password
			SendLoginError(Connection, "Accountnumber or password is not correct.");
			break;
		}

		case 3:{
			SendLoginError(Connection, "Account disabled for five minutes. Please wait.");
			break;
		}

		case 4:{
			SendLoginError(Connection, "IP address blocked for 30 minutes. Please wait.");
			break;
		}

		case 5:{
			SendLoginError(Connection, "Your account is banished.");
			break;
		}

		case 6:{
			SendLoginError(Connection, "Your IP address is banished.");
			break;
		}

		default:{
			if(LoginCode != -1){
				LOG_ERR("Invalid login code %d", LoginCode);
			}
			SendLoginError(Connection, "Internal error, closing connection.");
			break;
		}
	}
}

// Status Request
//==============================================================================
static bool AllowStatusRequest(int IPAddress){
	TStatusRecord *Record = NULL;
	int LeastRecentlyUsedIndex = 0;
	int LeastRecentlyUsedTime = g_StatusRecords[0].Timestamp;
	int TimeNow = GetMonotonicUptime();
	for(int i = 0; i < g_MaxStatusRecords; i += 1){
		if(g_StatusRecords[i].Timestamp < LeastRecentlyUsedTime){
			LeastRecentlyUsedIndex = i;
			LeastRecentlyUsedTime = g_StatusRecords[i].Timestamp;
		}

		if(g_StatusRecords[i].IPAddress == IPAddress){
			Record = &g_StatusRecords[i];
			break;
		}
	}

	bool Result = false;
	if(Record == NULL){
		Record = &g_StatusRecords[LeastRecentlyUsedIndex];
		Record->IPAddress = IPAddress;
		Record->Timestamp = TimeNow;
		Result = true;
	}else if((TimeNow - Record->Timestamp) >= g_Config.MinStatusInterval){
		Record->Timestamp = TimeNow;
		Result = true;
	}

	return Result;
}

static void SendStatusString(TConnection *Connection, const char *StatusString){
	if(Connection->State != CONNECTION_PROCESSING){
		LOG_ERR("Connection %s is not PROCESSING (State: %d)",
				Connection->RemoteAddress, Connection->State);
		CloseConnection(Connection);
		return;
	}

	int Length = (int)strlen(StatusString);
	if(Length > (int)sizeof(Connection->Buffer)){
		Length = (int)sizeof(Connection->Buffer);
	}

	Connection->RWSize = Length;
	Connection->RWPosition = 0;
	memcpy(Connection->Buffer, StatusString, Length);
	Connection->State = CONNECTION_WRITING;
}

void ProcessStatusRequest(TConnection *Connection){
	if(!AllowStatusRequest(Connection->IPAddress)){
		LOG_ERR("Too many status requests from %s", Connection->RemoteAddress);
		CloseConnection(Connection);
		return;
	}

	TReadBuffer ReadBuffer(Connection->Buffer, Connection->RWSize);
	ReadBuffer.Read8(); // always 255 for a status request
	int Format = (int)ReadBuffer.Read8();
	if(Format == 255){ // XML
		char Request[5] = {};
		ReadBuffer.ReadBytes((uint8*)Request, 4);
		if(StringEqCI(Request, "info")){
			const char *StatusString = GetStatusString();
			SendStatusString(Connection, StatusString);
		}else{
			LOG_WARN("Invalid status request \"%s\" from %s",
					Request, Connection->RemoteAddress);
			CloseConnection(Connection);
		}
	}else{
		LOG_WARN("Invalid status format %d from %s",
				Format, Connection->RemoteAddress);
		CloseConnection(Connection);
	}
}

