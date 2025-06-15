#include "utility.h"
volatile int shutdown_flag = 0;
#define MAX_THREADS 1024

struct door_queue {
    struct DOOR doors[MAX_DOORS];
    int door_cnt;
    pthread_mutex_t mutex;
};

struct door_info {
    int id;
    char *addr;
    int port;
    char *config;
};

struct door_list {
    struct door_info info[MAX_DOORS];
    int door_cnt;
    pthread_mutex_t mutex;
};

struct door_queue door_queue = {.door_cnt = 0, .mutex = PTHREAD_MUTEX_INITIALIZER};
struct door_list door_list = {.door_cnt = 0, .mutex = PTHREAD_MUTEX_INITIALIZER};
char firealarm_addr[BUFFER_SIZE] = {0};

struct thread_args {
    struct sockaddr_in overseer_sockaddr;
    int client_socket;
    const char *connections;
    const char *auth;
    int door_open_duration;
    int datagram_resend_delay;
};


char* get_door_id(char *card_id, const char *connect_file) {

    FILE *connections; 
    connections = fopen(connect_file, "r");
    if (connections == NULL) {
        perror("Failed to open file");
        exit(1); 
    }

    char *saveptr;
    char buffer[BUFFER_SIZE];
    while (fgets(buffer, sizeof(buffer), connections) != NULL) {

        char *component = strtok_r(buffer, " ", &saveptr);
        char *cmp_card_id = strtok_r(NULL, " ", &saveptr);
        char *door_id = strtok_r(NULL, " ", &saveptr);
        if (
            process_token(&component) == -1 ||
            process_token(&cmp_card_id) == -1 ||
            process_token(&door_id) == -1
        ) {
            fprintf(stderr, "NULL token\n");
            fclose(connections);
            exit(1);
        }

        if (strcmp(component, "DOOR") == 0) {

            if (strcmp(cmp_card_id, card_id) == 0) {
                fclose(connections);
                return door_id;
            }
        } else { break; }
    }

    fclose(connections);
    return "NULL";
}

bool check_access(char *code, char *door_id, const char *auth_file) {

    FILE *auth; 
    auth = fopen(auth_file, "r");
    if (auth == NULL) {
        perror("Failed to open file");
        exit(1); 
    }

    char buffer[BUFFER_SIZE];
    char *saveptr;
    char *tokenptr;
    while (fgets(buffer, sizeof(buffer), auth) != NULL) {

        char *cmp_code = strtok_r(buffer, " ", &saveptr);
        if (cmp_code != NULL) {

            if (strcmp(cmp_code, code) == 0) {

                char *token = strtok_r(NULL, ":", &saveptr);
                while (process_token(&token) != -1) {

                    char *component = strtok_r(token, ":", &tokenptr);
                    if (component != NULL) {
                        if (strcmp(component, "DOOR") == 0) {

                            char *cmp_id = strtok_r(NULL, ":", &tokenptr);
                            if (process_token(&cmp_id) == -1) {
                                fprintf(stderr, "NULL token\n");
                                fclose(auth);
                                exit(1);
                            }
                            
                            if (strcmp(cmp_id, door_id) == 0) {
                                fclose(auth);
                                return true;
                            }   

                        } else if (strcmp(component, "FLOOR") == 0) {
                            // Skip
                            continue;
                        }
                    }

                    token = strtok_r(NULL, " ", &saveptr);
                }
            }
        }
    }

    fclose(auth);
    return false;
}

int find_port_by_id(int id) {

    int port = -1;
    struct door_list local;
    pthread_mutex_lock(&door_list.mutex);
    memcpy(&local, &door_list, sizeof(struct door_list));
    pthread_mutex_unlock(&door_list.mutex);

    // Iterate through the array and find the matching id
    for (int i = 0; i < local.door_cnt; i++) {
        if (local.info[i].id == id) {
            port = local.info[i].port;
            break;
        }
    }

    return port;
}

void handle_scan(char *id, char *code, int client_sock, const char *connections, const char *auth, int door_open_duration) {

    char buffer[BUFFER_SIZE];
    char *door_id = get_door_id(id, connections);
    if (check_access(code, door_id, auth)) {

        // Reply with ALLOWED#
        send_message(client_sock, ALLOWED);
        close(client_sock);

        // Use door_id to open connection to door
        int door_port = find_port_by_id(atoi(door_id));
        if (door_port == -1) {
            fprintf(stderr, "Door not registered\n");
            exit(0);
        }

        char door_addr[BUFFER_SIZE];
        snprintf(door_addr, BUFFER_SIZE, "%s:%d", ADDRESS, door_port);
        struct sockaddr_in door_sockaddr = initialise_sockaddr(door_addr);

        int door_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (door_socket == -1) {
            perror("Failed to create socket file descriptor");
            exit(1);
        }

        if (connect(door_socket, (struct sockaddr *)&door_sockaddr, sizeof(door_sockaddr)) == -1) {
            perror("Failed to connect to sockaddr");
            close(door_socket);
            exit(1);
        }

        // Request open
        send_message(door_socket, OPEN);
        receive_message(door_socket, buffer, BUFFER_SIZE);

        if (strcmp(buffer, OPENING) == 0) {

            // Wait until door controller responds with OPENED#
            receive_message(door_socket, buffer, BUFFER_SIZE);
            if (strcmp(buffer, OPENED) == 0) {

                close(door_socket);
                usleep(door_open_duration);

                // Open connection again and send CLOSE#
                door_socket = socket(AF_INET, SOCK_STREAM, 0);
                if (door_socket == -1) {
                    perror("Failed to create socket file descriptor");
                    exit(1);
                }

                if (connect(door_socket, (struct sockaddr *)&door_sockaddr, sizeof(door_sockaddr)) == -1) {
                    perror("Failed to connect to sockaddr");
                    close(door_socket);
                    exit(1);
                }

                // Request close
                send_message(door_socket, CLOSE);
                close(door_socket);
                exit(0);

            } else {

                // Never received the opened message
                fprintf(stderr, "Unexpected message received\n");
                close(door_socket);
                exit(1);
            }   

        } else if (strcmp(buffer, ALREADY) == 0 || strcmp(buffer, SECURE) == 0) {
            close(door_socket);
            exit(0);
        
        } else {
            fprintf(stderr, "Unexpected respone from door\n");
            close(door_socket);
            exit(1);
        }   

    } else {
        // Reply with DENIED#
        send_message(client_sock, DENIED);
        close(client_sock);
        exit(0);
    }
}
                          
void register_door(struct DOOR datagram, int datagram_resend_delay) {

    // Send the DOOR datagram until a datagram is sent back with matching properties
    bool match;
    struct sockaddr_in firealarm_sockaddr = initialise_sockaddr(firealarm_addr);
    int UDP_send_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (UDP_send_sock < 0) {
        perror("Failed to create socket file descriptor");
        exit(1);
    }

    do {
        match = false;
        // Send to firealarm
        int bytes_sent = sendto(UDP_send_sock, &datagram, sizeof(datagram), 0, (struct sockaddr *)&firealarm_sockaddr, sizeof(firealarm_sockaddr));
        if (bytes_sent == -1) {
            perror("Failed to send packet");
            exit(1);
        }

        usleep(datagram_resend_delay);

        for (int i = 0; i < door_queue.door_cnt; i++) {   
            if (door_queue.doors[i].door_port == datagram.door_port) { match = true; }
        }

    } while (match);
}

void *handle_connection(void *arg) {

    struct thread_args *args = (struct thread_args *)arg;
    int TCP_sock = args->client_socket;
    const char *connections = args->connections;
    const char *auth = args->auth;
    int door_open_duration = args->door_open_duration;
    int datagram_resend_delay = args->datagram_resend_delay;

    // Aceept next connection
    struct sockaddr_in client_addr;
    socklen_t client_size = sizeof(client_addr);  
    int client_sock = accept(TCP_sock, (struct sockaddr*)&client_addr, &client_size);
    if (client_sock == -1) {
        perror("Failed to accept connection");
        exit(0);
        //continue;  // Continue listening for other connections
    }

    int opt = 1;
    if (setsockopt(client_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Failed to set socket for reuse");
        close(client_sock);
        exit(1);
    }

    // Receive data from the client
    char buffer[BUFFER_SIZE];
    receive_message(client_sock, buffer, BUFFER_SIZE);

    // Handle the received message here
    char *saveptr;
    char *id;
    char *addr;
    char *qual;
    char *component = strtok_r(buffer, " ", &saveptr);
    if (component != NULL) {

        if (strcmp(component, "CARDREADER") == 0) {

            id = strtok_r(NULL, " ", &saveptr);
            qual = strtok_r(NULL, " ", &saveptr);
            if (
                process_token(&id) == -1 ||
                process_token(&qual) == -1
            ) {
                fprintf(stderr, "NULL token\n");
                close(client_sock);
                exit(1);
            }

            if (strcmp(qual, "HELLO#") == 0) {
                close(client_sock);
                exit(0);

            } else if (strcmp(qual, "SCANNED#") == 0) {
                char *code = strtok_r(NULL, " ", &saveptr);
                process_token(&code);
                handle_scan(id, code, client_sock, connections, auth, door_open_duration);

            } else {
                fprintf(stderr, "Unexpected token\n");
                exit(1);
            }

        } else if (strcmp(component, "DOOR") == 0) {

            id = strtok_r(NULL, " ", &saveptr);
            addr = strtok_r(NULL, " ", &saveptr);
            qual = strtok_r(NULL, " ", &saveptr);
            if (
                process_token(&id) == -1 ||
                process_token(&addr) == -1 ||
                process_token(&qual) == -1
            ) {
                fprintf(stderr, "NULL token\n");
                close(client_sock);
                exit(1);
            }

            // Construct datagram
            struct DOOR door_entry;
            memcpy(door_entry.header, "DOOR", 4);

            char *addr_str = strtok_r(addr, ":", &saveptr);
            int port = atoi(strtok_r(NULL, ":", &saveptr));

            door_entry.door_port = (in_port_t)port;
            if (inet_pton(AF_INET, addr_str, &door_entry.door_addr) <= 0) {
                perror("Failed to convert IP address string to binary");
                exit(1);
            }

            // Save door info to door list
            pthread_mutex_lock(&door_list.mutex);
            door_list.info[door_list.door_cnt].id = atoi(id);
            door_list.info[door_list.door_cnt].addr = addr_str;
            door_list.info[door_list.door_cnt].port = port;
            door_list.info[door_list.door_cnt].config = qual;
            door_list.door_cnt++;
            pthread_mutex_unlock(&door_list.mutex);

            if (strcmp(qual, "FAIL_SAFE#") == 0) {

                if (firealarm_addr[0] == '\0') {
                    //Save globally to be sent once the firealarm is registered
                    pthread_mutex_lock(&door_queue.mutex);
                    door_queue.doors[door_queue.door_cnt] = door_entry;
                    door_queue.door_cnt++;
                    pthread_mutex_unlock(&door_queue.mutex);

                } else {
                    // Firealarm has been registed, send datagram
                    register_door(door_entry, datagram_resend_delay);
                }

            } else if (strcmp(qual, "FAIL_SECURE#") == 0) {
                close(client_sock);
                exit(0);

            } else {
                fprintf(stderr, "Unexpected token\n");
                exit(1);
            }

        } else if (strcmp(component, "FIREALARM") == 0) {

            addr = strtok_r(NULL, " ", &saveptr);
            qual = strtok_r(NULL, " ", &saveptr);
            if (
                process_token(&addr) == -1 ||
                process_token(&qual) == -1
            ) {
                fprintf(stderr, "NULL token\n");
                exit(1);
            }

            if (strcmp(qual, "HELLO#") == 0) {
                // Save the address to global
                snprintf(firealarm_addr, BUFFER_SIZE, "%s", addr);

                for (int i = 0; i < door_queue.door_cnt; i++) {
                    register_door(door_queue.doors[i], datagram_resend_delay);
                }

            } else {
                fprintf(stderr, "Unexpected token\n");
                exit(1);
            }
        }
    }
   
    // Close the client socket
    close(client_sock);
    free(args);
    return NULL;
}

void *TCP_listener(void *arg) {

    // Args
    struct thread_args *args = (struct thread_args *)arg;
    struct sockaddr_in overseer_sockaddr = args->overseer_sockaddr;

    // Set up the overseer's address
    int TCP_sock;
    //struct sockaddr_in client_addr;

    // Initialise TCP connection
    TCP_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (TCP_sock == -1) {
        perror("Failed to create socket file descriptor");
        exit(1);
    }

    // Enable address reuse
    int opt = 1;
    if (setsockopt(TCP_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Failed to set socket for reuse");
        close(TCP_sock);
        exit(1);
    }

    if (bind(TCP_sock, (struct sockaddr*)&overseer_sockaddr, sizeof(overseer_sockaddr)) == -1) {
        perror("Failed to bind socket");
        close(TCP_sock);
        exit(1);
    }

    // Listen for incoming connections
    if (listen(TCP_sock, 64) == -1) {
        perror("Failed to listen on socket");
        close(TCP_sock);
        exit(1);
    }

    pthread_t threads[MAX_THREADS];  
    int thread_cnt = 0;

    // Accept and handle incoming TCP connections
    while (!shutdown_flag) {

        // socklen_t client_size = sizeof(client_addr);  
        // client_sock = accept(TCP_sock, (struct sockaddr*)&client_addr, &client_size);
        // if (client_sock == -1) {
        //     perror("Failed to accept connection");
        //     continue;  // Continue listening for other connections
        // }

        // if (setsockopt(client_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        //     perror("Failed to set socket for reuse");
        //     close(client_sock);
        //     exit(1);
        // }

        if (thread_cnt < MAX_THREADS) {

            struct thread_args *new_args = malloc(sizeof(struct thread_args));
            *new_args = *args;
            new_args->client_socket = TCP_sock;
            pthread_create(&threads[thread_cnt], NULL, handle_connection, new_args);
            thread_cnt++;

        } else {
            fprintf(stderr, "Maximum thread count reached\n");
            close(TCP_sock);
            exit(0);
        }
    }

    for (int i = 0; i < thread_cnt; i++) {
        pthread_join(threads[i], NULL);
    }

    close(TCP_sock);
    return NULL;
}

void *UDP_listener(void *arg) {

    int UDP_sock;
    struct sockaddr_in client_addr;
    struct sockaddr_in* overseer_sockaddr = (struct sockaddr_in*)arg;
    
    UDP_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (UDP_sock < 0) {
        perror("Failed to create socket file descriptor");
        exit(1);
    }

    // Enable address reuse
    int opt = 1;
    if (setsockopt(UDP_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Failed to set socket for reuse");
        close(UDP_sock);
        exit(1);
    }

    // Bind the socket to the overseer's address
    if (bind(UDP_sock, (struct sockaddr*)overseer_sockaddr, sizeof(*overseer_sockaddr)) < 0) {
        perror("Failed to bind socket");
        close(UDP_sock);
        exit(1);
    }

    // Listen for initialization messages
    while (!shutdown_flag) {

        char buffer[BUFFER_SIZE];
        socklen_t client_size = sizeof(client_addr);
        ssize_t numBytes = recvfrom(UDP_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, &client_size);
        if (numBytes <= 0) {
            perror("Failed to receive packet");
            exit(1);

        } 
        
        if (strncmp(buffer, "DREG", 4) == 0) {

            struct DREG *datagram = (struct DREG *)buffer;
            pthread_mutex_lock(&door_queue.mutex);
            for (int i = 0; i < door_queue.door_cnt; i++) {
                if (door_queue.doors[i].door_port == datagram->door_port) { 
                    
                    for (int j = i + 1; j < door_queue.door_cnt; j++) {     
                        door_queue.doors[j - 1] = door_queue.doors[j];
                        door_queue.door_cnt--;
                    }

                    break; 
                }
            }
            pthread_mutex_unlock(&door_queue.mutex);
        }
    }
    close(UDP_sock);
    return NULL;
}

void display_door_list() {

    for (int i = 0; i < door_list.door_cnt; i++) {  
        fprintf(stderr, "%d %s:%d %s\n", door_list.info[i].id, door_list.info[i].addr, door_list.info[i].port, door_list.info[i].config);
    }
}

void open_close_door(int id, const char *command) {

    int door_port = find_port_by_id(id);
    if (door_port == -1) {
        fprintf(stderr, "Door not found\n");
        exit(0);
    }

    char door_addr[BUFFER_SIZE];
    snprintf(door_addr, BUFFER_SIZE, "%s:%d", ADDRESS, door_port);
    struct sockaddr_in door_sockaddr = initialise_sockaddr(door_addr);

    int door_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (door_socket == -1) {
        perror("Failed to create socket file descriptor");
        exit(1);
    }

    if (connect(door_socket, (struct sockaddr *)&door_sockaddr, sizeof(door_sockaddr)) == -1) {
        perror("Failed to connect");
        close(door_socket);
        exit(1);
    }

    // Request open/close
    send_message(door_socket, command);
}

void trigger_fire_alarm(int datagram_resend_delay) {

    struct sockaddr_in firealarm_sockaddr = initialise_sockaddr(firealarm_addr);
    struct FIRE fire_alert;
    memcpy(fire_alert.header, "FIRE", 4);

    // Open UDP connection
    int fire_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (fire_socket == -1) {
        perror("socket()");
        exit(1);
    }

    while(1) {
        // Send a fire emergency datagram
        int bytes_sent = sendto(fire_socket, &fire_alert, sizeof(fire_alert), 0, (struct sockaddr *)&firealarm_sockaddr, sizeof(firealarm_sockaddr));
        if (bytes_sent == -1) {
            perror("sendto()");
            exit(1);
        }
        usleep(datagram_resend_delay);
    }
}


int main(int argc, char **argv) {

    if (argc > 9) {
        fprintf(stderr, "Too many arguments\n");
        exit(0);
    }

    char *overseer_addr = argv[1];
    int door_open_duration = atoi(argv[2]);
    int datagram_resend_delay = atoi(argv[3]);
    const char *auth_file = argv[4];
    const char *connect_file = argv[5];

    struct sockaddr_in overseer_sockaddr = initialise_sockaddr(overseer_addr);
    
    pthread_t UDP_thread;
    pthread_create(&UDP_thread, NULL, UDP_listener, &overseer_sockaddr);

    struct thread_args args;
    args.overseer_sockaddr = overseer_sockaddr;
    args.connections = connect_file;
    args.auth = auth_file;
    args.door_open_duration = door_open_duration;
    args.datagram_resend_delay = datagram_resend_delay;

    pthread_t TCP_thread;
    pthread_create(&TCP_thread, NULL, TCP_listener, (void*)&args);


    char input[BUFFER_SIZE];
    while (1) {

        fprintf(stderr, "Enter command: ");
        fgets(input, sizeof(input), stdin);

        // Remove newline character if present
        char *newline = strchr(input, '\n');
        if (newline != NULL) {
            *newline = '\0';
        } else {
            fprintf(stderr, "Input cannot be NULL\n");
            continue;
        }

        char *saveptr;
        char *command = strtok_r(input, " ", &saveptr);

        if (strcmp(command, "DOOR") == 0) {

            char *sub_command = strtok_r(NULL, " ", &saveptr);
            if (strcmp(sub_command, "LIST") == 0) {
                display_door_list();

            } else if (strcmp(sub_command, "OPEN") == 0) {
                char *id_str = strtok_r(NULL, " ", &saveptr);
                if (id_str != NULL) {
                    int id = atoi(id_str);
                    open_close_door(id, OPEN);
                }

            } else if (strcmp(sub_command, "CLOSE") == 0) {
                char *id_str = strtok_r(NULL, " ", &saveptr);
                if (id_str != NULL) {
                    int id = atoi(id_str);
                    open_close_door(id, CLOSE);
                }

            } else {
                fprintf(stderr, "Unknown command\n");
            }

        } else if (strcmp(command, "FIRE") == 0) {

            char *sub_command = strtok_r(NULL, " ", &saveptr);
            if (strcmp(sub_command, "ALARM") == 0) {
                trigger_fire_alarm(datagram_resend_delay);

            } else {
                fprintf(stderr, "Unknown command\n");
            }

        } else if (strcmp(command, "EXIT") == 0) {
            shutdown_flag = 1;
            pthread_join(UDP_thread, NULL);
            pthread_join(TCP_thread, NULL);
            fprintf(stderr, "Exiting overseer...\n");
            break;

        } else {
            fprintf(stderr, "Unknown command\n");
        }
    }

    return 0;
}