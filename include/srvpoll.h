#ifndef SRVPOLL_H
#define SRVPOLL_H

#include <poll.h>

#define MAX_CLIENTS 256
#define PORT 8080
#define BUFFER_SIZE 4096
#define BACKLOG 10

typedef enum {
    STATE_NEW,
    STATE_CONNECTED,
    STATE_DISCONNECTED
} state_e;

typedef struct {
    int fd;
    state_e state;
    char buffer[BUFFER_SIZE];
} clientstate_t;

void init_clients(clientstate_t* states);

int find_free_slot(clientstate_t* states);

int find_slot_by_fd(clientstate_t* states, int fd);

#endif
