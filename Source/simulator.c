#include "utility.h"
#define MAX_COMPONENTS 82
#define SPAWN_DELAY 250000
#define DELAY 1000000

struct shm_struct {
    overseer_shm overseer;
    firealarm_shm firealarm;
    card_shm cards[MAX_CARDREADERS]; 
    door_shm doors[MAX_DOORS];
    callpoint_shm callpoints[MAX_CALLPOINTS];
};

struct door_threads {
    struct shm_struct *shared;
    int door_num;
    int open_close_time;
};

volatile int shutdown_flag = 0;
pthread_t door_threads[MAX_DOORS];
pid_t pids[MAX_COMPONENTS];
int pid_cnt = 0;
int overseer_num = 0;
int firealarm_num = 0;  
int cardreader_num = 0;
int door_num = 0;
int callpoint_num = 0;
int port_num = 3000;
char overseer_addr[BUFFER_SIZE];
char firealarm_addr[BUFFER_SIZE];
const char *shm_path = "/shm";

struct shm_struct* create_shm(const char *shm_path) {

    int fd = shm_open(shm_path, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("Failed to open shared memory object");
        exit(1);
    }

    // Allocate the size of your struct in the shared memory
    if (ftruncate(fd, sizeof(struct shm_struct)) == -1) {
        perror("ftruncate");
        exit(1);
    }

    // Map the shared memory
    struct shm_struct *shared = (struct shm_struct *) mmap(0, sizeof(struct shm_struct), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        perror("Failed to map file into memory");
        exit(1);
    }

    return shared;
}

int init_components(FILE *scenario, struct shm_struct *shared) {

    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_condattr_init(&cond_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);

    memset(&shared->overseer, 0, sizeof(shared->overseer));
    memset(&shared->firealarm, 0, sizeof(shared->firealarm));
    memset(&shared->cards, 0, sizeof(shared->cards));       
    memset(&shared->doors, 0, sizeof(shared->doors));       
    memset(&shared->callpoints, 0, sizeof(shared->callpoints)); 

    char buffer[BUFFER_SIZE];
    int card_cnt = 0;
    int door_cnt = 0;
    int callpoint_cnt = 0;

    // Read data from the scenario file
    while (fgets(buffer, sizeof(buffer), scenario) != NULL) {

        // Process the line
        char *token = strtok(buffer, " ");
        if (process_token(&token) == -1) {
            fprintf(stderr, "NULL token\n");
            exit(1);
        }

        if (strcmp(token, "INIT") == 0) {
            
            // Initialise component variables
            char *component = strtok(NULL, " ");   
            if (process_token(&component) == -1) {
                fprintf(stderr, "NULL token\n");
                exit(1);
            }

            if (strcmp(component, "overseer") == 0) {
                
                pthread_mutex_init(&shared->overseer.mutex, &mutex_attr);
                pthread_cond_init(&shared->overseer.cond, &cond_attr);

                pthread_mutex_lock(&shared->overseer.mutex);
                shared->overseer.security_alarm = '-';
                pthread_mutex_unlock(&shared->overseer.mutex);
                
            } else if (strcmp(component, "firealarm") == 0) {

                pthread_mutex_init(&shared->firealarm.mutex, &mutex_attr);
                pthread_cond_init(&shared->firealarm.cond, &cond_attr);

                pthread_mutex_lock(&shared->firealarm.mutex);
                shared->firealarm.alarm = '-';
                pthread_mutex_unlock(&shared->firealarm.mutex);

            } else if (strcmp(component, "cardreader") == 0) {

                pthread_mutex_init(&shared->cards[card_cnt].mutex, &mutex_attr);
                pthread_cond_init(&shared->cards[card_cnt].scanned_cond, &cond_attr);
                pthread_cond_init(&shared->cards[card_cnt].response_cond, &cond_attr);

                pthread_mutex_lock(&shared->cards[card_cnt].mutex);
                memset(&shared->cards[card_cnt].scanned, '\0', sizeof(char)*16);
                shared->cards[card_cnt].response = '\0';
                pthread_mutex_unlock(&shared->cards[card_cnt].mutex);

                card_cnt++;

            } else if (strcmp(component, "door") == 0) {

                pthread_mutex_init(&shared->doors[door_cnt].mutex, &mutex_attr);
                pthread_cond_init(&shared->doors[door_cnt].cond_start, &cond_attr);
                pthread_cond_init(&shared->doors[door_cnt].cond_end, &cond_attr);

                pthread_mutex_lock(&shared->doors[door_cnt].mutex);
                shared->doors[door_cnt].status = 'C';
                pthread_mutex_unlock(&shared->doors[door_cnt].mutex);

                door_cnt++;

            } else if (strcmp(component, "callpoint") == 0) {

                pthread_mutex_init(&shared->callpoints[callpoint_cnt].mutex, &mutex_attr);
                pthread_cond_init(&shared->callpoints[callpoint_cnt].cond, &cond_attr);

                pthread_mutex_lock(&shared->callpoints[callpoint_cnt].mutex);
                shared->callpoints[callpoint_cnt].status = '-';
                pthread_mutex_unlock(&shared->callpoints[callpoint_cnt].mutex);

                callpoint_cnt++;

            } else {
                // Unrecognised component, skip
                continue;
            }
                        
            
        } else if (strcmp(token, "SCENARIO") == 0) {
            // End of initialisation
            pthread_mutexattr_destroy(&mutex_attr);
            pthread_condattr_destroy(&cond_attr);
            return 0; 

        }
    }

    pthread_mutexattr_destroy(&mutex_attr);
    pthread_condattr_destroy(&cond_attr);
    return -1;
}

void *door_status(void *args) {

    struct door_threads *thread_args = (struct door_threads *)args;
    struct shm_struct *shared = thread_args->shared;
    int door_num = thread_args->door_num;
    int open_close_time = thread_args->open_close_time;

    pthread_mutex_lock(&shared->doors[door_num].mutex);
    while(!shutdown_flag) {

        pthread_cond_wait(&shared->doors[door_num].cond_start, &shared->doors[door_num].mutex);
        if (shutdown_flag) {
            break;  // Break out of the loop if shutdown_flag is set
        }

        if (shared->doors[door_num].status == 'C' || shared->doors[door_num].status == 'O') {
            continue;
        }

        usleep(open_close_time);
        if (shared->doors[door_num].status == 'c') {
            shared->doors[door_num].status = 'C';

        } else if (shared->doors[door_num].status == 'o') {
            shared->doors[door_num].status = 'O'; 
        }
        pthread_cond_signal(&shared->doors[door_num].cond_end);
    }

    free(thread_args);
    return NULL;
}

int spawn(char *buffer, struct shm_struct *shared) {

    // Process the buffer
    char *saveptr;
    char *token = strtok_r(buffer, " ", &saveptr);
    if (process_token(&token) == -1) {
        fprintf(stderr, "NULL token\n");
        exit(1);
    }
    
    if (strcmp(token, "INIT") == 0) {
        
        char offset[BUFFER_SIZE];
        char *component = strtok_r(NULL, " ", &saveptr);   
        if (process_token(&component) == -1) {
            fprintf(stderr, "NULL token\n");
            exit(1);
        }
        
        if (strcmp(component, "overseer") == 0) {

            if (overseer_num > 0) {
                fprintf(stderr, "Exceeded maximum number of overseers\n");
                exit(1);
            }

            char *door_open_duration = strtok_r(NULL, " ", &saveptr);
            char *datagram_resDELAY = strtok_r(NULL, " ", &saveptr);
            char *authorisation_file = strtok_r(NULL, " ", &saveptr);
            char *connections_file = strtok_r(NULL, " ", &saveptr);
            char *layout_file = strtok_r(NULL, " ", &saveptr);
            if (
                process_token(&door_open_duration) == -1 ||
                process_token(&datagram_resDELAY) == -1 ||
                process_token(&authorisation_file) == -1 ||
                process_token(&connections_file) == -1 ||
                process_token(&layout_file) == -1 
            ) {
                fprintf(stderr, "NULL token\n");
                exit(1);
            }

            ssize_t shm_offset = offsetof(struct shm_struct, overseer);
            snprintf(offset, BUFFER_SIZE, "%zd", shm_offset);
            snprintf(overseer_addr, BUFFER_SIZE, "%s:%d", ADDRESS, port_num);

            // Fork a new process
            pid_t pid = fork();
            if (pid == -1) {
                perror("Failed to create new process");
                exit(1);

            } else if (pid > 0) {
                // Parent process
                execl("overseer", "overseer", overseer_addr, door_open_duration, datagram_resDELAY, authorisation_file, connections_file, layout_file, shm_path, offset, (char *)NULL);
                perror("Failed to execute program");
                exit(1);
            } 

            // Child process
            pids[pid_cnt++] = getppid();
            overseer_num++;
            
        } else if (strcmp(component, "firealarm") == 0) {

            if (firealarm_num > 0) {
                fprintf(stderr, "Exceeded maximum number of firealarms\n");
                exit(1);
            }

            char *temp_threshold = strtok_r(NULL, " ", &saveptr);
            char *min_detections = strtok_r(NULL, " ", &saveptr);
            char *detection_period = strtok_r(NULL, " ", &saveptr);
            char *reserved = "0";
            if (
                process_token(&temp_threshold) == -1 ||
                process_token(&min_detections) == -1 ||
                process_token(&detection_period) == -1
            ) {
                fprintf(stderr, "NULL token\n");
                exit(1);
            }

            ssize_t shm_offset = offsetof(struct shm_struct, firealarm);
            snprintf(offset, BUFFER_SIZE, "%zd", shm_offset);
            snprintf(firealarm_addr, BUFFER_SIZE, "%s:%d", ADDRESS, port_num);

            pid_t pid = fork();
            if (pid == -1) {
                perror("Failed to create new process");
                exit(1);

            } else if (pid == 0) {
                // Child process
                execl("firealarm", "firealarm", firealarm_addr, temp_threshold, min_detections, detection_period, reserved, shm_path, offset, overseer_addr, (char *)NULL);
                perror("Failed to execute program");
                exit(1);
            } 

            // Parent process
            pids[pid_cnt++] = pid;  // Store the child's process ID
            firealarm_num++;  

        } else if (strcmp(component, "cardreader") == 0) {

            if (cardreader_num >= MAX_CARDREADERS) {
                fprintf(stderr, "Exceeded maximum number of cardreaders\n");
                exit(1);
            }

            char *cardreader_id = strtok_r(NULL, " ", &saveptr);
            char *wait_time = strtok_r(NULL, " ", &saveptr);
            if (
                process_token(&cardreader_id) == -1 ||
                process_token(&wait_time) == -1
            ) {
                fprintf(stderr, "NULL token\n");
                exit(1);
            }

            ssize_t shm_offset = offsetof(struct shm_struct, cards) + (cardreader_num * sizeof(card_shm));
            snprintf(offset, BUFFER_SIZE, "%zd", shm_offset);

            pid_t pid = fork();
            if (pid == -1) {
                perror("Failed to create new process");
                exit(1);

            } else if (pid == 0) {
                // Child process
                execl("cardreader", "cardreader", cardreader_id, wait_time, shm_path, offset, overseer_addr, (char *)NULL);
                perror("Failed to execute program");
                exit(1);
            } 

            // Parent process
            pids[pid_cnt++] = pid;  // Store the child's process ID
            cardreader_num++;
  
        } else if (strcmp(component, "door") == 0) {
            
            if (door_num >= MAX_DOORS) {
                fprintf(stderr, "Exceeded maximum number of doors\n");
                exit(1);
            }

            char *door_id = strtok_r(NULL, " ", &saveptr);
            char *configuration = strtok_r(NULL, " ", &saveptr);
            char *open_close_str = strtok_r(NULL, " ", &saveptr);
            if (
                process_token(&door_id) == -1 ||
                process_token(&configuration) == -1 ||
                process_token(&open_close_str) == -1
            ) {
                fprintf(stderr, "NULL token\n");
                exit(1);
            }

            ssize_t shm_offset = offsetof(struct shm_struct, doors) + (door_num * sizeof(door_shm));
            snprintf(offset, BUFFER_SIZE, "%zd", shm_offset);

            int open_close_time = atoi(open_close_str);
            char door_addr[BUFFER_SIZE];
            snprintf(door_addr, BUFFER_SIZE, "%s:%d", ADDRESS, port_num);

            pid_t pid = fork();
            if (pid == -1) {
                perror("Failed to create new process");
                exit(1);

            } else if (pid == 0) {
                // Child process
                execl("door", "door", door_id, door_addr, configuration, shm_path, offset, overseer_addr, (char *)NULL);
                perror("Failed to execute program");
                exit(1);
            }

            // Parent process
            // Dynamically allocate memory and assign variables for the argument struct
            struct door_threads *new_door_thread = (struct door_threads *)malloc(sizeof(struct door_threads));
            new_door_thread->shared = shared;
            new_door_thread->door_num = door_num;
            new_door_thread->open_close_time = open_close_time;

            // Create thread for door status
            pthread_create(&door_threads[door_num++], NULL, door_status, new_door_thread);
            pids[pid_cnt++] = pid;  // Store the child's process ID      

        } else if (strcmp(component, "callpoint") == 0) {

            if (cardreader_num >= MAX_CALLPOINTS) {
                fprintf(stderr, "Exceeded maximum number of callpoints\n");
                exit(1);
            }

            char *resend_delay = strtok_r(NULL, " ", &saveptr);
            if (process_token(&resend_delay) == -1) {
                fprintf(stderr, "NULL token\n");
                exit(1);
            }

            ssize_t shm_offset = offsetof(struct shm_struct, callpoints) + (callpoint_num * sizeof(callpoint_shm));
            snprintf(offset, BUFFER_SIZE, "%zd", shm_offset);

            pid_t pid = fork();
            if (pid == -1) {
                perror("Failed to create new process");
                exit(1);

            } else if (pid == 0) {
                // Child process
                execl("callpoint", "callpoint", resend_delay, shm_path, offset, firealarm_addr, (char *)NULL);
                perror("Failed to execute program");
                exit(1);

            } 

            // Parent process
            pids[pid_cnt++] = pid;  // Store the child's process ID
            callpoint_num++;
        }
            
        port_num++;
        return 0;
        
    } else {
        return -1;
    }
}

int run_event(char *buffer, int runtime, struct shm_struct *shared) {

    // Process the line
    char* saveptr;
    char *token = strtok_r(buffer, " ", &saveptr);
    char *event = strtok_r(NULL, " ", &saveptr);
    char *num_str = strtok_r(NULL, " ", &saveptr);
    if (
        process_token(&token) == -1 ||
        process_token(&event) == -1 ||
        process_token(&num_str) == -1
    ) {
        fprintf(stderr, "NULL token\n");
        exit(1);
    }

    int timestamp = atoi(token);
    int num = atoi(num_str);
    int time_delta = timestamp - runtime;
    if (time_delta <= 0) {
        fprintf(stderr, "Invalid timestamp\n");
        return -1;
    }

    if (strcmp(event, "CARD_SCAN") == 0) {

        char *code = strtok_r(NULL, " ", &saveptr);
        process_token(&code);

        usleep(time_delta);
        pthread_mutex_lock(&shared->cards[num].mutex);
        
        for (int i = 0; i < 16; i++) {
            shared->cards[num].scanned[i] = code[i];
        }

        pthread_mutex_unlock(&shared->cards[num].mutex);
        pthread_cond_signal(&shared->cards[num].response_cond);
        return timestamp;

    } else if (strcmp(event, "CALLPOINT_TRIGGER") == 0) {

        usleep(time_delta);
        pthread_mutex_lock(&shared->callpoints[num].mutex);
        shared->callpoints[num].status = '*';
        pthread_mutex_unlock(&shared->callpoints[num].mutex);
        pthread_cond_signal(&shared->callpoints[num].cond);
        return timestamp;

    } else {
        // Unsupported event - skip
        return runtime;
    }
}

void terminate(struct shm_struct *shared) {

    shutdown_flag = 1;

    // Signal threads to terminate then join them
    for (int i = 0; i < door_num; i++) {
        pthread_mutex_lock(&shared->doors[i].mutex);
        pthread_cond_signal(&shared->doors[i].cond_start);
        pthread_mutex_unlock(&shared->doors[i].mutex);
        pthread_join(door_threads[i], NULL);
    }

    // Terminate child processes
    for (int i = 0; i < pid_cnt; i++) {

        if (kill(pids[i], SIGTERM) != 0) {
            perror("Error terminating process with SIGTERM");
            kill(pids[i], SIGKILL);  // Force termination if SIGTERM failed
        }
    }

    // Cleanup
    munmap(shared, sizeof(struct shm_struct));
}


int main(int argc, char **argv) {

    if (argc > 2) {
        fprintf(stderr, "Too many arguments\n");
        exit(0);
    }

    const char *scenario_file = argv[1];

    struct shm_struct *shared = create_shm(shm_path);
    shared = (struct shm_struct *)malloc(sizeof(struct shm_struct));
    if (shared == NULL) {
        perror("Failed to allocate memory");
        exit(1);
    }

    FILE *scenario; 
    scenario = fopen(scenario_file, "r");
    if (scenario == NULL) {
        perror("Failed to open scenario file");
        exit(1);
    }

    if (init_components(scenario, shared) == -1) {
        fprintf(stderr, "Unexpected token\n");
        fclose(scenario); 
        exit(1);
    }

    rewind(scenario);
    char buffer[BUFFER_SIZE];
    bool firstLine = true;
    int runtime = 0;

    while (fgets(buffer, sizeof(buffer), scenario) != NULL) {
        
        // If it's the first line and doesn't start with "INIT overseer", error out.
        if (firstLine) {
            if (strncmp(buffer, "INIT overseer", 13) != 0) {
                fprintf(stderr, "First component is not the overseer\n");
                fclose(scenario); 
                exit(1);
            }
            firstLine = false;
        }

        if (strncmp(buffer, "INIT", 4) == 0) {
            if (spawn(buffer, shared) == -1) {
                perror("Unexpected token");
                fclose(scenario); 
                exit(1);
            }
            usleep(SPAWN_DELAY);

        } else if (strncmp(buffer, "SCENARIO", 8) == 0) {
            usleep(DELAY);
            continue;
        
        } else {
            // Run events
            runtime = run_event(buffer, runtime, shared);
            if (runtime == -1) {
                perror("Invalid token");
                fclose(scenario); 
                exit(1);
            }
        }
    }

    fclose(scenario);   
    usleep(DELAY);
    terminate(shared);
    return 0;
}