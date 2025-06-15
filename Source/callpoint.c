#include "utility.h"


int main(int argc, char **argv) {  // SAFETY CRITICAL

    if (argc > 5) {
        (void) fprintf(stderr, "Too many arguments\n");
        exit(0);
    }

    int32_t delay = atoi(argv[1]);
    const char *shm_path = argv[2];
    int32_t shm_offset = atoi(argv[3]);
    char *firealarm_addr = argv[4];

    if (delay < 0) {
        (void) fprintf(stderr, "Delay cannot be negative\n");
        exit(0);
    }

    // Initialise shared memory
    char *shm = initialise_shm(shm_path);
    callpoint_shm *shared = (callpoint_shm *)(shm + shm_offset);

    // Initialise server address and datagram
    struct sockaddr_in firealarm_sockaddr = initialise_sockaddr(firealarm_addr);
    struct FIRE fire_alert;
    memcpy(fire_alert.header, "FIRE", 4);

    // Open UDP connection
    int32_t callpoint_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (callpoint_socket == -1) {
        perror("Failed to create socket file descriptor");
        exit(1);
    }

    // Normal logic
    pthread_mutex_lock(&shared->mutex);
    
    while(1) {

        if (shared->status == '*') {
            while(1) {

                // Send a fire emergency datagram
                int32_t bytes_sent = sendto(callpoint_socket, &fire_alert, sizeof(fire_alert), 0, (struct sockaddr *)&firealarm_sockaddr, sizeof(firealarm_sockaddr));
                if (bytes_sent == -1) {
                    perror("Failed to send packet");
                    exit(1);
                }

                usleep(delay);
            }
        } else {
            pthread_cond_wait(&shared->cond, &shared->mutex);
        }
    }

    return 0;
}