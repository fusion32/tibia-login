#include "common.hh"

#include <errno.h>
#include <signal.h>

int64   g_StartTimeMS    = 0;
int     g_ShutdownSignal = 0;
TConfig g_Config         = {};

void LogAdd(const char *Prefix, const char *Format, ...){
	char Entry[4096];
	va_list ap;
	va_start(ap, Format);
	vsnprintf(Entry, sizeof(Entry), Format, ap);
	va_end(ap);

	// NOTE(fusion): Trim trailing whitespace.
	int Length = (int)strlen(Entry);
	while(Length > 0 && isspace(Entry[Length - 1])){
		Entry[Length - 1] = 0;
		Length -= 1;
	}

	if(Length > 0){
		char TimeString[128];
		StringBufFormatTime(TimeString, "%Y-%m-%d %H:%M:%S", (int)time(NULL));
		fprintf(stdout, "%s [%s] %s\n", TimeString, Prefix, Entry);
		fflush(stdout);
	}
}

void LogAddVerbose(const char *Prefix, const char *Function,
		const char *File, int Line, const char *Format, ...){
	char Entry[4096];
	va_list ap;
	va_start(ap, Format);
	vsnprintf(Entry, sizeof(Entry), Format, ap);
	va_end(ap);

	// NOTE(fusion): Trim trailing whitespace.
	int Length = (int)strlen(Entry);
	while(Length > 0 && isspace(Entry[Length - 1])){
		Entry[Length - 1] = 0;
		Length -= 1;
	}

	if(Length > 0){
		(void)File;
		(void)Line;
		char TimeString[128];
		StringBufFormatTime(TimeString, "%Y-%m-%d %H:%M:%S", (int)time(NULL));
		fprintf(stdout, "%s [%s] %s: %s\n", TimeString, Prefix, Function, Entry);
		fflush(stdout);
	}
}

struct tm GetLocalTime(time_t t){
	struct tm result;
#if COMPILER_MSVC
	localtime_s(&result, &t);
#else
	localtime_r(&t, &result);
#endif
	return result;
}

struct tm GetGMTime(time_t t){
	struct tm result;
#if COMPILER_MSVC
	gmtime_s(&result, &t);
#else
	gmtime_r(&t, &result);
#endif
	return result;
}

int64 GetClockMonotonicMS(void){
#if OS_WINDOWS
	LARGE_INTEGER Counter, Frequency;
	QueryPerformanceCounter(&Counter);
	QueryPerformanceFrequency(&Frequency);
	return (int64)((Counter.QuadPart * 1000) / Frequency.QuadPart);
#else
	// NOTE(fusion): The coarse monotonic clock has a larger resolution but is
	// supposed to be faster, even avoiding system calls in some cases. It should
	// be fine for millisecond precision which is what we're using.
	struct timespec Time;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &Time);
	return ((int64)Time.tv_sec * 1000)
		+ ((int64)Time.tv_nsec / 1000000);
#endif
}

int GetMonotonicUptime(void){
	return (int)((GetClockMonotonicMS() - g_StartTimeMS) / 1000);
}

bool StringEmpty(const char *String){
	return String[0] == 0;
}

bool StringEq(const char *A, const char *B){
	int Index = 0;
	while(A[Index] != 0 && A[Index] == B[Index]){
		Index += 1;
	}
	return A[Index] == B[Index];
}

bool StringEqCI(const char *A, const char *B){
	int Index = 0;
	while(A[Index] != 0 && tolower(A[Index]) == tolower(B[Index])){
		Index += 1;
	}
	return tolower(A[Index]) == tolower(B[Index]);
}

bool StringCopy(char *Dest, int DestCapacity, const char *Src){
	int SrcLength = (Src != NULL ? (int)strlen(Src) : 0);
	return StringCopyN(Dest, DestCapacity, Src, SrcLength);
}

bool StringCopyN(char *Dest, int DestCapacity, const char *Src, int SrcLength){
	ASSERT(DestCapacity > 0);
	bool Result = (SrcLength < DestCapacity);
	if(Result && SrcLength > 0){
		memcpy(Dest, Src, SrcLength);
		Dest[SrcLength] = 0;
	}else{
		Dest[0] = 0;
	}
	return Result;
}

bool StringFormat(char *Dest, int DestCapacity, const char *Format, ...){
	va_list ap;
	va_start(ap, Format);
	int Written = vsnprintf(Dest, DestCapacity, Format, ap);
	va_end(ap);
	return Written >= 0 && Written < DestCapacity;
}

bool StringFormatTime(char *Dest, int DestCapacity, const char *Format, int Timestamp){
	struct tm tm = GetLocalTime((time_t)Timestamp);
	int Result = (int)strftime(Dest, DestCapacity, Format, &tm);

	// NOTE(fusion): `strftime` will return ZERO if it's unable to fit the result
	// in the supplied buffer, which is annoying because ZERO may not represent a
	// failure if the result is an empty string.
	ASSERT(Result >= 0 && Result < DestCapacity);
	if(Result == 0){
		memset(Dest, 0, DestCapacity);
	}

	return Result != 0;
}

void StringClear(char *Dest, int DestCapacity){
	ASSERT(DestCapacity > 0);
	memset(Dest, 0, DestCapacity);
}

uint32 StringHash(const char *String){
	// FNV1a 32-bits
	uint32 Hash = 0x811C9DC5U;
	for(int i = 0; String[i] != 0; i += 1){
		Hash ^= (uint32)String[i];
		Hash *= 0x01000193U;
	}
	return Hash;
}

bool StringEscape(char *Dest, int DestCapacity, const char *Src){
	int WritePos = 0;
	for(int ReadPos = 0; Src[ReadPos] != 0 && WritePos < DestCapacity; ReadPos += 1){
		int EscapeCh = -1;
		switch(Src[ReadPos]){
			case '\a': EscapeCh = 'a'; break;
			case '\b': EscapeCh = 'b'; break;
			case '\t': EscapeCh = 't'; break;
			case '\n': EscapeCh = 'n'; break;
			case '\v': EscapeCh = 'v'; break;
			case '\f': EscapeCh = 'f'; break;
			case '\r': EscapeCh = 'r'; break;
			case '\"': EscapeCh = '\"'; break;
			case '\'': EscapeCh = '\''; break;
			case '\\': EscapeCh = '\\'; break;
		}

		if(EscapeCh != -1){
			if((WritePos + 1) <= DestCapacity){
				Dest[WritePos] = '\\';
				WritePos += 1;
			}

			if((WritePos + 1) <= DestCapacity){
				Dest[WritePos] = EscapeCh;
				WritePos += 1;
			}
		}else{
			if((WritePos + 1) <= DestCapacity){
				Dest[WritePos] = Src[ReadPos];
				WritePos += 1;
			}
		}
	}

	if(WritePos < DestCapacity){
		Dest[WritePos] = 0;
		return true;
	}else{
		Dest[DestCapacity - 1] = 0;
		return false;
	}
}

int UTF8SequenceSize(uint8 LeadingByte){
	if((LeadingByte & 0x80) == 0){
		return 1;
	}else if((LeadingByte & 0xE0) == 0xC0){
		return 2;
	}else if((LeadingByte & 0xF0) == 0xE0){
		return 3;
	}else if((LeadingByte & 0xF8) == 0xF0){
		return 4;
	}else{
		return 0;
	}
}

bool UTF8IsTrailingByte(uint8 Byte){
	return (Byte & 0xC0) == 0x80;
}

int UTF8EncodedSize(int Codepoint){
	if(Codepoint < 0){
		return 0;
	}else if(Codepoint <= 0x7F){
		return 1;
	}else if(Codepoint <= 0x07FF){
		return 2;
	}else if(Codepoint <= 0xFFFF){
		return 3;
	}else if(Codepoint <= 0x10FFFF){
		return 4;
	}else{
		return 0;
	}
}

int UTF8FindNextLeadingByte(const char *Src, int SrcLength){
	int Offset = 0;
	while(Offset < SrcLength){
		// NOTE(fusion): Allow the first byte to be a leading byte, in case we
		// just want to advance from one leading byte to another.
		if(Offset > 0 && !UTF8IsTrailingByte(Src[Offset])){
			break;
		}
		Offset += 1;
	}
	return Offset;
}

int UTF8DecodeOne(const uint8 *Src, int SrcLength, int *OutCodepoint){
	if(SrcLength <= 0){
		return 0;
	}

	int Size = UTF8SequenceSize(Src[0]);
	if(Size <= 0 || Size > SrcLength){
		return 0;
	}

	for(int i = 1; i < Size; i += 1){
		if(!UTF8IsTrailingByte(Src[i])){
			return 0;
		}
	}

	int Codepoint = 0;
	switch(Size){
		case 1:{
			Codepoint = (int)Src[0];
			break;
		}

		case 2:{
			Codepoint = ((int)(Src[0] & 0x1F) <<  6)
					|   ((int)(Src[1] & 0x3F) <<  0);
			break;
		}

		case 3:{
			Codepoint = ((int)(Src[0] & 0x0F) << 12)
					|   ((int)(Src[1] & 0x3F) <<  6)
					|   ((int)(Src[2] & 0x3F) <<  0);
			break;
		}

		case 4:{
			Codepoint = ((int)(Src[0] & 0x07) << 18)
					|   ((int)(Src[1] & 0x3F) << 12)
					|   ((int)(Src[2] & 0x3F) <<  6)
					|   ((int)(Src[3] & 0x3F) <<  0);
			break;
		}
	}

	if(OutCodepoint){
		*OutCodepoint = Codepoint;
	}

	return Size;
}

int UTF8EncodeOne(uint8 *Dest, int DestCapacity, int Codepoint){
	int Size = UTF8EncodedSize(Codepoint);
	if(Size > 0 && Size <= DestCapacity){
		switch(Size){
			case 1:{
				Dest[0] = (uint8)Codepoint;
				break;
			}

			case 2:{
				Dest[0] = (uint8)(0xC0 | (0x1F & (Codepoint >>	6)));
				Dest[1] = (uint8)(0x80 | (0x3F & (Codepoint >>	0)));
				break;
			}

			case 3:{
				Dest[0] = (uint8)(0xE0 | (0x0F & (Codepoint >> 12)));
				Dest[1] = (uint8)(0x80 | (0x3F & (Codepoint >>	6)));
				Dest[2] = (uint8)(0x80 | (0x3F & (Codepoint >>	0)));
				break;
			}

			case 4:{
				Dest[0] = (uint8)(0xF0 | (0x07 & (Codepoint >> 18)));
				Dest[1] = (uint8)(0x80 | (0x3F & (Codepoint >> 12)));
				Dest[2] = (uint8)(0x80 | (0x3F & (Codepoint >>	6)));
				Dest[3] = (uint8)(0x80 | (0x3F & (Codepoint >>	0)));
				break;
			}
		}
	}

	return Size;
}

// IMPORTANT(fusion): This function WON'T handle null-termination. It'll rather
// convert any characters, INCLUDING the null-terminator, contained in the src
// string. Invalid or NON-LATIN1 codepoints are translated into '?'.
int UTF8ToLatin1(char *Dest, int DestCapacity, const char *Src, int SrcLength){
	int ReadPos = 0;
	int WritePos = 0;
	while(ReadPos < SrcLength){
		int Codepoint = -1;
		int Size = UTF8DecodeOne((uint8*)(Src + ReadPos), (SrcLength - ReadPos), &Codepoint);
		if(Size > 0){
			ReadPos += Size;
		}else{
			ReadPos += UTF8FindNextLeadingByte((Src + ReadPos), (SrcLength - ReadPos));
		}

		if(WritePos < DestCapacity){
			if(Codepoint >= 0 && Codepoint <= 0xFF){
				Dest[WritePos] = (char)Codepoint;
			}else{
				Dest[WritePos] = '?';
			}
		}
		WritePos += 1;
	}

	return WritePos;
}

// IMPORTANT(fusion): This function WON'T handle null-termination. It'll rather
// convert any characters, INCLUDING the null-terminator, contained in the src
// string. Note that LATIN1 characters translates directly into UNICODE codepoints.
int Latin1ToUTF8(char *Dest, int DestCapacity, const char *Src, int SrcLength){
	int WritePos = 0;
	for(int ReadPos = 0; ReadPos < SrcLength; ReadPos += 1){
		WritePos += UTF8EncodeOne((uint8*)(Dest + WritePos),
				(DestCapacity - WritePos), (uint8)Src[ReadPos]);
	}
	return WritePos;
}

bool ParseBoolean(bool *Dest, const char *String){
	ASSERT(Dest && String);
	*Dest = StringEqCI(String, "true")
			|| StringEqCI(String, "on")
			|| StringEqCI(String, "yes");
	return *Dest
			|| StringEqCI(String, "false")
			|| StringEqCI(String, "off")
			|| StringEqCI(String, "no");
}

bool ParseInteger(int *Dest, const char *String){
	ASSERT(Dest && String);
	const char *StringEnd;
	*Dest = (int)strtol(String, (char**)&StringEnd, 0);
	return StringEnd > String;
}

bool ParseDuration(int *Dest, const char *String){
	ASSERT(Dest && String);
	const char *Suffix;
	*Dest = (int)strtol(String, (char**)&Suffix, 0);
	if(Suffix == String){
		return false;
	}

	while(Suffix[0] != 0 && isspace(Suffix[0])){
		Suffix += 1;
	}

	if(Suffix[0] == 'S' || Suffix[0] == 's'){
		*Dest *= (1);
	}else if(Suffix[0] == 'M' || Suffix[0] == 'm'){
		*Dest *= (60);
	}else if(Suffix[0] == 'H' || Suffix[0] == 'h'){
		*Dest *= (60 * 60);
	}

	return true;
}

bool ParseSize(int *Dest, const char *String){
	ASSERT(Dest && String);
	const char *Suffix;
	*Dest = (int)strtol(String, (char**)&Suffix, 0);
	if(Suffix == String){
		return false;
	}

	while(Suffix[0] != 0 && isspace(Suffix[0])){
		Suffix += 1;
	}

	if(Suffix[0] == 'K' || Suffix[0] == 'k'){
		*Dest *= (1024);
	}else if(Suffix[0] == 'M' || Suffix[0] == 'm'){
		*Dest *= (1024 * 1024);
	}

	return true;
}

bool ParseString(char *Dest, int DestCapacity, const char *String){
	ASSERT(Dest && DestCapacity > 0 && String);
	int StringStart = 0;
	int StringEnd = (int)strlen(String);
	if(StringEnd >= 2){
		if((String[0] == '"' && String[StringEnd - 1] == '"')
		|| (String[0] == '\'' && String[StringEnd - 1] == '\'')
		|| (String[0] == '`' && String[StringEnd - 1] == '`')){
			StringStart += 1;
			StringEnd -= 1;
		}
	}

	return StringCopyN(Dest, DestCapacity,
			&String[StringStart], (StringEnd - StringStart));
}

void ParseMotd(char *Dest, int DestCapacity, const char *String){
	char *Motd = (char*)alloca(DestCapacity);
	ParseString(Motd, DestCapacity, String);
	if(Motd[0] != 0){
		StringFormat(Dest, DestCapacity, "%u\n%s", StringHash(Motd), Motd);
	}
}

bool ReadConfig(const char *FileName, TConfig *Config){
	FILE *File = fopen(FileName, "rb");
	if(File == NULL){
		LOG_ERR("Failed to open config file \"%s\"", FileName);
		return false;
	}

	bool EndOfFile = false;
	for(int LineNumber = 1; !EndOfFile; LineNumber += 1){
		const int MaxLineSize = 1024;
		char Line[MaxLineSize];
		int LineSize = 0;
		int KeyStart = -1;
		int EqualPos = -1;
		while(true){
			int ch = fgetc(File);
			if(ch == EOF || ch == '\n'){
				if(ch == EOF){
					EndOfFile = true;
				}
				break;
			}

			if(LineSize < MaxLineSize){
				Line[LineSize] = (char)ch;
			}

			if(KeyStart == -1 && !isspace(ch)){
				KeyStart = LineSize;
			}

			if(EqualPos == -1 && ch == '='){
				EqualPos = LineSize;
			}

			LineSize += 1;
		}

		// NOTE(fusion): Check line size limit.
		if(LineSize > MaxLineSize){
			LOG_WARN("%s:%d: Exceeded line size limit of %d characters",
					FileName, LineNumber, MaxLineSize);
			continue;
		}

		// NOTE(fusion): Check empty line or comment.
		if(KeyStart == -1 || Line[KeyStart] == '#'){
			continue;
		}

		// NOTE(fusion): Check assignment.
		if(EqualPos == -1){
			LOG_WARN("%s:%d: No assignment found on non empty line",
					FileName, LineNumber);
			continue;
		}

		// NOTE(fusion): Check empty key.
		int KeyEnd = EqualPos;
		while(KeyEnd > KeyStart && isspace(Line[KeyEnd - 1])){
			KeyEnd -= 1;
		}

		if(KeyStart == KeyEnd){
			LOG_WARN("%s:%d: Empty key", FileName, LineNumber);
			continue;
		}

		// NOTE(fusion): Check empty value.
		int ValStart = EqualPos + 1;
		int ValEnd = LineSize;
		while(ValStart < ValEnd && isspace(Line[ValStart])){
			ValStart += 1;
		}

		while(ValEnd > ValStart && isspace(Line[ValEnd - 1])){
			ValEnd -= 1;
		}

		if(ValStart == ValEnd){
			LOG_WARN("%s:%d: Empty value", FileName, LineNumber);
			continue;
		}

		// NOTE(fusion): Parse KV pair.
		char Key[256];
		if(!StringBufCopyN(Key, &Line[KeyStart], (KeyEnd - KeyStart))){
			LOG_WARN("%s:%d: Exceeded key size limit of %d characters",
					FileName, LineNumber, (int)(sizeof(Key) - 1));
			continue;
		}

		char Val[256];
		if(!StringBufCopyN(Val, &Line[ValStart], (ValEnd - ValStart))){
			LOG_WARN("%s:%d: Exceeded value size limit of %d characters",
					FileName, LineNumber, (int)(sizeof(Val) - 1));
			continue;
		}

		if(StringEqCI(Key, "LoginPort")){
			ParseInteger(&Config->LoginPort, Val);
		}else if(StringEqCI(Key, "ConnectionTimeout")){
			ParseDuration(&Config->ConnectionTimeout, Val);
		}else if(StringEqCI(Key, "MaxConnections")){
			ParseInteger(&Config->MaxConnections, Val);
		}else if(StringEqCI(Key, "MaxStatusRecords")){
			ParseInteger(&Config->MaxStatusRecords, Val);
		}else if(StringEqCI(Key, "MinStatusInterval")){
			ParseDuration(&Config->MinStatusInterval, Val);
		}else if(StringEqCI(Key, "QueryManagerHost")){
			ParseStringBuf(Config->QueryManagerHost, Val);
		}else if(StringEqCI(Key, "QueryManagerPort")){
			ParseInteger(&Config->QueryManagerPort, Val);
		}else if(StringEqCI(Key, "QueryManagerPassword")){
			ParseStringBuf(Config->QueryManagerPassword, Val);
		}else if(StringEqCI(Key, "StatusWorld")){
			ParseStringBuf(Config->StatusWorld, Val);
		}else if(StringEqCI(Key, "URL")){
			ParseStringBuf(Config->Url, Val);
		}else if(StringEqCI(Key, "Location")){
			ParseStringBuf(Config->Location, Val);
		}else if(StringEqCI(Key, "ServerType")){
			ParseStringBuf(Config->ServerType, Val);
		}else if(StringEqCI(Key, "ServerVersion")){
			ParseStringBuf(Config->ServerVersion, Val);
		}else if(StringEqCI(Key, "ClientVersion")){
			ParseStringBuf(Config->ClientVersion, Val);
		}else if(StringEqCI(Key, "MOTD")){
			ParseMotd(Config->Motd, sizeof(Config->Motd), Val);
		}else{
			LOG_WARN("Unknown config \"%s\"", Key);
		}
	}

	fclose(File);
	return true;
}

static bool SigHandler(int SigNr, sighandler_t Handler){
	struct sigaction Action = {};
	Action.sa_handler = Handler;
	sigfillset(&Action.sa_mask);
	if(sigaction(SigNr, &Action, NULL) == -1){
		LOG_ERR("Failed to change handler for signal %d (%s): (%d) %s",
				SigNr, sigdescr_np(SigNr), errno, strerrordesc_np(errno));
		return false;
	}
	return true;
}

static void ShutdownHandler(int SigNr){
	g_ShutdownSignal = SigNr;
	//WakeConnections?
}

int main(int argc, const char **argv){
	(void)argc;
	(void)argv;

	g_StartTimeMS = GetClockMonotonicMS();
	g_ShutdownSignal = 0;
	if(!SigHandler(SIGPIPE, SIG_IGN)
	|| !SigHandler(SIGINT, ShutdownHandler)
	|| !SigHandler(SIGTERM, ShutdownHandler)){
		return EXIT_FAILURE;
	}

	// Service Config
	g_Config.LoginPort         = 7171;
	g_Config.ConnectionTimeout = 5;   // seconds
	g_Config.MaxConnections    = 10;
	g_Config.MaxStatusRecords  = 1024;
	g_Config.MinStatusInterval = 300; // seconds
	StringBufCopy(g_Config.QueryManagerHost, "127.0.0.1");
	g_Config.QueryManagerPort  = 7173;
	StringBufCopy(g_Config.QueryManagerPassword, "");

	// Service Info
	StringBufCopy(g_Config.StatusWorld,   "");
	StringBufCopy(g_Config.Url,           "");
	StringBufCopy(g_Config.Location,      "");
	StringBufCopy(g_Config.ServerType,    "");
	StringBufCopy(g_Config.ServerVersion, "");
	StringBufCopy(g_Config.ClientVersion, "");
	StringBufCopy(g_Config.Motd,          "");

	LOG("Tibia Login v0.2");
	if(!ReadConfig("config.cfg", &g_Config)){
		return EXIT_FAILURE;
	}

	LOG("Login port:          %d",     g_Config.LoginPort);
	LOG("Connection timeout:  %ds",    g_Config.ConnectionTimeout);
	LOG("Max connections:     %d",     g_Config.MaxConnections);
	LOG("Max status records:  %d",     g_Config.MaxStatusRecords);
	LOG("Min status interval: %ds",    g_Config.MinStatusInterval);
	LOG("Query manager host:  \"%s\"", g_Config.QueryManagerHost);
	LOG("Query manager port:  %d",     g_Config.QueryManagerPort);
	LOG("Status world:        \"%s\"", g_Config.StatusWorld);
	LOG("URL:                 \"%s\"", g_Config.Url);
	LOG("Location:            \"%s\"", g_Config.Location);
	LOG("Server type:         \"%s\"", g_Config.ServerType);
	LOG("Server version:      \"%s\"", g_Config.ServerVersion);
	LOG("Client version:      \"%s\"", g_Config.ClientVersion);
	LOG("MOTD:                \"%s\"", g_Config.Motd);


	{	// NOTE(fusion): Print MOTD preview with escape codes.
		char MotdPreview[30];
		if(StringBufEscape(MotdPreview, g_Config.Motd)){
			LOG("MOTD:                \"%s\"", MotdPreview);
		}else{
			LOG("MOTD:                \"%s...\"", MotdPreview);
		}
	}

	atexit(ExitQuery);
	atexit(ExitConnections);
	if(!InitQuery() || !InitConnections()){
		return EXIT_FAILURE;
	}

	LOG("Running...");
	while(g_ShutdownSignal == 0){
		ProcessConnections();
	}

	LOG("Received signal %d (%s), shutting down...",
			g_ShutdownSignal, sigdescr_np(g_ShutdownSignal));
	return EXIT_SUCCESS;
}

