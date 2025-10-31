#include "login.hh"

// TODO(fusion): Support windows eventually?
#if OS_LINUX
#	include <errno.h>
#	include <fcntl.h>
#	include <netinet/in.h>
#	include <poll.h>
#	include <sys/socket.h>
#	include <unistd.h>
#	include <time.h>
#else
#	error "Operating system not currently supported."
#endif

#if TIBIA772
static const int TERMINALVERSION[]	= {772, 772, 772};
#else
static const int TERMINALVERSION[]	= {770, 770, 770};
#endif

static RSAKey *g_PrivateKey			= NULL;
static int g_Listener				= -1;
static TConnection *g_Connections	= NULL;

// Connection Handling
//==============================================================================
// NOTE(fusion): This is very similar to the connection handling code used by the
// query manager with a few subtle differences including the encryption scheme.
int ListenerBind(uint16 Port){
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

int ListenerAccept(int Listener, uint32 *OutAddr, uint16 *OutPort){
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

void CloseConnection(TConnection *Connection){
	if(Connection->Socket != -1){
		close(Connection->Socket);
		Connection->Socket = -1;
	}
}

TConnection *AssignConnection(int Socket, uint32 Addr, uint16 Port){
	int ConnectionIndex = -1;
	for(int i = 0; i < g_MaxConnections; i += 1){
		if(g_Connections[i].State == CONNECTION_FREE){
			ConnectionIndex = i;
			break;
		}
	}

	TConnection *Connection = NULL;
	if(ConnectionIndex != -1){
		Connection = &g_Connections[ConnectionIndex];
		Connection->State = CONNECTION_HANDSHAKE;
		Connection->Socket = Socket;
		Connection->StartTime = g_MonotonicTimeMS;
		Connection->RandomSeed = (uint32)rand();
		snprintf(Connection->IPAddress, sizeof(Connection->IPAddress),
				"%d.%d.%d.%d", ((int)(Addr >> 24) & 0xFF), ((int)(Addr >> 16) & 0xFF),
				((int)(Addr >>  8) & 0xFF), ((int)(Addr >>  0) & 0xFF));
		snprintf(Connection->RemoteAddress, sizeof(Connection->RemoteAddress),
				"%s:%d", Connection->IPAddress, (int)Port);
		LOG("Connection %s assigned to slot %d",
				Connection->RemoteAddress, ConnectionIndex);
	}
	return Connection;
}

void ReleaseConnection(TConnection *Connection){
	if(Connection->State != CONNECTION_FREE){
		LOG("Connection %s released", Connection->RemoteAddress);
		CloseConnection(Connection);
		memset(Connection, 0, sizeof(TConnection));
		Connection->State = CONNECTION_FREE;
	}
}

void CheckConnectionInput(TConnection *Connection, int Events){
	if((Events & POLLIN) == 0 || Connection->Socket == -1){
		return;
	}

	if(Connection->State != CONNECTION_HANDSHAKE){
		LOG_ERR("Connection %s (State: %d) sending out-of-order data",
				Connection->RemoteAddress, Connection->State);
		CloseConnection(Connection);
		return;
	}

	while(true){
		int ReadSize = Connection->RWSize;
		if(ReadSize == 0){
			ReadSize = 2 - Connection->RWPosition;
			ASSERT(ReadSize > 0);
		}

		int BytesRead = read(Connection->Socket,
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
			if(Connection->RWSize == 0){
				int PayloadSize = BufferRead16LE(Connection->Buffer);
				if(PayloadSize <= 0 || PayloadSize > NARRAY(Connection->Buffer)){
					CloseConnection(Connection);
					break;
				}

				Connection->RWSize = PayloadSize;
				Connection->RWPosition = 0;
			}else{
				Connection->State = CONNECTION_LOGIN;
				break;
			}
		}
	}

	if(Connection->State == CONNECTION_LOGIN){
		ProcessLoginRequest(Connection);
	}
}

void CheckConnectionOutput(TConnection *Connection, int Events){
	if((Events & POLLOUT) == 0 || Connection->Socket == -1){
		return;
	}

	if(Connection->State != CONNECTION_WRITE){
		return;
	}

	while(true){
		int BytesWritten = write(Connection->Socket,
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

void CheckConnection(TConnection *Connection, int Events){
	ASSERT((Events & POLLNVAL) == 0);

	if((Events & (POLLERR | POLLHUP)) != 0){
		CloseConnection(Connection);
	}

	if(g_LoginTimeout > 0){
		int LoginTime = (g_MonotonicTimeMS - Connection->StartTime);
		if(LoginTime >= g_LoginTimeout){
			LOG_WARN("Connection %s TIMEDOUT (LoginTime: %dms, Timeout: %dms)",
					Connection->RemoteAddress, LoginTime, g_LoginTimeout);
			CloseConnection(Connection);
		}
	}

	if(Connection->Socket == -1){
		ReleaseConnection(Connection);
	}
}

void ProcessConnections(void){
	// NOTE(fusion): Accept new connections.
	while(true){
		uint32 Addr;
		uint16 Port;
		int Socket = ListenerAccept(g_Listener, &Addr, &Port);
		if(Socket == -1){
			break;
		}

		if(AssignConnection(Socket, Addr, Port) == NULL){
			LOG_ERR("Rejecting connection from %08X:%d due to max number of"
					" connections being reached (%d)", Addr, Port, g_MaxConnections);
			close(Socket);
		}
	}

	// NOTE(fusion): Gather active connections.
	int NumConnections = 0;
	int *ConnectionIndices = (int*)alloca(g_MaxConnections * sizeof(int));
	pollfd *ConnectionFds  = (pollfd*)alloca(g_MaxConnections * sizeof(pollfd));
	for(int i = 0; i < g_MaxConnections; i += 1){
		if(g_Connections[i].State == CONNECTION_FREE || g_Connections[i].Socket == -1){
			continue;
		}

		ConnectionIndices[NumConnections] = i;
		ConnectionFds[NumConnections].fd = g_Connections[i].Socket;
		ConnectionFds[NumConnections].events = POLLIN | POLLOUT;
		ConnectionFds[NumConnections].revents = 0;
		NumConnections += 1;
	}

	if(NumConnections <= 0){
		return;
	}

	// NOTE(fusion): Poll connections.
	int NumEvents = poll(ConnectionFds, NumConnections, 0);
	if(NumEvents == -1){
		LOG_ERR("Failed to poll connections: (%d) %s", errno, strerrordesc_np(errno));
		return;
	}

	// NOTE(fusion): Process connections.
	for(int i = 0; i < NumConnections; i += 1){
		TConnection *Connection = &g_Connections[ConnectionIndices[i]];
		int Events = (int)ConnectionFds[i].revents;
		CheckConnectionInput(Connection, Events);
		CheckConnectionOutput(Connection, Events);
		CheckConnection(Connection, Events);
	}
}

bool InitConnections(void){
	ASSERT(g_Listener == -1);
	ASSERT(g_Connections == NULL);

	LOG("Login port: %d", g_LoginPort);
	LOG("Max connections: %d", g_MaxConnections);
	LOG("Login timeout: %dms", g_LoginTimeout);

	g_PrivateKey = RSALoadPEM("tibia.pem");
	if(g_PrivateKey == NULL){
		LOG_ERR("Failed to load RSA key");
		return false;
	}

	g_Listener = ListenerBind(g_LoginPort);
	if(g_Listener == -1){
		LOG_ERR("Failed to bind listener");
		return false;
	}

	g_Connections = (TConnection*)calloc(g_MaxConnections, sizeof(TConnection));
	for(int i = 0; i < g_MaxConnections; i += 1){
		g_Connections[i].State = CONNECTION_FREE;
	}

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
}

// Connection Requests
//==============================================================================
TWriteBuffer PrepareResponse(TConnection *Connection){
	if(Connection->State != CONNECTION_LOGIN){
		LOG_ERR("Connection %s is not processing login (State: %d)",
				Connection->RemoteAddress, Connection->State);
		CloseConnection(Connection);
		return TWriteBuffer(NULL, 0);
	}

	TWriteBuffer WriteBuffer(Connection->Buffer, sizeof(Connection->Buffer));
	WriteBuffer.Write16(0); // Encrypted Size
	WriteBuffer.Write16(0); // Data Size
	return WriteBuffer;
}

void SendResponse(TConnection *Connection, TWriteBuffer *WriteBuffer){
	if(Connection->State != CONNECTION_LOGIN){
		LOG_ERR("Connection %s is not processing login (State: %d)",
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
	Connection->State = CONNECTION_WRITE;
	Connection->RWSize = WriteBuffer->Position;
	Connection->RWPosition = 0;
}

void SendLoginError(TConnection *Connection, const char *Message){
	TWriteBuffer WriteBuffer = PrepareResponse(Connection);
	WriteBuffer.Write8(10); // LOGIN_ERROR
	WriteBuffer.WriteString(Message);
	SendResponse(Connection, &WriteBuffer);
}

void SendCharacterList(TConnection *Connection, int NumCharacters,
		TCharacterLoginData *Characters, int PremiumDays){
	TWriteBuffer WriteBuffer = PrepareResponse(Connection);

	if(g_Motd[0] != 0){
		WriteBuffer.Write8(20); // MOTD
		WriteBuffer.WriteString(g_Motd);
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

	SendResponse(Connection, &WriteBuffer);
}

void ProcessLoginRequest(TConnection *Connection){
	if(Connection->RWSize != 145){
		LOG_ERR("Invalid login request size %d from %s (expected 145)",
				Connection->RWSize, Connection->RemoteAddress);
		CloseConnection(Connection);
		return;
	}

	TReadBuffer InputBuffer(Connection->Buffer, Connection->RWSize);
	int Command = InputBuffer.Read8();
	if(Command != 1){
		LOG_ERR("Invalid login command %d from %s (expected 1)",
				Command, Connection->RemoteAddress);
		CloseConnection(Connection);
		return;
	}

	int TerminalType = InputBuffer.Read16();
	int TerminalVersion = InputBuffer.Read16();
	InputBuffer.Read32(); // DATSIGNATURE
	InputBuffer.Read32(); // SPRSIGNATURE
	InputBuffer.Read32(); // PICSIGNATURE

	uint8 AsymmetricData[128];
	InputBuffer.ReadBytes(AsymmetricData, sizeof(AsymmetricData));
	if(InputBuffer.Overflowed()){
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

	TReadBuffer Buffer(AsymmetricData, sizeof(AsymmetricData));
	Buffer.Read8(); // always zero
	Connection->XTEA[0] = Buffer.Read32();
	Connection->XTEA[1] = Buffer.Read32();
	Connection->XTEA[2] = Buffer.Read32();
	Connection->XTEA[3] = Buffer.Read32();

	char Password[30];
	int AccountID = Buffer.Read32();
	Buffer.ReadString(Password, sizeof(Password));
	if(Buffer.Overflowed()){
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

	int NumCharacters = 0;
	int PremiumDays = 0;
	TCharacterLoginData Characters[50];
	int LoginCode = LoginAccount(AccountID, Password, Connection->IPAddress,
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
