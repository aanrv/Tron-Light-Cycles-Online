#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>		// struct sockaddr_inA
#include <arpa/inet.h>		// inet_ntop()
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <ncurses.h>
#include "player.h"
#include "h.h"			// PLAYER_1, PLAYER_2

typedef enum _errtype {NONE, STD, PERROR} errtype;
enum {APPNAME, PORTNUM, HOSTIP};			// args
enum {P1COLOR = 1, P2COLOR = 2};			// colors

const int refreshrate = 0;		// rate of redraw

/**
 * Connects client socket to server with specified parameters.
 * If straddr is NULL, INADDR_LOOPBACK will be used,
 * otherwise straddr will be converted (using inet_ntop).
 * sersock and seraddr will be set to the server socket and server address respectively.
 * playernum will be set to either PLAYER_1 or PLAYER_2.
 */
void connecttoserver(int clisock, unsigned short port, char* straddr, int* sersock, struct sockaddr_in* seraddr, unsigned char* playernum);

void assigncolors(void);

/* Initializes ncurses screen and starts the game's graphics. */
void playgame(int clisock, int sersock, const unsigned char playernum);

/* Set up window (disable cursor, disable line buffering, set nonblock, set keypad, etc.) */
errtype setscreen(WINDOW* win);

/* (Re)draw screen based on player locations and directions. */
void redrawscreen(struct Player* players);

/* Recieve signal from server. */
void recv_server(int clisock, struct Player* players);

/* Retrieve variables from server and update accordingly. */
void updateplayers(int clisock, struct Player* players);

/* Properly terminate game. */
void quitgame(int clisock);

/* Send variables to server. */
void send_server(int clisock, struct Player* players, const unsigned char playernum);

/* Send collision signal to server. */
void sendcol(int clisock);

/* Checks if next move is a collision. */
int nocollision(const struct Player* p, int maxy, int maxx);

/* Checks if player is within given bounds. */
int withinbounds(const struct Player* p, int maxy, int maxx);

int main(int argc, char** argv) {
	if (argc < 2) { fprintf(stderr, "Usage: %s portnum [host ip]\n", argv[0]); exit(EXIT_FAILURE); }

	// create socket
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) exitwerror("socket", EXIT_ERRNO);

	int sersock;			// server socket
	struct sockaddr_in seraddr;	// server address
	unsigned char playernum;	// will hold PLAYER_1 or PLAYER_2 after connecting

	connecttoserver(sock, atoi(argv[PORTNUM]), argc >= 3 ? argv[HOSTIP] : NULL, &sersock, &seraddr, &playernum);

	playgame(sock, sersock, playernum);

	//close(sock);
	return EXIT_SUCCESS;
}

void assigncolors(void) {
	init_pair(P1COLOR, COLOR_GREEN, COLOR_BLACK);
	init_pair(P2COLOR, COLOR_BLUE, COLOR_BLACK);
}

void playgame(int clisock, int sersock, const unsigned char playernum) {
	int i;
	const int countdown = 3;
	puts("Starting...");
	for (i = countdown; i > 0; --i) { printf("%d ", i); fflush(stdout); sleep(1); }

	WINDOW* win = initscr();
	if (win == NULL) exitwerror("Unable to initialize curses window.\n", EXIT_STD);

	start_color();
	assigncolors();		// assign colors to players

	int maxy;
	int maxx;
	getmaxyx(win, maxy, maxx);

	if (setscreen(win) == ERR) exitwerror("Unable to set screen.", EXIT_STD);

	struct Point loc1;
	loc1.x = maxx * 0.75f;
	loc1.y = maxy / 2;

	struct Point loc2;
	loc2.x = maxx * 0.25f;
	loc2.y = maxy / 2;

	struct Player players[NUMPLAYERS];
	memset(players, 0, sizeof (struct Player) * NUMPLAYERS);
	players[PLAYER_1] = createpl(loc1, LEFT, ACS_BLOCK);
	players[PLAYER_2] = createpl(loc2, RIGHT, ACS_BLOCK);

	for (;;) {
		recv_server(clisock, players);				// receive signal from server
		checkdirchange(&players[playernum]);			// FIRST, check for a direction change

		if (nocollision(&players[playernum], maxy, maxx)) {	// AFTER, make sure no collision will occur
			send_server(clisock, players, playernum);	// send standard message (variables, etc.) to server
		} else {
			sendcol(clisock);				// will collide
		}
		usleep(refreshrate);
	}
}

void redrawscreen(struct Player* players) {
	int i;
	for (i = 0; i < NUMPLAYERS; ++i) {
		int currcolor;
		switch (i + 1) {
			case P1COLOR:	currcolor = P1COLOR; break;
			case P2COLOR:	currcolor = P2COLOR; break;
			default:	exitwerror("redrawscreen: Invalid color.", EXIT_STD);
		}
		attron(COLOR_PAIR(currcolor));
		insertpl(&players[i]);
		attroff(COLOR_PAIR(currcolor));
	}

	refresh();
}

errtype setscreen(WINDOW* win) {
	if (curs_set(0) == ERR) return ERR;		// hide cursor
	if (nodelay(win, TRUE) == ERR) return ERR;	// set nonblocking
	if (cbreak() == ERR) return ERR;		// disable line buffering
	if (keypad(win, 1) == ERR) return ERR;		// enable user terminal
	return NONE;
}

void connecttoserver(int clisock, unsigned short port, char* straddr, int* sersock, struct sockaddr_in* seraddr, unsigned char* playernum) {
	// initialize server address
	memset(seraddr, 0, sizeof (*seraddr));
	seraddr->sin_family = AF_INET;
	seraddr->sin_port = htons(port);
	if (straddr == NULL)
		seraddr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	else
		if (inet_pton(AF_INET, straddr, &(seraddr->sin_addr.s_addr)) <= 0) exitwerror("Invalid ip.", EXIT_STD);

	// connect to server
	*sersock = connect(clisock, (struct sockaddr*) seraddr, sizeof (*seraddr));
	if (*sersock == -1) exitwerror("connect", EXIT_ERRNO);

	// receive notification
	if (recv(clisock, playernum, 1, 0) == -1) exitwerror("recv", EXIT_ERRNO);

	switch (*playernum) {
		case PLAYER_1: puts("Connected as Player 1."); break;
		case PLAYER_2: puts("Connected as Player 2."); break;
		default: exitwerror("Received invalid player number", EXIT_STD);
	}
}

void recv_server(int clisock, struct Player* players) {
	char sigtype;
	if (recv(clisock, &sigtype, 1, 0) == -1) exitwerror("recvsersig", EXIT_ERRNO);
	
	switch (sigtype) {
		case SC_STD: updateplayers(clisock, players); break;
		case SC_END: quitgame(clisock); break;
		default: exitwerror("recvsersig: invalid signal", EXIT_STD);
	}
}

void updateplayers(int clisock, struct Player* players) {
	char buffer[SC_STDSIZE];
	if (recv(clisock, buffer, SC_STDSIZE, 0) == -1) exitwerror("recv", EXIT_ERRNO);
	
	// modify direction
	players[PLAYER_1].dir = buffer[P1DIR];
	players[PLAYER_2].dir = buffer[P2DIR];
	
	// modify location
	movepl(&players[PLAYER_1]);
	movepl(&players[PLAYER_2]);
	
	// redraw
	redrawscreen(players);
}

void quitgame(int clisock) {
	endwin();
	
	// determine winning player
	char winner;
	if (recv(clisock, &winner, 1, 0) == -1) exitwerror("quitgame: recv", EXIT_ERRNO);
	
	printf("The winner is: Player %d!\n", winner + 1);
	
	close(clisock);
	exit(EXIT_SUCCESS);
}

void send_server(int clisock, struct Player* players, const unsigned char playernum) {
	char msgtype = CS_STD;
	char buffer[CS_STDSIZE];
	buffer[PDIR] = players[playernum].dir;
	
	if (send(clisock, &msgtype, 1, 0) == -1) exitwerror("send", EXIT_ERRNO);
	if (send(clisock, buffer, CS_STDSIZE, 0) == -1) exitwerror("send", EXIT_ERRNO);
}

void sendcol(int clisock) {
	char collisionsignal = CS_COL;
	if (send(clisock, &collisionsignal, 1, 0) == -1) exitwerror("sendcol: send", EXIT_ERRNO);
}

int nocollision(const struct Player* p, int maxy, int maxx) {
	return withinbounds(p, maxy, maxx) && (!willcollide(p));
}

int withinbounds(const struct Player* p, int maxy, int maxx) {
	int out = 1;
	
	int cury = p->loc.y;
	int curx = p->loc.x;
	
	int dy = p->dir == DOWN ? 1 : (p->dir == UP ? -1 : 0);
	int dx = p->dir == RIGHT ? 1 : (p->dir == LEFT ? -1 : 0);
	
	cury += dy;
	curx += dx;
	
	if (cury >= maxy || cury < 0) out = 0;
	else if (curx >= maxx || curx < 0) out = 0;

	return out;
}

void exitwerror(const char* msg, int exittype) {
	endwin();
	switch (exittype) {
		case EXIT_STD: 		fprintf(stderr, "%s\n", msg);
		case EXIT_ERRNO:	perror(msg);
	}
	exit(EXIT_FAILURE);
}

