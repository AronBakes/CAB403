//{id} {address:port} {temperature change threshold} {shared memory path} {shared memory offset} {overseer address:port}

#include "utility.h"


int main(int argc, char **argv) { 

    if (argc > 7) {
        (void) fprintf(stderr, "Too many arguments\n");
        exit(0);
    }

    int id = atoi(argv[1]);
    char *camera_addr = argv[2];
    int temmp_change = argv[3];
    const char *shm_path = argv[4];
    int shm_offset = atoi(argv[5]);
    char *overseer_addr = argv[6];


}