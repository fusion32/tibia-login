#include "common.hh"

void LogAdd(const char *Prefix, const char *Format, ...){
	char Entry[4096];
	va_list ap;
	va_start(ap, Format);
	vsnprintf(Entry, sizeof(Entry), Format, ap);
	va_end(ap);

	if(Entry[0] != 0){
		struct tm LocalTime = GetLocalTime(time(NULL));
		fprintf(stdout, "%04d/%02d/%02d %02d:%02d:%02d [%s] %s\n",
				LocalTime.tm_year + 1900, LocalTime.tm_mon + 1, LocalTime.tm_mday,
				LocalTime.tm_hour, LocalTime.tm_min, LocalTime.tm_sec,
				Prefix, Entry);
	}
}

void LogAddVerbose(const char *Prefix, const char *Function,
		const char *File, int Line, const char *Format, ...){
	char Entry[4096];
	va_list ap;
	va_start(ap, Format);
	vsnprintf(Entry, sizeof(Entry), Format, ap);
	va_end(ap);

	if(Entry[0] != 0){
		(void)File;
		(void)Line;
		struct tm LocalTime = GetLocalTime(time(NULL));
		fprintf(stdout, "%04d/%02d/%02d %02d:%02d:%02d [%s] %s: %s\n",
				LocalTime.tm_year + 1900, LocalTime.tm_mon + 1, LocalTime.tm_mday,
				LocalTime.tm_hour, LocalTime.tm_min, LocalTime.tm_sec,
				Prefix, Function, Entry);
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

int64 GetClockMonotonicMS(void){
#if OS_WINDOWS
	LARGE_INTEGER Counter, Frequency;
	QueryPerformanceCounter(&Counter);
	QueryPerformanceFrequency(&Frequency);
	return (int64)((Counter.QuadPart * 1000) / Frequency.QuadPart);
#else
	struct timespec Time;
	clock_gettime(CLOCK_MONOTONIC, &Time);
	return ((int64)Time.tv_sec * 1000)
		+ ((int64)Time.tv_nsec / 1000000);
#endif
}

void SleepMS(int64 DurationMS){
#if OS_WINDOWS
	Sleep((DWORD)DurationMS);
#else
	struct timespec Duration;
	Duration.tv_sec = (time_t)(DurationMS / 1000);
	Duration.tv_nsec = (long)((DurationMS % 1000) * 1000000);
	nanosleep(&Duration, NULL);
#endif
}

bool StringEq(const char *A, const char *B){
	int Index = 0;
	while(true){
		if(A[Index] != B[Index]){
			return false;
		}else if(A[Index] == 0){
			return true;
		}
		Index += 1;
	}
}

bool StringEqCI(const char *A, const char *B){
	int Index = 0;
	while(true){
		if(tolower(A[Index]) != tolower(B[Index])){
			return false;
		}else if(A[Index] == 0){
			return true;
		}
		Index += 1;
	}
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

bool StringCopy(char *Dest, int DestCapacity, const char *Src){
	// IMPORTANT(fusion): `sqlite3_column_text` may return NULL if the column is
	// also NULL so we have an incentive to properly handle the case where `Src`
	// is NULL.
	int SrcLength = (Src != NULL ? (int)strlen(Src) : 0);
	return StringCopyN(Dest, DestCapacity, Src, SrcLength);
}

bool EscapeString(char *Dest, int DestCapacity, const char *Src){
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

uint32 HashString(const char *String){
	// FNV1a 32-bits
	uint32 Hash = 0x811C9DC5U;
	for(int i = 0; String[i] != 0; i += 1){
		Hash ^= (uint32)String[i];
		Hash *= 0x01000193U;
	}
	return Hash;
}

bool ParseIPAddress(int *Dest, const char *String){
	ASSERT(Dest != NULL && String != NULL);
	int Addr[4];
	if(sscanf(String, "%d.%d.%d.%d", &Addr[0], &Addr[1], &Addr[2], &Addr[3]) != 4){
		LOG_ERR("Invalid IP Address format \"%s\"", String);
		return false;
	}

	if(Addr[0] < 0 || Addr[0] > 0xFF
	|| Addr[1] < 0 || Addr[1] > 0xFF
	|| Addr[2] < 0 || Addr[2] > 0xFF
	|| Addr[3] < 0 || Addr[3] > 0xFF){
		LOG_ERR("Invalid IP Address \"%s\"", String);
		return false;
	}

	*Dest = (Addr[0] << 24)
			| (Addr[1] << 16)
			| (Addr[2] << 8)
			| (Addr[3] << 0);
	return true;
}

bool ParseBoolean(bool *Dest, const char *String){
	ASSERT(Dest != NULL && String != NULL);
	*Dest = StringEqCI(String, "true");
	return *Dest || StringEqCI(String, "false");
}

bool ParseInteger(int *Dest, const char *String){
	ASSERT(Dest != NULL && String != NULL);
	const char *StringEnd;
	*Dest = (int)strtol(String, (char**)&StringEnd, 0);
	return StringEnd > String;
}

bool ParseDuration(int *Dest, const char *String){
	ASSERT(Dest != NULL && String != NULL);
	const char *Suffix;
	*Dest = (int)strtol(String, (char**)&Suffix, 0);
	if(Suffix == String){
		return false;
	}

	while(Suffix[0] != 0 && isspace(Suffix[0])){
		Suffix += 1;
	}

	if(Suffix[0] == 'S' || Suffix[0] == 's'){
		*Dest *= (1000);
	}else if(Suffix[0] == 'M' || Suffix[0] == 'm'){
		*Dest *= (60 * 1000);
	}else if(Suffix[0] == 'H' || Suffix[0] == 'h'){
		*Dest *= (60 * 60 * 1000);
	}

	return true;
}

bool ParseSize(int *Dest, const char *String){
	ASSERT(Dest != NULL && String != NULL);
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
	ASSERT(Dest != NULL && DestCapacity > 0 && String != NULL);
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

bool ReadConfig(const char *FileName, ConfigKVCallback *KVCallback){
	ASSERT(FileName != NULL && KVCallback != NULL);
	FILE *File = fopen(FileName, "rb");
	if(File == NULL){
		LOG_ERR("Failed to open config file \"%s\"", FileName);
		return false;
	}

	bool EndOfFile = false;
	for(int LineNumber = 1; !EndOfFile; LineNumber += 1){
		char Line[1024];
		int MaxLineSize = (int)sizeof(Line);
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
		if(!StringCopyN(Key, (int)sizeof(Key), &Line[KeyStart], (KeyEnd - KeyStart))){
			LOG_WARN("%s:%d: Exceeded key size limit of %d characters",
					FileName, LineNumber, (int)(sizeof(Key) - 1));
			continue;
		}

		char Val[256];
		if(!StringCopyN(Val, (int)sizeof(Val), &Line[ValStart], (ValEnd - ValStart))){
			LOG_WARN("%s:%d: Exceeded value size limit of %d characters",
					FileName, LineNumber, (int)(sizeof(Val) - 1));
			continue;
		}

		KVCallback(Key, Val);
	}

	fclose(File);
	return true;
}
