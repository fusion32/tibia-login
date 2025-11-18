#include "common.hh"

// IMPORTANT(fusion): We want connections to use the status string directly for
// output, but because it could be refreshed while connections are still using
// it, we need to have them buffered, to allow old versions to remain valid, at
// least for a couple refreshes. Note that `

static int g_LastStatusRefresh = 0;
static int g_StatusStringIndex = 0;
static char g_StatusString[3][KB(2)];

struct XMLBuffer{
	char *Data;
	int Size;
	int Position;
};

static void XMLNullTerminate(XMLBuffer *Buffer){
	ASSERT(Buffer->Size > 0);
	if(Buffer->Position < Buffer->Size){
		Buffer->Data[Buffer->Position] = 0;
	}else{
		Buffer->Data[Buffer->Size - 1] = 0;
	}
}

static void XMLAppendChar(XMLBuffer *Buffer, char Ch){
	if(Buffer->Position < Buffer->Size){
		Buffer->Data[Buffer->Position] = Ch;
	}
	Buffer->Position += 1;
}

static void XMLAppendNumber(XMLBuffer *Buffer, int64 Num){
	if(Num == 0){
		XMLAppendChar(Buffer, '0');
		return;
	}

	if(Num < 0){
		XMLAppendChar(Buffer, '-');
		Num = -Num;
	}

	char String[64] = {};
	int StringLen = 0;
	while(Num > 0){
		ASSERT(StringLen < (int)sizeof(String));
		String[StringLen] = (Num % 10) + '0';
		Num = (Num / 10);
		StringLen += 1;
	}

	for(int i = 0; i < StringLen; i += 1){
		XMLAppendChar(Buffer, String[(StringLen - 1) - i]);
	}
}

static void XMLAppendString(XMLBuffer *Buffer, const char *String){
	const char *P = String;
	while(P[0]){
		XMLAppendChar(Buffer, P[0]);
		P += 1;
	}
}

static void XMLAppendStringEscaped(XMLBuffer *Buffer, const char *String){
	const char *P = String;
	while(P[0]){
		switch(P[0]){
			case '\t': XMLAppendString(Buffer, "&#9;"); break;
			case '\n': XMLAppendString(Buffer, "&#10;"); break;
			case '"':  XMLAppendString(Buffer, "&quot;"); break;
			case '&':  XMLAppendString(Buffer, "&amp;"); break;
			case '\'': XMLAppendString(Buffer, "&apos;"); break;
			case '<':  XMLAppendString(Buffer, "&lt;"); break;
			case '>':  XMLAppendString(Buffer, "&gt;"); break;
			default:   XMLAppendChar(Buffer, P[0]); break;
		}
		P += 1;
	}
}

static void XMLAppendStringFV(XMLBuffer *Buffer, const char *Format, va_list Args){
	const char *P = Format;
	while(P[0]){
		// IMPORTANT(fusion): This function only implements a small subset of
		// the printf syntax.
		if(P[0] == '%'){
			P += 1;

			// IMPORTANT(fusion): Small integral parameters such as char and short
			// are promoted to int, similar to how they're promoted in arithmetic
			// operations. This means there are only really 4 and 8 bytes integers.
			int Length = 4;
			if(P[0] == 'h' && P[1] == 'h'){
				P += 2;
			}else if(P[0] == 'l' && P[1] == 'l'){
				Length = 8;
				P += 2;
			}else if(P[0] == 'h'){
				P += 1;
			}else if(P[0] == 'l'){
				Length = 8;
				P += 1;
			}

			switch(P[0]){
				case '%':{
					XMLAppendChar(Buffer, '%');
					break;
				}

				case 'c':{
					int Ch = va_arg(Args, int);
					XMLAppendChar(Buffer, (char)Ch);
					break;
				}

				case 'd':{
					int64 Num = (Length == 4 ? va_arg(Args, int) : va_arg(Args, int64));
					XMLAppendNumber(Buffer, Num);
					break;
				}

				case 's':{
					const char *String = va_arg(Args, const char*);
					XMLAppendStringEscaped(Buffer, String);
					break;
				}

				default:{
					LOG_ERR("Invalid XML format specifier \"%c\"", P[0]);
					break;
				}
			}

			if(P[0]){
				P += 1;
			}

		}else{
			XMLAppendChar(Buffer, P[0]);
			P += 1;
		}
	}
}

static void XMLAppendStringF(XMLBuffer *Buffer, const char *Format, ...){
	va_list Args;
	va_start(Args, Format);
	XMLAppendStringFV(Buffer, Format, Args);
	va_end(Args);
}

const char *GetStatusString(void){
	int TimeNow = (int)time(NULL);
	if((TimeNow - g_LastStatusRefresh) >= g_Config.MinStatusInterval){
		const char *WorldName = "";
		int Uptime = 0;
		int NumPlayers = 0;
		int MaxPlayers = 0;
		int OnlinePeak = 0;

		TWorld World = {};
		if(GetWorld(g_Config.StatusWorld, &World) == 0){
			WorldName = World.Name;
			if(World.LastStartup != 0 && World.LastStartup > World.LastShutdown){
				Uptime = (int)time(NULL) - World.LastStartup;
			}
			NumPlayers = World.NumPlayers;
			MaxPlayers = World.MaxPlayers;
			OnlinePeak = World.OnlinePeak;

			// IMPORTANT(fusion): This could be a common behaviour but, on OTSERVLIST,
			// the server will show as OFFLINE if the the online peak is less than
			// the number of online players. This shouldn't usually be a problem since
			// the online character list and online peak are updated together in the
			// same CREATE_PLAYERLIST query, but is something to keep in mind.
			if(OnlinePeak < NumPlayers){
				OnlinePeak = NumPlayers;
			}
		}else{
			LOG_ERR("Failed to query world data...");
		}

		// NOTE(fusion): Skip line with MOTD hash.
		const char *Motd = g_Config.Motd;
		while(Motd[0]){
			if(Motd[0] == '\n'){
				Motd += 1;
				break;
			}
			Motd += 1;
		}

		g_StatusStringIndex = (g_StatusStringIndex + 1) % NARRAY(g_StatusString);
		XMLBuffer Buffer = {};
		Buffer.Data = g_StatusString[g_StatusStringIndex];
		Buffer.Size = sizeof(g_StatusString[g_StatusStringIndex]);
		XMLAppendString(&Buffer, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
		XMLAppendString(&Buffer, "<tsqp version=\"1.0\">");
		XMLAppendStringF(&Buffer,
				"<serverinfo servername=\"%s\" uptime=\"%d\" url=\"%s\""
					" location=\"%s\" server=\"%s\" version=\"%s\""
					" client=\"%s\"/>",
				WorldName, Uptime, g_Config.Url, g_Config.Location,
				g_Config.ServerType, g_Config.ServerVersion,
				g_Config.ClientVersion);
		XMLAppendStringF(&Buffer,
				"<players online=\"%d\" max=\"%d\" peak=\"%d\"/>",
				NumPlayers, MaxPlayers, OnlinePeak);
		XMLAppendStringF(&Buffer, "<motd>%s</motd>", Motd);
		XMLAppendString(&Buffer, "</tsqp>");
		XMLNullTerminate(&Buffer);

		g_LastStatusRefresh = TimeNow;
	}

	return g_StatusString[g_StatusStringIndex];
}

