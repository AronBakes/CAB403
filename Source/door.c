#include "utility.h"

void update_status(door_shm *shared, char status) {

    pthread_mutex_lock(&shared->mutex);
    shared->status = status;
    pthread_cond_signal(&shared->cond_start);
    pthread_cond_wait(&shared->cond_end, &shared->mutex);
    pthread_mutex_unlock(&shared->mutex);
}


int main(int argc, char **argv) {  // SAFETY CRITICAL

    if (argc > 7) {
        (void) fprintf(stderr, "Too many arguments\n");
        exit(0);
    }

    int32_t id = atoi(argv[1]);
    char *door_addr = argv[2];
    const char *config = argv[3];
    const char *shm_path = argv[4];
    int32_t shm_offset = atoi(argv[5]);
    char *overseer_addr = argv[6];

    char buffer[BUFFER_SIZE];
    
    // Initialise shared memory
    char *shm = initialise_shm(shm_path);
    door_shm *shared = (door_shm *)(shm + shm_offset);

    // Initialise server addresses
    struct sockaddr_in overseer_sockaddr = initialise_sockaddr(overseer_addr);
    struct sockaddr_in door_sockaddr = initialise_sockaddr(door_addr);

    // Initialise client socket
    int32_t init_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (init_socket == -1) {
        perror("Failed to create socket");
        exit(1);
    }

    if (connect(init_socket, (struct sockaddr *)&overseer_sockaddr, sizeof(overseer_sockaddr)) == -1) {
        perror("Failed to connect");
        close(init_socket);
        exit(1);
    }

    // Initialisation message
    snprintf(buffer, BUFFER_SIZE, "DOOR %d %s %s#", id, door_addr, config);
    send_message(init_socket, buffer);
    close(init_socket);


    int32_t door_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (door_socket == -1) {
        perror("Failed to create socket");
        exit(1);
    }

    // Bind the socket to this doors address and port
    if (bind(door_socket, (struct sockaddr *)&door_sockaddr, sizeof(door_sockaddr)) == -1) {
        perror("Failed to bind socket");   
        exit(1);
    }

    // Listen for incoming connections
    if (listen(door_socket, 64) == -1) {
        perror("Failed to listen on socket");
        exit(1);
    }

    // Normal logic 
    bool emergency = false;
    bool secure = false;

    while (1) {
        
        pthread_mutex_lock(&shared->mutex);
        char status = shared->status;
        pthread_mutex_unlock(&shared->mutex);

        while (1) {

            // Accept incoming connections
            struct sockaddr_in client_addr;
            socklen_t client_size = sizeof(client_addr);
            int32_t sender_socket = accept(door_socket, (struct sockaddr *)&client_addr, &client_size);
            if (sender_socket == -1) {
                perror("Failed to accept connection");
                exit(1);
            }

            // Receive data from the client
            receive_message(sender_socket, buffer, BUFFER_SIZE);
            if (strcmp(config, "FAIL_SAFE") == 0) {

                if (strcmp(buffer, CLOSE) == 0) {

                    if (!emergency) {

                        if (status == 'C') {
                 
                            send_message(sender_socket, ALREADY);
                            continue;

                        } else if (status == 'O') {

                            send_message(sender_socket, CLOSING);
                            update_status(shared, 'c');
                            send_message(sender_socket, CLOSED);
                            close(sender_socket);
                            break;
                        }  

                    } else {
                        
                        send_message(sender_socket, EMERGENCY);
                        close(sender_socket);
                        continue;
                    }

                } else if (strcmp(buffer, OPEN) == 0) {

                    if (status == 'O') {
                 
                        send_message(sender_socket, ALREADY);
                        continue;

                    } else if (status == 'C') {

                        send_message(sender_socket, OPENING);
                        update_status(shared, 'o');
                        send_message(sender_socket, OPENED);
                        close(sender_socket);
                        break;
                    }

                } else if (strcmp(buffer, OPEN_EMERG) == 0) {

                    update_status(shared, 'o');
                    emergency = true;
                    break;
                }

            } else if (strcmp(config, "FAIL_SECURE") == 0) {

                if (strcmp(buffer, CLOSE) == 0) {

                    if (status == 'C') {
                
                        send_message(sender_socket, ALREADY);
                        continue;

                    } else if (status == 'O') {

                        send_message(sender_socket, CLOSING);
                        update_status(shared, 'c');
                        send_message(sender_socket, CLOSED);
                        close(sender_socket);
                        break;
                    }  

                } else if (strcmp(buffer, OPEN) == 0) {

                    if (!secure) {

                        if (status == 'O') {
                    
                            send_message(sender_socket, ALREADY);
                            continue;

                        } else if (status == 'C') {

                            send_message(sender_socket, OPENING);
                            update_status(shared, 'o');
                            send_message(sender_socket, OPENED);
                            close(sender_socket);
                            break;
                        }

                    } else {

                        send_message(sender_socket, EMERGENCY);
                        close(sender_socket);
                        continue;
                    }

                } else if (strcmp(buffer, CLOSE_SECURE) == 0) {

                    update_status(shared, 'c');
                    secure = true;
                    break;
                }
            }
        }
    }

    return 0;
} 