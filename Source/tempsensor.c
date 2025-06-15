#include "utility.h"

int main(int argc, char **argv) {

    int id = atoi(argv[1]);
    char *addr = argv[2];
    int max_condvar_wait = atoi(argv[3]);
    int max_update_wait = atoi(argv[4]);
    const char *shm_path = argv[5];
    int shm_offset = atoi(argv[6]);
    char *receiver_list = argv[7];

    char *ip = strtok(addr, ":");
    int port = atoi(strtok(NULL, ":"));


    // Initialize shared memory
    int shm_fd;
    char *shm;

    shm_fd = shm_open(shm_path, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_open()");
        exit(1);
    }

    struct stat shm_stat;
    if (fstat(shm_fd, &shm_stat) == -1) {
        perror("fstat()");
        exit(1);
    }

    shm = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap()");
        close(shm_fd);
        exit(1);
    }

    tempsensor_shm *shared = (tempsensor_shm *)(shm + shm_offset);

    // Create an addr_entry for this tempsensor
    struct addr_entry sensor_addr;
    sensor_addr.sensor_port = (in_port_t)port;
    if (inet_pton(AF_INET, ip, &sensor_addr.sensor_addr) <= 0) {
        perror("inet_pton()");
        exit(1);
    }

    // Initialize UDP socket
    int sockfd;
    struct sockaddr_in server_addr, client_addr, receiver_sockaddr; 
    socklen_t client_size = sizeof(client_addr);

    // Create a UDP Socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket error()");
        exit(1);
    }

    // Initialise server address
    memset(&server_addr, '\0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port); // Port for listening to incoming datagrams
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton()");
        exit(1);
    }

    // Bind the server address to the socket descriptor
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind error()");
        exit(1);
    }


    struct TEMP UDP_datagram;
    UDP_datagram.header[0] = 'T';
    UDP_datagram.header[1] = 'E';
    UDP_datagram.header[2] = 'M';
    UDP_datagram.header[3] = 'P';
    
    struct timeval last_update_time;
    struct timeval current_time;
    struct timespec max_wait_time;
    bool init = true;

    // Normal logic
    pthread_mutex_lock(&shared->mutex);
    while (1) {
        
        float temp_reading = shared->temperature;
        pthread_mutex_unlock(&shared->mutex);

        // Calculate the time elapsed since the last update
        gettimeofday(&current_time, NULL); // NULL indicates that timezone info is not needed
        int elapsed_time = (current_time.tv_sec - last_update_time.tv_sec) * 1000000 + (current_time.tv_usec - last_update_time.tv_usec);

        // Check if it's the first itteration of the loop or the temperature has changed or it's been max_update_wait microseconds since the last update
        if (init == true || temp_reading != shared->temperature || elapsed_time >= max_update_wait) {

            UDP_datagram.id = id;
            UDP_datagram.temperature = temp_reading;

            gettimeofday(&current_time, NULL);
            UDP_datagram.timestamp = current_time;
            
            UDP_datagram.address_count = 1;
            UDP_datagram.address_list[0] = sensor_addr;

            // Send the datagram to each receiver
            char *buffer = strdup(receiver_list);
            char *receiver = strtok(buffer, " ");

            while (receiver != NULL) {

                char *receiver_addr = strtok(addr, ":");
                int receiver_port = atoi(strtok(NULL, ":"));

                memset(&receiver_sockaddr, 0, sizeof(receiver_sockaddr));
                receiver_sockaddr.sin_family = AF_INET;
                receiver_sockaddr.sin_port = htons(receiver_port);
                if (inet_pton(AF_INET, receiver_addr, &receiver_sockaddr.sin_addr) <= 0) {
                    perror("inet_pton()");
                    exit(1);
                }
                
                // Send the datagram
                int bytes_sent = sendto(sockfd, &UDP_datagram, sizeof(UDP_datagram), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
                if (bytes_sent == -1) {
                    perror("sendto()");
                    exit(1);
                } 

                receiver = strtok(NULL, " ");
            }

            // Update the last update time
            gettimeofday(&last_update_time, NULL);

            init = false;
        }

        // Unlock the mutex
        pthread_mutex_unlock(&shared->mutex);

        // Receive the next UDP datagram if there is one waiting to be sent
        struct TEMP received_datagram;

        int bytes_received = recvfrom(sockfd, &received_datagram, sizeof(received_datagram), MSG_DONTWAIT, (struct sockaddr *)&client_addr, &client_size);
        if (bytes_received == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data is currently available
                continue;
            } else {
                perror("recvfrom()");
                exit(1);
            }

        } else {

            // Datagram received successfully
            if (received_datagram.address_count == MAX_ADDRESS_COUNT) {
 
                // Shifting elemenmax_wait_time.o the left by 1 
                for (int i = 0; i < MAX_ADDRESS_COUNT - 1; i++)
                {
                    received_datagram.address_list[i] = received_datagram.address_list[i + 1];
                }
                received_datagram.address_count--;
            }

            received_datagram.address_list[received_datagram.address_count] = sensor_addr;
            received_datagram.address_count++;


            char *buffer = strdup(receiver_list);
            char *token = strtok(buffer, " ");
            while (token != NULL) {

                char *receiver_addr = strtok(token, ":");
                int receiver_port = atoi(strtok(NULL, ":"));

                bool match = false;
                for (int i = 0; i < received_datagram.address_count && match == false; i++) {
                    
                    char *addr_entry = inet_ntoa(received_datagram.address_list[i].sensor_addr);
                    int port_entry = received_datagram.address_list[i].sensor_port;

                    // Compare to receiver addr
                    if (receiver_addr == addr_entry && receiver_port == port_entry) {
                        match = true;
                    }
                }

                if (!match) {
                    // Send the updated datagram to the current receiver if it is not in the received datagrams address list
                    memset(&receiver_sockaddr, 0, sizeof(receiver_sockaddr));
                    receiver_sockaddr.sin_family = AF_INET;
                    receiver_sockaddr.sin_port = htons(receiver_port);
                    if (inet_pton(AF_INET, receiver_addr, &receiver_sockaddr.sin_addr) <= 0) {
                        perror("inet_pton()");
                        exit(1);
                    }

                    int bytes_sent = sendto(sockfd, &received_datagram, sizeof(received_datagram), 0, (struct sockaddr *)&receiver_sockaddr, sizeof(receiver_sockaddr));
                    if (bytes_sent == -1) {
                        perror("sendto()");
                        exit(1);
                    } 
                }

                token = strtok(NULL, " ");
            }
        }
    
        // Update timespec max_wait_time to represent the value of max_condvar_wait
        gettimeofday(&current_time, NULL);
        max_wait_time.tv_sec = current_time.tv_sec;
        max_wait_time.tv_nsec = (current_time.tv_usec += max_condvar_wait) * 1000;

        // Lock the mutex and wait on 'cond' using pthread_cond_timedwait
        pthread_mutex_lock(&shared->mutex);
        int cond_wait_result = pthread_cond_timedwait(&shared->cond, &shared->mutex, &max_wait_time);
        if (cond_wait_result == -1) {
            if (errno == ETIMEDOUT) {
            // Timeout occurred.
            // Handle the timeout situation.
            } else {
                // Other error occurred. Handle it as needed.
                perror("pthread_cond_timedwait()");
                exit(1);
            }
        }
    }

    close(sockfd); // Close the UDP socket
    return 0;
}