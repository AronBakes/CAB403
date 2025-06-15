//{id} {address:port} {wait time (in microseconds)} {door open time (in microseconds)} {shared memory path} {shared memory offset} {overseer address:port}

#include "utility.h"


int main(int argc, char **argv) { 

    if (argc > 8) {
        (void) fprintf(stderr, "Too many arguments\n");
        exit(0);
    }

    int id = atoi(argv[1]);
    char *elevator_addr = argv[2];
    int wait_time = atoi(argv[3]);
    int open_time = atoi(argv[4]);
    const char *shm_path = argv[5];
    int shm_offset = atoi(argv[6]);
    char *overseer_addr = argv[7];


}