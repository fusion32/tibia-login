#include "login.hh"

// TODO(fusion): Support windows eventually?
#if OS_LINUX
#	include <errno.h>
#	include <signal.h>
#else
#	error "Operating system not currently supported."
#endif

// Shutdown Signal
int  g_ShutdownSignal				= 0;

// Time
int  g_MonotonicTimeMS				= 0;

// Config
char g_Motd[256]					= "";
int  g_UpdateRate					= 20;
int  g_LoginPort					= 7171;
int  g_MaxConnections				= 10;
int  g_LoginTimeout					= 10000;
char g_QueryManagerHost[100]		= "127.0.0.1";
int  g_QueryManagerPort				= 7174;
char g_QueryManagerPassword[30]		= "";

static void ParseMotd(char *Dest, int DestCapacity, const char *String){
	char *Motd = (char*)alloca(DestCapacity);
	ParseString(Motd, DestCapacity, String);
	if(Motd[0] != 0){
		snprintf(Dest, DestCapacity, "%u\n%s", HashString(Motd), Motd);
	}
}

static void LoginKVCallback(const char *Key, const char *Val){
	if(StringEqCI(Key, "LoginPort")){
		ParseInteger(&g_LoginPort, Val);
	}else if(StringEqCI(Key, "MaxConnections")){
		ParseInteger(&g_MaxConnections, Val);
	}else if(StringEqCI(Key, "LoginTimeout")){
		ParseDuration(&g_LoginTimeout, Val);
	}else if(StringEqCI(Key, "UpdateRate")){
		ParseInteger(&g_UpdateRate, Val);
	}else if(StringEqCI(Key, "QueryManagerHost")){
		ParseString(g_QueryManagerHost,
				sizeof(g_QueryManagerHost), Val);
	}else if(StringEqCI(Key, "QueryManagerPort")){
		ParseInteger(&g_QueryManagerPort, Val);
	}else if(StringEqCI(Key, "QueryManagerPassword")){
		ParseString(g_QueryManagerPassword,
				sizeof(g_QueryManagerPassword), Val);
	}else if(StringEqCI(Key, "MOTD")){
		ParseMotd(g_Motd, sizeof(g_Motd), Val);
	}else{
		LOG_WARN("Unknown config \"%s\"", Key);
	}
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
}

int main(int argc, char **argv){
	(void)argc;
	(void)argv;

	g_ShutdownSignal = 0;
	if(!SigHandler(SIGPIPE, SIG_IGN)
	|| !SigHandler(SIGINT, ShutdownHandler)
	|| !SigHandler(SIGTERM, ShutdownHandler)){
		return EXIT_FAILURE;
	}

	int64 StartTime = GetClockMonotonicMS();
	g_MonotonicTimeMS = 0;

	LOG("Tibia Login Server v0.1");
	if(!ReadConfig("config.cfg", LoginKVCallback)){
		return EXIT_FAILURE;
	}

	atexit(ExitConnections);
	atexit(ExitQuery);

	if(!InitConnections()){
		return EXIT_FAILURE;
	}

	if(!InitQuery()){
		return EXIT_FAILURE;
	}

	// NOTE(fusion): Print MOTD with escape codes.
	char Motd[30];
	if(EscapeString(Motd, sizeof(Motd), g_Motd)){
		LOG("MOTD: \"%s\"", Motd);
	}else{
		LOG("MOTD: \"%s...\"", Motd);
	}

	LOG("Running at %d updates per second...", g_UpdateRate);
	int64 UpdateInterval = 1000 / (int64)g_UpdateRate;
	while(g_ShutdownSignal == 0){
		int64 UpdateStart = GetClockMonotonicMS();
		g_MonotonicTimeMS = (int)(UpdateStart - StartTime);
		ProcessConnections();
		int64 UpdateEnd = GetClockMonotonicMS();
		int64 NextUpdate = UpdateStart + UpdateInterval;
		if(NextUpdate > UpdateEnd){
			SleepMS(NextUpdate - UpdateEnd);
		}
	}

	LOG("Received signal %d (%s), shutting down...",
			g_ShutdownSignal, sigdescr_np(g_ShutdownSignal));

	return EXIT_SUCCESS;
}
