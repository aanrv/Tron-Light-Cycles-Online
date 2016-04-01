#define NUMPLAYERS 2		// number of players
enum {PLAYER_1, PLAYER_2};	// player information indices

enum SC_SIGTYPE {		// server-to-client signal types
	SC_STD,			// standard message containing vars
	SC_END			// signal to quit game
};

enum CS_SIGTYPE {		// client-to-server signal types
	CS_STD,			// standard message containing vars
	CS_COL			// send message to server indicating collision
};

#define SC_STDSIZE 2		// server-to-client packet size
enum SC_STD {			// server-to-client standard signal
	P1DIR,			// direction of player 1
	P2DIR			// direction of player 2
};

#define CS_STDSIZE 1		// client-to-server packet size
enum CS_STD {			// client-to-server standard signal
	PDIR			// direction of player
};

#define SC_ENDSIZE 1
enum SC_END {			// server-to-client end signal
	PWIN			// end signal also indicates winning player
};

