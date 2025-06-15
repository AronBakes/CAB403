#include "utility.h"

void send_emergency(struct in_addr addr, uint16_t port) {

    struct sockaddr_in receiver_sockaddr;
    memset(&receiver_sockaddr, 0, sizeof(struct sockaddr_in));
    receiver_sockaddr.sin_family = AF_INET;
    receiver_sockaddr.sin_port = ntohs(port); 
    receiver_sockaddr.sin_addr = addr; 

    int32_t emerg_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (emerg_socket == -1) {
        perror("Failed to create socket file descriptor");
    }

    if (connect(emerg_socket, (struct sockaddr *)&receiver_sockaddr, sizeof(receiver_sockaddr)) == -1) {
        perror("Failed to connect");
        close(emerg_socket);
    }

    send_message(emerg_socket, OPEN_EMERG);
    close(emerg_socket);
}

void FIRE_protocal(firealarm_shm *shared, struct DOOR *door_list, int32_t *door_num, int32_t UDP_recv_sock, struct sockaddr_in client_sockaddr, int32_t UDP_send_sock, struct sockaddr_in overseer_sockaddr) {

    pthread_mutex_lock(&shared->mutex);
    shared->alarm = 'A';
    pthread_mutex_unlock(&shared->mutex);
    pthread_cond_signal(&shared->cond);

    for (int32_t i = 0; i < *door_num; i++) {
        send_emergency(door_list[i].door_addr, ntohs(door_list[i].door_port));
    }

    char buffer[BUFFER_SIZE];
    socklen_t client_size;
    ssize_t bytes_received;
    int32_t bytes_sent;
    struct DOOR *datagram;
    struct DREG DREG_datagram;
    memcpy(DREG_datagram.header, "DREG", 4);

    while(1) {
        
        // Receive the next UDP datagram
        client_size = sizeof(client_sockaddr);
        bytes_received = recvfrom(UDP_recv_sock, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_sockaddr, &client_size);
        if (bytes_received == -1) {
            perror("Failed to receive packet");
            continue;
            
        }

        if (strncmp(buffer, "DOOR", 4) == 0) {

            datagram = (struct DOOR *)buffer;

            // Send OPEN_EMERG to new door
            send_emergency(datagram->door_addr, ntohs(datagram->door_port));

            // Send a DREG to the overseer
            DREG_datagram.door_addr = datagram->door_addr;
            DREG_datagram.door_port = datagram->door_port;

            bytes_sent = sendto(UDP_send_sock, &DREG_datagram, sizeof(datagram), 0, (struct sockaddr *)&overseer_sockaddr, sizeof(overseer_sockaddr));
            if (bytes_sent == -1) {
                perror("Failed to send packet");
                continue;
            }
        }
    }
}


int main(int argc, char **argv) {  // SAFETY CRITICAL

    if (argc > 9) {
        (void) fprintf(stderr, "Too many arguments\n");
        exit(0);
    }

    char *firealarm_addr = argv[1];
    const char *shm_path = argv[6];
    int32_t shm_offset = atoi(argv[7]);
    char *overseer_addr = argv[8];

    char buffer[BUFFER_SIZE];
    struct DOOR door_list[MAX_FAIL_SAFE];
    int32_t door_num = 0;

    // Initialise shared memory
    char *shm = initialise_shm(shm_path);
    firealarm_shm *shared = (firealarm_shm *)(shm + shm_offset);

    // Initialise server addresses
    struct sockaddr_in firealarm_sockaddr = initialise_sockaddr(firealarm_addr); 
    struct sockaddr_in overseer_sockaddr = initialise_sockaddr(overseer_addr); 
    struct sockaddr_in client_sockaddr;
    socklen_t client_size = sizeof(client_sockaddr);

    int32_t TCP_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (TCP_sock < 0) {
        perror("Failed to create socket file desciptor");
        exit(1);
    }

    if (connect(TCP_sock, (struct sockaddr *)&overseer_sockaddr, sizeof(overseer_sockaddr)) == -1) {
        perror("Failed to connect");
        close(TCP_sock);
        exit(1);
    }

    // Initialisation message
    snprintf(buffer, BUFFER_SIZE, "FIREALARM %s HELLO#", firealarm_addr);
    send_message(TCP_sock, buffer);
    close(TCP_sock);


    // Normal logic
    int32_t UDP_recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (UDP_recv_sock == -1) {
        perror("Failed to create socket file descriptor");
        exit(1);
    }

    if (bind(UDP_recv_sock, (struct sockaddr *)&firealarm_sockaddr, sizeof(firealarm_sockaddr)) == -1) {
        perror("Failed to bind socket");
        exit(1);
    }

    int32_t UDP_send_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (UDP_send_sock == -1) {
        perror("Failed to create socket file descriptor");
        exit(1);
    }

    struct DOOR *datagram;
    struct DREG DREG_datagram;
    memcpy(DREG_datagram.header, "DREG", 4);

    while(1) {

        // Receive UDP
        ssize_t bytes_received = recvfrom(UDP_recv_sock, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_sockaddr, &client_size);
        if (bytes_received == -1) {
            perror("Failed to receive packet");
            exit(1);
        }

        if (strncmp(buffer, "FIRE", 4) == 0) {

            FIRE_protocal(shared, door_list, &door_num, UDP_recv_sock, client_sockaddr, UDP_send_sock, overseer_sockaddr);

        } else if (strncmp(buffer, "DOOR", 4) == 0) {

            datagram = (struct DOOR *)buffer;
            if (door_num == 0) {

                // Copy the received DOOR datagram into the door_list array
                memcpy(&door_list[door_num], datagram, sizeof(struct DOOR));
                door_num++;
            
            } else if (door_num < MAX_FAIL_SAFE) {

                // Check if door is already in the list
                bool match = false;
                for (int32_t i = 0; i < door_num || match == true; i++) {
                    if (door_list[door_num].door_port == datagram->door_port) { match = true; }
                }

                if (!match) {
                    // Copy the received DOOR datagram into the door_list array
                    memcpy(&door_list[door_num], datagram, sizeof(struct DOOR));
                    door_num++;
                }

            } else {
                // Handle array full condition
                (void) fprintf(stderr, "Array is full, cannot store more DOOR datagrams\n");
            }

            DREG_datagram.door_addr = datagram->door_addr;
            DREG_datagram.door_port = datagram->door_port;

            // Send to overseer
            ssize_t bytes_sent = sendto(UDP_send_sock, &DREG_datagram, sizeof(DREG_datagram), 0, (struct sockaddr *)&overseer_sockaddr, sizeof(overseer_sockaddr));
            if (bytes_sent == -1) {
                perror("Failed to send packet");
                exit(1);
            }

        } else if (strncmp(buffer, "TEMP", 4) == 0) {
            // Skip
            continue;

        } else {
            (void) fprintf(stderr, "Unexpected header\n");
            exit(1);
        }
    }

    return 0;
}