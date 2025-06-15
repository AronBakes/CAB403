#include "utility.h"

// Define global variables
const char *ADDRESS = "127.0.0.1";
const char *ALLOWED = "ALLOWED#";
const char *DENIED = "DENIED#";
const char *OPEN = "OPEN#";
const char *CLOSE = "CLOSE#";
const char *ALREADY = "ALREADY#";
const char *OPENING = "OPENING#";
const char *CLOSING = "CLOSING#";
const char *OPENED = "OPENED#";
const char *CLOSED = "CLOSED#";
const char *OPEN_EMERG = "OPEN_EMERG#";
const char *CLOSE_SECURE = "CLOSE_SECURE#";
const char *EMERGENCY = "EMERGENCY_MODE#";
const char *SECURE = "SECURE_MODE#";


// Implement functions
char *initialise_shm(const char *shm_path) {

    int fd = shm_open(shm_path, O_RDWR, 0666);
    if (fd == -1) {
        perror("Failed to open shared memory object");
        exit(1);
    }

    struct stat shm_stat;
    if (fstat(fd, &shm_stat) == -1) {
        perror("Failed to get file status information");
        exit(1);
    }

    char *shm = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("Failed to map file into memory");
        close(fd);
        exit(1);
    }

    return shm;
}

struct sockaddr_in initialise_sockaddr(char *arg) {

    char *saveptr;
    char *addr = strdup(arg);
    addr = strtok_r(addr, ":", &saveptr);
    int port = atoi(strtok_r(NULL, ":", &saveptr));

    struct sockaddr_in sockaddr;
    memset(&sockaddr, 0, sizeof(struct sockaddr_in));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    if (inet_pton(AF_INET, addr, &sockaddr.sin_addr) <= 0) {
        perror("Failed to convert IP address string to binary");
        exit(1);
    }

    free(addr);
    return sockaddr;
}

int process_token(char **token) {

    if (*token == NULL) {
        return -1;

    } else {
        // Remove newline character if it exists
        char *newline = strchr(*token, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }
        return 0;
    }
}

void send_message(int socket, const char* message) {

    ssize_t bytes_sent = send(socket, message, strlen(message), 0);
    if (bytes_sent == -1) {
        perror("Failed to send message");
        close(socket);
        exit(1);
    }
}

void receive_message(int socket, char *buffer, size_t buffer_size) {

    int offset = 0;
    char byte;  // Temporary char to receive a single byte

    while(1) {

        ssize_t bytes_received = recv(socket, &byte, 1, 0);
        if (bytes_received == -1) {
            // Handle error or closed connection
            perror("Failed to receive message");
            close(socket);

        } else if (bytes_received == 0) {
            perror("Connection was closed");
            //close(socket);
        }

        buffer[offset] = byte;
        offset++;

        if (byte == '#') {
            break;  // End of the message
        }

        if (offset == buffer_size - 1) {
            // Buffer is nearly full, we leave space for a null terminator
            break;

        } else if (offset >= buffer_size) {
            fprintf(stderr, "Unexpected behaviour from incoming message\n");
            close(socket);
            exit(1);
        }
    }

    buffer[offset] = '\0';  // Null terminate the string
}