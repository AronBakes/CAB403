#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define BUFFER_SIZE 256
#define MAX_CARDREADERS 40
#define MAX_DOORS 20
#define MAX_CALLPOINTS 20
#define MAX_FAIL_SAFE 100

#define MAX_DETECTIONS 50
#define MAX_ADDRESS_COUNT 50

extern const char *ADDRESS;
extern const char *ALLOWED;
extern const char *DENIED;
extern const char *OPEN;
extern const char *CLOSE;
extern const char *ALREADY;
extern const char *OPENING;
extern const char *CLOSING;
extern const char *OPENED;
extern const char *CLOSED;
extern const char *OPEN_EMERG;
extern const char *CLOSE_SECURE;
extern const char *EMERGENCY;
extern const char *SECURE;


// Function prototypes 
struct sockaddr_in initialise_sockaddr(char *arg);
char *initialise_shm(const char *shm_path);
int process_token(char **token);
void send_message(int socket, const char* message);
void receive_message(int socket, char *buffer, size_t buffer_size);


// Struct prototypes
typedef struct {
    char security_alarm; // '-' if inactive, 'A' if active
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} overseer_shm;

typedef struct {
    char alarm; // '-' if inactive, 'A' if active
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} firealarm_shm;

typedef struct {
    char scanned[16];
    pthread_mutex_t mutex;
    pthread_cond_t scanned_cond;
    char response; // 'Y' or 'N' (or '\0' at first)
    pthread_cond_t response_cond;
} card_shm;

typedef struct {
    char status; // 'O' for open, 'C' for closed, 'o' for opening, 'c' for closing
    pthread_mutex_t mutex;
    pthread_cond_t cond_start;
    pthread_cond_t cond_end;
} door_shm;

typedef struct {
    char status; // '-' for inactive, '*' for active
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} callpoint_shm;

typedef struct {
    float temperature;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} tempsensor_shm;

typedef struct {
    char scanned[16];
    uint8_t floor_select;
    pthread_mutex_t mutex;
    pthread_cond_t scanned_cond;
    char response; // 'Y' or 'N' (or '\0' at first)
    pthread_cond_t response_cond;
} destselct_shm;

typedef struct {
    char status; // 'O' for open, 'C' for closed and not moving, 'o' for opening, 'c' for closing, 'M' for moving
    char direction; // 'U' for up, 'D' for down, '-' for motionless.
    uint8_t floor;
    pthread_mutex_t mutex;
    pthread_cond_t cond_elevator;
    pthread_cond_t cond_controller;
} elevator_shm;

typedef struct {
    uint16_t current_angle;
    uint16_t min_angle;
    uint16_t max_angle;
    char status; // 'L' for turning left, 'R' for turning right, 'O' for on (and stationary), '-' for off
    uint8_t video[36][48];
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} camera_shm;



struct FIRE {
    char header[4]; // {'F', 'I', 'R', 'E'}
};

struct DOOR {    // Fail-safe doors only
    char header[4]; // {'D', 'O', 'O', 'R'}
    struct in_addr door_addr;
    in_port_t door_port;
};

struct DREG {
    char header[4]; // {'D', 'R', 'E', 'G'}
    struct in_addr door_addr;
    in_port_t door_port;
};

struct addr_entry {
    struct in_addr sensor_addr;
    in_port_t sensor_port;
};

struct TEMP {
    char header[4]; // {'T', 'E', 'M', 'P'}
    struct timeval timestamp;
    float temperature;
    uint16_t id;
    uint8_t address_count;
    struct addr_entry address_list[MAX_ADDRESS_COUNT];
};