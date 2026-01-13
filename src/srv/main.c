#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <getopt.h>

#include "common.h"
#include "file.h"
#include "parse.h"
#include "srvpoll.h"


clientstate_t clientStates[MAX_CLIENTS] = {0};

void print_usage(char *argv[]) {
    printf("Usage: %s -n -f <database file>\n", argv[0]);
    printf("\t -n - create new database file\n");
    printf("\t -f - (required) path to database file\n");
    printf("\t -p - (required) listening port\n");
    printf("\t -a - add via CSV list of <name,address,hours>\n");
    printf("\t -l - list employee database\n");
    printf("\t -u - update employee hours via CSV list of <name,hours>\n");
    printf("\t -r - remove employee from database via CSV list of <name>\n");
    return;
}

void poll_loop(unsigned short port, struct dbheader_t *dbhdr, struct employee_t *employees) {
    int listen_fd = -1;
    int connection_fd = -1;
    int freeServerSlot = -1;
    int nfds = 1;
    int opt = 1;

    struct sockaddr_in server_address, client_address;
    socklen_t client_length = sizeof(client_address);

    struct pollfd fd_list[MAX_CLIENTS + 1];

    // initialize client states
    init_clients(&clientStates[0]);

    // create listening socket
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // setup server_address settings
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    memset(fd_list, 0, sizeof(fd_list));
    fd_list[0].fd = listen_fd;
    fd_list[0].events = POLLIN;

    while (1) {
        int j = 1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clientStates[i].fd != -1) {
                fd_list[j].fd = clientStates[i].fd; // Offset by 1 for listen_fd
                fd_list[j].events = POLLIN;
                j++;
            }
        }

        // wait for event on one socket
        int n_events = poll(fd_list, nfds, -1); // NO TIMEOUT at -1
        if (n_events == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        // check for new connections
        if (fd_list[0].revents & POLLIN) {
            if ((connection_fd = accept(listen_fd, (struct sockaddr *)&client_address, &client_length)) == -1) {
                perror("accept");
                continue;
            }

            printf("New connection from %s:%d\n",
                   inet_ntoa(client_address.sin_addr),
                   ntohs(client_address.sin_port));

            // find_free_slot for new connection
            freeServerSlot = find_free_slot(&clientStates[0]);
            if (freeServerSlot == -1) {
                printf("Server full: closing new connection.\n");
                close(connection_fd);
            } else {
                clientStates[freeServerSlot].fd = connection_fd;
                clientStates[freeServerSlot].state = STATE_CONNECTED;
                nfds++;
                printf("Slot %d has fd %d\n", freeServerSlot, clientStates[freeServerSlot].fd);
            }

            n_events--;
        }

        // check each client for read/write activity
        for (int i = 0; i <= nfds && n_events > 0; i++) { // start from 1 to skip listen_fd
            if (fd_list[i].revents & POLLIN) {
                n_events--;

                int fd = fd_list[i].fd;
                int slot = find_slot_by_fd(&clientStates[0], fd);
                ssize_t bytes_read = read(fd, &clientStates[slot].buffer, sizeof(clientStates[slot].buffer));

                // connection closed or error
                if (bytes_read <= 0) {
                    close(fd);
                    if (slot == -1) {
                        printf("Tried closing nonexistent fd?\n");
                    } else {
                        clientStates[slot].fd = -1; // free up the slot
                        clientStates[slot].state = STATE_DISCONNECTED;
                        printf("Client disconnected or error.\n");
                        memset(&clientStates[slot].buffer, '\0', BUFFER_SIZE);
                        nfds--;
                    }
                } else {
                    printf("Received data from client: %s\n", clientStates[slot].buffer);
                    memset(&clientStates[slot].buffer, '\0', BUFFER_SIZE);
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    char *filepath = NULL;
    char *portarg = NULL;
    char *addstring = NULL;
    char *updatehours = NULL;
    char *removestring = NULL;
    bool newfile = false;
    bool list = false;
    unsigned short port = 0;
    int c;
    int dbfd = -1;
    struct dbheader_t *dbhdr = NULL;
    struct employee_t *employees = NULL;

    while ((c = getopt(argc, argv, "nf:p:a:lu:r:")) != -1) {
        switch (c) {
            case 'n':
                newfile = true;
                break;
            case 'f':
                filepath = optarg;
                break;
            case 'p':
                portarg = optarg;
                port = atoi(portarg);
                if (port == 0) {
                    printf("Bad port: %s\n", portarg);
                }
                break;
            case 'a':
                addstring = optarg;
                break;
            case 'l':
                list = true;
                break;
            case 'u':
                updatehours = optarg;
                break;
            case 'r':
                removestring = optarg;
                break;
            case '?':
                printf("Unknown option -%c\n", c);
                break;
            default:
                return -1;
        }
    }

    if (filepath == NULL) {
        printf("Filepath is a required argument!\n");
        print_usage(argv);
        return 0;
    }

    if (port == 0) {
        printf("Port not set.\n");
        print_usage(argv);
        return 0;
    }

    if (newfile) {
        dbfd = create_db_file(filepath);
        if (dbfd == STATUS_ERROR) {
            printf("Unable to create database file!\n");
            return -1;
        }

        if (create_db_header(&dbhdr) != STATUS_SUCCESS) {
            printf("Unable to create database header!\n");
            return -1;
        }

    } else {
        dbfd = open_db_file(filepath);
        if (dbfd == STATUS_ERROR) {
            printf("Unable to open database file!\n");
            return -1;
        }

        if (validate_db_header(dbfd, &dbhdr) != STATUS_SUCCESS) {
            printf("Unable to validate database header!\n");
            return -1;
        }
    }

    if (read_employees(dbfd, dbhdr, &employees) != STATUS_SUCCESS) {
        printf("Unable to read employee data!\n");
        return -1;
    }

    if (addstring) {
        if (add_employee(dbhdr, &employees, addstring) != STATUS_SUCCESS) {
            printf("Failed to add employee to database!\n");
            return -1;
        }
    }

    if (removestring) {
        if (remove_employee(dbhdr, &employees, removestring) != STATUS_SUCCESS) {
            printf("Failed to remove employee from database!\n");
            return -1;
        }
    }

    if (updatehours) {
        if (update_hours(dbhdr, employees, updatehours) != STATUS_SUCCESS) {
            printf("Failed to update employee hours!\n");
            return -1;
        }
    }

    if (list) {
        if (list_employees(dbhdr, employees) != STATUS_SUCCESS) {
            printf("Unable to list employee database!\n");
            return -1;
        }
    }

    poll_loop(port, dbhdr, employees);

    if (output_file(dbfd, dbhdr, employees) != STATUS_SUCCESS) {
        printf("Failed writing data to file!\n");
        return -1;
    }

    return 0;
}
