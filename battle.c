/*
 * socket demonstrations:
 * This is the server side of an "internet domain" socket connection, for
 * communicating over the network.
 *
 * In this case we are willing to wait for chatter from the client
 * _or_ for a new connection.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef PORT
    #define PORT 30100
#endif

#define TIMEOUT_SECONDS 10
#define MAX_NAME_LEN 50
#define MAX_MSG_LEN 200
#define MAX_BUFFER_LEN 200

#define HP_MIN 20
#define HP_MAX 30

#define REGULAR_DMG_MIN 2
#define REGULAR_DMG_MAX 6

#define POWERMOVE_COUNT_MIN 1
#define POWERMOVE_COUNT_MAX 3
#define POWERMOVE_DMG_MULTIPLIER 3 //powermove is just regular dmg (generated randomly) multiplied by the multiplier
#define POWERMOVE_CHANCE 2 //2 means chance of powermove is 1/2, or 50%. If it was 3, it would be 1/3, or 33%, and so on...

#define HP_REGEN_MIN 3 //min amount of regeneration
#define HP_REGEN_MAX 10 //max amount of regeneration
#define HP_REGEN_COUNT_MIN 1
#define HP_REGEN_COUNT_MAX 3

struct client {
    int fd;
    struct in_addr ipaddr;
    struct client *next;

    int name_registered; //0 for false, 1 for true
    char name[MAX_NAME_LEN];

    struct player_info *player_info;

    int in_match; //0 for false, 1 for true
    struct match *current_match;

    struct client *client_just_played;

    struct bufferinfo *bufferinfo;
};

struct player_info {
    int hp; //healthpoints
    int powermoves_remaining;
    int hp_regens_remaining;
};

struct bufferinfo {
    int buffering_input;
    int buffer_index;
    char buffer[MAX_BUFFER_LEN];
};

//malloc matches to free them later on
struct match {
    struct client* players[2];
    struct client* starting_player;
    struct client* active_player;
    struct client* non_active_player;
    int round; //when any player makes a move, a new round begins (if the match hasn't ended, ofc)
    int speech_state; //1 when active player is saying something and 0 otherwise
    int powermove_count;
    int hp_regen_count;
    struct client* winner;
    struct client* loser;
};

struct chat_message {
    struct client *sender;
    struct client *receiver;
    char *message;
    struct chat_message *next;
};


int bindandlisten();

struct client *addclient(int fd, struct in_addr addr);
void removeclient(struct client *c);
void welcomeclient(struct client *c);
int handleclient(struct client *p);
int registername(struct client *c, char *s);
void moveclienttoendoflist(struct client *c);

void resetbuffer(struct client *c);

struct match* creatematch(struct client *c1, struct client *c2);
void endmatch(struct match *match);
int checkifmatchended(struct match *match);
void switchturn(struct match *match);

struct client* findopponent(struct client *c); //may return NULL if no opponent is available
void matchloneclients();

void attack(struct client *c);
int usepowermove(struct client *c); //returns 0 if powermove missed, 1 if it landed
void usehealthregen(struct client *c);
void speak(struct client *c, char *s); //buffer bytes (bcs using noncanonical mode)

void displayinfo(struct client *c);
void displaymenu(struct client *c);
void updatedisplay(struct match *match, int mode);

void broadcast_all(struct client *sender, char *s, int size);
void broadcast_to_client(struct client *c, char *s);
unsigned int time();

//static variables
static int *client_count;
static struct client *top = NULL;


int main() 
{
    int clientfd, maxfd, nready;
    socklen_t len;
    struct sockaddr_in q;
    struct timeval tv;
    fd_set allset;
    fd_set rset;

    int i;

    client_count = malloc(sizeof(int));
    *client_count = 0;

    srand(time(0)); //seed RNG

    int listenfd = bindandlisten();
    printf("Port number: %d\n", PORT);
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;
        
        //jamie
        if (*client_count == 0)
        {
            //if server is empty, wait a couple seconds before shutting down (unless a client joins)
            
            //timeout variable
            tv.tv_sec = TIMEOUT_SECONDS; //seconds
            tv.tv_usec = 0; //microseconds

            nready = select(maxfd + 1, &rset, NULL, NULL, &tv);
            
            if (nready == 0)
            {
                //timeout
                printf("Server has been empty for %d seconds. Shutting down...\n", TIMEOUT_SECONDS);
                break;
            }
        }
        else {
            nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        }
        
        if (nready == -1) {
            perror("select");
            continue;
        }
        
        //shahar
        if (FD_ISSET(listenfd, &rset)){
            len = sizeof(q);
            
            if ((clientfd = accept(listenfd, (struct sockaddr *)&q, &len)) < 0) {
                perror("accept");
                exit(1);
            }
            
            FD_SET(clientfd, &allset);
            
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            
            printf("Connection from %s\n", inet_ntoa(q.sin_addr));

            struct client *new_client = addclient(clientfd, q.sin_addr);
            welcomeclient(new_client);
        }
        //shahr end

        for(i = 0; i <= maxfd; i++) {
            if (FD_ISSET(i, &rset)) {
                struct client *p;
                for (p = top; p; p = p->next) {
                    if (p->fd == i) {
                        if (handleclient(p) == -1) {
                            int tmp_fd = p->fd;

                            removeclient(p);

                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                        }
                        break;
                    }
                }
            }
        }
    }

    free(client_count);
    return 0;
}

int handleclient(struct client *p) {
    char buf[1];
    int len = read(p->fd, buf, sizeof(char));

    if (!p->name_registered)
    {
        //read the client's name's characters, one by one

        if (buf[0] == '\n')
        {
            p->bufferinfo->buffer[p->bufferinfo->buffer_index] = '\0';
            registername(p, p->bufferinfo->buffer);
            resetbuffer(p);
            matchloneclients();
        }
        else {
            p->bufferinfo->buffer[p->bufferinfo->buffer_index] = buf[0];
            p->bufferinfo->buffer_index++;
        }
        
        return 0;
    }
    
    if (len > 0)
    {
        printf("Received %d bytes from %s: %s\n", len, p->name, buf);

        //game logic

        //shahr start
        if (p->in_match && p->current_match->active_player == p)
        {
            if (p->current_match->speech_state)
            {
                //BUFFER BYTES HERE
                if (buf[0] == '\n')
                {
                    p->bufferinfo->buffer[p->bufferinfo->buffer_index] = '\0';

                    //speak
                    speak(p, p->bufferinfo->buffer);
                    
                    //untoggle speech state
                    p->current_match->speech_state = 0;
                    //untoggle buffering state
                    p->bufferinfo->buffering_input = 0;

                    resetbuffer(p);
                }
                else {
                    p->bufferinfo->buffer[p->bufferinfo->buffer_index] = buf[0];
                    p->bufferinfo->buffer_index++;
                }
            } 
            else if (strcmp(buf, "a") == 0 || strcmp(buf, "p") == 0 || strcmp(buf, "r") == 0)
            {
                if (strcmp(buf, "a") == 0) {
                    //regular attack
                    attack(p);
                }
                else if (strcmp(buf, "p") == 0) {
                    if (p->player_info->powermoves_remaining > 0)
                    {
                        //powermove
                        usepowermove(p);
                    }
                    else {
                        return 0;
                    }
                }
                else if (strcmp(buf, "r") == 0) {
                    if (p->player_info->hp_regens_remaining > 0)
                    {
                        //regenerate hp
                        usehealthregen(p);
                        updatedisplay(p->current_match, 1);
                    }

                    return 0;
                }
                
                if (checkifmatchended(p->current_match) == 1)
                {
                    //sending winner and loser messages
                    broadcast_to_client(p->current_match->winner, "You killed your opponent. You win!\n");
                    broadcast_to_client(p->current_match->loser, "You have died. You lose!\n");
                    
                    broadcast_to_client(p->current_match->winner, "\nAwaiting next opponent...\n");
                    broadcast_to_client(p->current_match->loser, "\nAwaiting next opponent...\n");

                    //IMPORTANT: endmatch call must come AFTER broadcast messages to avoid a seg fault
                    endmatch(p->current_match);
                    return 0;
                }
                
                switchturn(p->current_match);
            
            }
            else if (strcmp(buf, "s") == 0 && p->current_match->speech_state == 0)
            {
                p->current_match->speech_state = 1;
                p->bufferinfo->buffering_input = 1;
                broadcast_to_client(p, "\nSpeak: ");
            }
   
        //shahr end
        }
        else {
            broadcast_to_client(p, "\nWait your turn...\n");
        }
        
        return 0;
    }
    else {
        // socket is closed, disconnect client
        return -1;
    }
}

void resetbuffer(struct client *c) {
    c->bufferinfo->buffer_index = 0;
    memset(c->bufferinfo->buffer, 0, MAX_BUFFER_LEN);
}

 /* bind and listen, abort on error
  * returns FD of listening socket
  */
int bindandlisten() {
    struct sockaddr_in r;
    int listenfd;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    int yes = 1;
    if ((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
    }
    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(PORT);

    int i = 0;
    while (bind(listenfd, (struct sockaddr *)&r, sizeof r) == -1)
    {
        printf("Port %d is already in use...trying port %d\n", PORT + i, PORT + i + 1);
        i++;
        r.sin_port = htons(PORT + i);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
    return listenfd;
}

struct client *addclient(int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
        perror("malloc");
        exit(1);
    }

    p->fd = fd;
    p->ipaddr = addr;
    p->next = top;
    p->name_registered = 0;
    p->in_match = 0;
    p->client_just_played = NULL;

    p->bufferinfo = malloc(sizeof(struct bufferinfo));

    top = p;

    (*client_count)++;
    return p;
}

void removeclient(struct client *c) {
    if (c == top)
    {
        top = c->next;
    }
    else {
        struct client *p;

        for (p = top; p && p->next != c; p = p->next);
        // Now, p points to the client just before c

        //removing c from the linked list of clients, but not yet deleting it
        p->next = c->next;
    }

    if (c) {
        printf("Disconnect from %s (%s)\n", inet_ntoa(c->ipaddr), c->name);

        if (c->client_just_played)
        {
            c->client_just_played->client_just_played = NULL;
        }

        char outbuf[MAX_MSG_LEN];
        sprintf(outbuf, "**%s leaves**\r\n", c->name);
        broadcast_all(c, outbuf, strlen(outbuf));

        if (c->in_match)
        {
            c->current_match->winner = (c->current_match->active_player == c) ? c->current_match->non_active_player : c->current_match->active_player;
            c->current_match->loser = c;

            //sending winner and loser messages
            broadcast_to_client(c->current_match->winner, "--Opponent dropped. You win!\n");
            broadcast_to_client(c->current_match->winner, "\nAwaiting next opponent...\n");
            
            endmatch(c->current_match);
        }

        free(c);
    } else {
        printf("ERROR\n");
        exit(1);
    }

    (*client_count)--;

    matchloneclients();
}

void broadcast_all(struct client *sender, char *s, int size) {
    struct client *p;

    //note: do not broadcast to the sender, and only broadcast to clients who have entered their name
    for (p = top; p; p = p->next) {
        if (p != sender && strlen(p->name) > 0)
        {
            broadcast_to_client(p, s);
        }
    }
}

void broadcast_to_client(struct client *c, char *s) {
    if (write(c->fd, s, strlen(s)) == -1) {
        perror("write");
        exit(1);
    }
}


/*
Register name of client c
returns -1 if there was an error reading, 0 if username is already taken, and 1 if successful
*/
int registername(struct client *c, char *s) {
    strcpy(c->name, s);

    if (strlen(s) == 0)
    {
        char *s = "Sorry, you cannot type an empty name. Please try again:\n";
        broadcast_to_client(c, s); //alert the client
        return 0;
    }

    //check if username already exists, and if so, alert the client:
    struct client *p;
    for (p = top; p; p = p->next) {
        if (p != c && strcmp(p->name, c->name) == 0)
        {
            //username taken!
            memset(c->name, 0, MAX_NAME_LEN);

            printf("Received %d bytes. Desired name of client %s is: %s, but name is already taken\n", (int) strlen(s), inet_ntoa(c->ipaddr), c->name);

            char *s = "Sorry, that name is already taken. Please type another name:\n";
            broadcast_to_client(c, s); //alert the client
            return 0;
        }
    }    

    printf("Received %d bytes. Name of client %s is: %s\n", (int) strlen(s), inet_ntoa(c->ipaddr), c->name);
    
    c->name_registered = 1;

    char *s1 = "\nAwaiting opponent...\n";
    broadcast_to_client(c, s1);
    
    //alert entire arena of new player
    char s2[MAX_MSG_LEN];
    sprintf(s2, "\n**%s enters the arena**\n", c->name);
    broadcast_all(c, s2, strlen(s2));
    
    return 1;
}

void welcomeclient(struct client *c) {
    char *s = "Welcome to the Battle Server! Please enter your name to begin:\n";
    broadcast_to_client(c, s);
}


/*
Returns NULL if no available opponent found
*/
struct client* findopponent(struct client *c) {
    for (struct client *p = top; p; p = p->next)
    {
        //p is not NULL, p has registered his name, p is not in a match, and p has not just played against c in his previous match
        if (p != c && p->name_registered && !p->in_match && p->client_just_played != c)
        {
            return p;
        }        
    }

    return NULL;    
}

/*

*/
void matchloneclients() {
    for (struct client *p = top; p; p = p->next) 
    {
        if (p->name_registered && !p->in_match)
        {
            //if client not in a match, find an opponent to match him up with (if available)
            struct client *opp = findopponent(p);

            if (opp != NULL)
            {
                //create match
                creatematch(p, opp);
            }
        }
        
    }
    
}


/*
Returns the newly created match
*/
struct match* creatematch(struct client *c1, struct client *c2) {
    c1->in_match = 1;
    c2->in_match = 1;

    //mallocing the match
    struct match *match = malloc(sizeof(struct match));

    //assigning the players to the match
    match->players[0] = c1;
    match->players[1] = c2;

    //randomly assigning the starting player
    match->starting_player = match->players[rand() % 2];
    match->active_player = match->starting_player;
    match->non_active_player = match->starting_player == c1 ? c2 : c1;

    //match info
    match->round = 0;
    match->speech_state = 0; //to indicate that no player is speaking rn
    match->powermove_count = POWERMOVE_COUNT_MIN + (rand() % (POWERMOVE_COUNT_MAX - POWERMOVE_COUNT_MIN + 1)); //random number of powermoves
    match->hp_regen_count = HP_REGEN_COUNT_MIN + (rand() % (HP_REGEN_COUNT_MAX - HP_REGEN_COUNT_MIN + 1)); //random number of hp regens

    //setting current match
    c1->current_match = match;
    c2->current_match = match;


    //setting the players' powermoves (equal for both players)
    c1->player_info = malloc(sizeof(struct player_info));
    c1->player_info->powermoves_remaining = match->powermove_count;
    c1->player_info->hp_regens_remaining = match->hp_regen_count;
    
    c2->player_info = malloc(sizeof(struct player_info));
    c2->player_info->powermoves_remaining = match->powermove_count;
    c2->player_info->hp_regens_remaining = match->hp_regen_count;
    
    //setting the health points randomly (not necessarily equal for both players)
    c1->player_info->hp = HP_MIN + (rand() % (HP_MAX - HP_MIN + 1));
    c2->player_info->hp = HP_MIN + (rand() % (HP_MAX - HP_MIN + 1));


    //alert clients that they have engaged each other
    char s1[MAX_MSG_LEN];
    sprintf(s1, "You engage %s!\n", c2->name);
    broadcast_to_client(c1, s1);
    
    char s2[MAX_MSG_LEN];
    sprintf(s2, "You engage %s!\n", c1->name);
    broadcast_to_client(c2, s2);
    
    //switchturn to initiate game properly
    switchturn(match);

    return match;
}

/*
Returns the new head of the client list (may be unchanged)
*/
void endmatch(struct match *match) {
    match->players[0]->in_match = 0;
    match->players[0]->current_match = NULL;
    match->players[0]->client_just_played = match->players[1];

    match->players[1]->in_match = 0;
    match->players[1]->current_match = NULL;
    match->players[1]->client_just_played = match->players[0];

    //send clients to end of list (first come, first serve). which client gets moved first will be random
    int first = rand() % 2; //0 or 1
    int second = (first == 0) ? 1 : 0;

    moveclienttoendoflist(match->players[first]);
    moveclienttoendoflist(match->players[second]);

    // top = match->players[second];

    free(match);

    matchloneclients();
}


void attack(struct client *c) {
    //should obv only be called when client c is currently in a match
    int dmg = REGULAR_DMG_MIN + (rand() % (REGULAR_DMG_MAX - REGULAR_DMG_MIN + 1));
    c->current_match->non_active_player->player_info->hp -= dmg;

    char s1[MAX_MSG_LEN];
    sprintf(s1, "\nYou hit %s for %d damage!\n", c->current_match->non_active_player->name, dmg);
    broadcast_to_client(c, s1);

    char s2[MAX_MSG_LEN];
    sprintf(s2, "\n%s hits you for %d damage!\n", c->name, dmg);
    broadcast_to_client(c->current_match->non_active_player, s2);
}

int usepowermove(struct client *c) {
    //should obv only be called when client c is currently in a match
    if (c->player_info->powermoves_remaining <= 0)
    {
        //ERROR
        //will never be the case since powermoves are checked before method is called
        exit(1);
    }
    
    int hit = (rand() % POWERMOVE_CHANCE == 0) ? 1 : 0;
    
    char s1[MAX_MSG_LEN];
    char s2[MAX_MSG_LEN];

    if (hit)
    {
        int dmg = (REGULAR_DMG_MIN + (rand() % (REGULAR_DMG_MAX - REGULAR_DMG_MIN + 1))) * POWERMOVE_DMG_MULTIPLIER;
        c->current_match->non_active_player->player_info->hp -= dmg;

        sprintf(s1, "\nYou powermove %s for %d damage!\n", c->current_match->non_active_player->name, dmg);
        sprintf(s2, "\n%s powermoves you for %d damage!\n", c->name, dmg);
    }
    else {
        sprintf(s1, "\nYou missed your powermove!\n");
        sprintf(s2, "\n%s missed his powermove against you!\n", c->name);
    }

    broadcast_to_client(c, s1);
    broadcast_to_client(c->current_match->non_active_player, s2);

    
    c->player_info->powermoves_remaining--;
    
    return hit; //1 if hit, 0 if missed
}

void usehealthregen(struct client *c) {
    //should obv only be called when client c is currently in a match
    if (c->player_info->hp_regens_remaining <= 0)
    {
        //ERROR
        //will never be the case since regen count is checked before method is called
        exit(1);
    }
    
    int regen_amt = HP_REGEN_MIN + (rand() % (HP_REGEN_MAX - HP_REGEN_MIN + 1));
    c->player_info->hp += regen_amt;

    char s1[MAX_MSG_LEN];
    char s2[MAX_MSG_LEN];

    sprintf(s1, "\nYou regenerated %d HP!", regen_amt);
    sprintf(s2, "\n%s regenerated %d HP!", c->name, regen_amt);

    broadcast_to_client(c, s1);
    broadcast_to_client(c->current_match->non_active_player, s2);

    c->player_info->hp_regens_remaining--;
}

void speak(struct client *c, char *s) {
    char msg[MAX_MSG_LEN];
    sprintf(msg, "[%s]: %s\n", c->name, s);
    broadcast_to_client(c->current_match->non_active_player, msg);
}


/*
Returns the new head of the list (may be unchanged), or NULL if client c was not found
*/
void moveclienttoendoflist(struct client *c) {
    //the case where client c is the first client (top) simply means it is already at the end of the list, and so we don't do anything


    if (!c || !top)
    {
        perror("moveclienttoendoflist");
        exit(1);
    }
    

    if (c != top)
    {
        struct client *beforec;
        for (beforec = top; beforec && beforec->next != c; beforec = beforec->next);
        //beforec is now the client in the list just before client c
        //OR beforec is NULL (in which case client c was not found in the list, and we raise error)

        if (!beforec)
        {
            return;
        }

        beforec->next = c->next;
        c->next = top;
        top = c;
    }

    //printf testing
    // printf("\n");
    // for (struct client* p = top; p; p = p->next)
    // {
    //     printf("%s\n", p->name);
    // }
}


void displayinfo(struct client *c) {
    //should obv only be called when client c is currently in a match
    struct client *opp = (c == c->current_match->active_player) ? c->current_match->non_active_player : c->current_match->active_player;

    char s[MAX_MSG_LEN];
    sprintf(s, "\nYour hitpoints: %d\nYour powermoves: %d\nYour HP regens: %d\n%s's hitpoints: %d\n\n", c->player_info->hp, c->player_info->powermoves_remaining, c->player_info->hp_regens_remaining, opp->name, opp->player_info->hp);

    //only if client is not active player
    if (c->current_match->active_player != c)
    {
        strcat(s, "Waiting for other player to strike...\n");
    }
    
    broadcast_to_client(c, s);
}

void displaymenu(struct client *c) {
    //should obv only be called when client c is currently in a match
    char s[200];
    strcpy(s, "\n(a)ttack");
    
    if (c->player_info->powermoves_remaining > 0)
    {
        strcat(s, "\n(p)owermove");
    }

    if (c->player_info->hp_regens_remaining > 0) {
        strcat(s, "\n(r)egenerate healthpoints");
    }

    strcat(s, "\n(s)peak something\n");
    
    broadcast_to_client(c, s);
}

/*
if mode 0, update for both players,
if mode 1, update for active player,
if mode 2, update for inactive player
*/
void updatedisplay(struct match *match, int mode) {
    if (mode == 0 || mode == 1)
    {
        displayinfo(match->active_player);
        displaymenu(match->active_player);
    }
    
    if (mode == 0 || mode == 2)
    {
        displayinfo(match->non_active_player);
    }
}

int checkifmatchended(struct match *match) {
    if (match->active_player->player_info->hp <= 0) {
        match->winner = match->non_active_player;
        match->loser = match->active_player;
        return 1;
    }
    else if (match->non_active_player->player_info->hp <= 0) {
        match->winner = match->active_player;
        match->loser = match->non_active_player;
        return 1;
    }
    
    return 0;
}

void switchturn(struct match *match) {
    struct client *tmp = match->active_player;
    match->active_player = match->non_active_player;
    match->non_active_player = tmp;
    match->round++;

    char s[200];
    sprintf(s, "---------------\nROUND %d\n---------------\n", match->round);

    broadcast_to_client(match->active_player, s);
    broadcast_to_client(match->non_active_player, s);

    //display
    updatedisplay(match, 0);
}