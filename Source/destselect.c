#include "utility.h"


int main(int argc, char **argv) { 

    if (argc > 6) {
        (void) fprintf(stderr, "Too many arguments\n");
        exit(0);
    }

    int id = atoi(argv[1]);
    int wait_time = atoi(argv[2]);
    const char *shm_path = argv[3];
    int shm_offset = atoi(argv[4]);
    char *overseer_addr = argv[5];

    if (wait_time < 0) {
        fprintf(stderr, "Wait time cannot be negative\n");
        exit(0);
    }

    char buffer[BUFFER_SIZE];

    // Initialise shared memory
    char *shm = initialise_shm(shm_path);
    destselct_shm *shared = (destselct_shm *)(shm + shm_offset);

    // Initialise server address
    struct sockaddr_in overseer_sockaddr = initialise_sockaddr(overseer_addr);

    // Initialise client socket
    int destselect_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (destselect_socket == -1) {
        perror("Failed to create socket");
        exit(1);
    }

    if (connect(destselect_socket, (struct sockaddr *)&overseer_sockaddr, sizeof(overseer_sockaddr)) == -1) {
        perror("Failed to connect");
        close(destselect_socket);
        exit(1);
    }

    // Initialisation message
    snprintf(buffer, BUFFER_SIZE, "DESTSELECT %d HELLO#", id);
    send_message(destselect_socket, buffer);
    close(destselect_socket);
  

    // Normal logic 
    pthread_mutex_lock(&shared->mutex);

    while (1) {
        
        bool valid_code = true;
        for (int i = 0; i < 16; i++) {
            if (shared->scanned[i] == '\0') { valid_code = false; break; }
        }

        if (valid_code) {

            char scanned[17];
            memcpy(scanned, shared->scanned, 16);
            scanned[16] = '\0'; 

            // Open TCP
            destselect_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (destselect_socket == -1) {
                perror("Failed to create socket file descriptor");
                exit(1);
            }

            if (connect(destselect_socket, (struct sockaddr *)&overseer_sockaddr, sizeof(overseer_sockaddr)) == -1) {
                perror("Failed to connect");
                close(destselect_socket);
                exit(1);
            }

            // Scanned message
            snprintf(buffer, BUFFER_SIZE, "CARDREADER %d SCANNED %s %d#", id, scanned, shared->floor_select);
            int bytes_sent = send(destselect_socket, buffer, strlen(buffer), 0);
            if (bytes_sent == -1) {
                perror("Failed to send message");
                close(destselect_socket);
                pthread_mutex_unlock(&shared->mutex); 
                exit(1);
            }

            // Handle response from overseer
            usleep(wait_time); 

            int bytes_received = recv(destselect_socket, buffer, BUFFER_SIZE, MSG_DONTWAIT);
            if (bytes_received == -1) {
                  
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No data is currently available
                    shared->response = 'N';
                    continue;

                } else {
                    perror("Failed to receive message");
                    close(destselect_socket);
                    pthread_mutex_unlock(&shared->mutex);
                    exit(1);
                }

            } else if (bytes_received == 0) {
                // No response from overseer
                shared->response = 'N';

            } else {
                // Null-terminate the received data to treat it as a string
                buffer[bytes_received] = '\0';
                
                // Process the received data
                if (strcmp(buffer, ALLOWED) == 0) {
                    shared->response = 'Y';

                } else if (strcmp(buffer, DENIED) == 0) {
                    shared->response = 'N';

                } else {
                    // Unexpected response
                    fprintf(stderr, "Unexpected response from overseer\n");
                    exit(1);
                }
            }
            
            pthread_cond_signal(&shared->response_cond);
            close(destselect_socket);
        }

        pthread_cond_wait(&shared->scanned_cond, &shared->mutex);
    }

    return 0;

}