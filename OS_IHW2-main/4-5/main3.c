#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <time.h>
#include <sys/mman.h>

#define SHM_NAME "/shm_example"
#define SEM_WRITER "/sem_writer"
#define SEM_READER_COUNT "/sem_reader_count"

int *shared_memory;
const int SIZE = 10;
int *reader_count;

long long fib(int n) {
    if (n == 0) return 0;
    if (n == 1) return 1;

    long long prev = 0;
    long long curr = 1;
    long long next;

    for (int i = 2; i <= n; i++) {
        next = prev + curr;
        prev = curr;
        curr = next;
    }

    return curr;
}

void setup_shared_resources(int initial_value) {
    sem_unlink(SEM_WRITER);
    sem_unlink(SEM_READER_COUNT);

    sem_t *writer = sem_open(SEM_WRITER, O_CREAT, 0666, 1);
    sem_t *reader_count_access = sem_open(SEM_READER_COUNT, O_CREAT, 0666, 1);
    if (writer == SEM_FAILED || reader_count_access == SEM_FAILED) {
        perror("Failed to create semaphores");
        exit(1);
    }

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Can't create shared memory");
        exit(1);
    }

    ftruncate(shm_fd, sizeof(int) * (initial_value + 1));
    shared_memory = (int *) mmap(NULL, sizeof(int) * (initial_value + 1), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_memory == (void *) -1) {
        perror("Can't map shared memory");
        exit(1);
    }

    for (int i = 0; i < initial_value; i++) {
        shared_memory[i] = i;
    }
    reader_count = &shared_memory[initial_value];
    *reader_count = 0;
}

void writer_process(int max_value) {
    sem_t *writer = sem_open(SEM_WRITER, 0);

    int *array = shared_memory;
    srand(time(NULL) ^ (getpid()<<16));

    for (int i = 0; i < 5; i++) {
        sem_wait(writer);
        int pos = rand() % max_value;
        int old_val = array[pos];
        int new_val = rand() % 100;
        array[pos] = new_val;
        printf("Writer [PID = %d] changed DB[index = %d] = %d to %d\n", getpid(), pos, old_val, new_val);
        sem_post(writer);
        sleep(2);
    }
    exit(0);
}

void reader_process(int max_value) {
    sem_t *writer = sem_open(SEM_WRITER, 0);
    sem_t *reader_count_access = sem_open(SEM_READER_COUNT, 0);

    int *array = shared_memory;

    srand(time(NULL) ^ (getpid()<<16));

    for (int i = 0; i < 5; i++) {
        sem_wait(reader_count_access);
        if (*reader_count == 0) sem_wait(writer);
        (*reader_count)++;
        sem_post(reader_count_access);

        int pos = rand() % max_value;
        printf("Reader [PID = %d] read DB[index = %d] = %d, Fibonacci[%d] = %lld \n", getpid(), pos, array[pos], array[pos], fib(array[pos]));

        sem_wait(reader_count_access);
        (*reader_count)--;
        if (*reader_count == 0) sem_post(writer);
        sem_post(reader_count_access);

        sleep(2);
    }
    exit(0);
}

void cleanup_resources() {
    if (shared_memory) {
        munmap(shared_memory, sizeof(int) * (SIZE + 1));
    }
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_WRITER);
    sem_unlink(SEM_READER_COUNT);
}


void signal_handler() {
    cleanup_resources();
    exit(0);
}

void setup_signal_handling() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error setting up signal handler");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    setup_signal_handling();
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <num_readers> <num_writers>\n", argv[0]);
        return 1;
    }

    int num_readers = atoi(argv[1]);
    int num_writers = atoi(argv[2]);

    setup_shared_resources(SIZE);

    for (int i = 0; i < num_readers; ++i) {
        if (fork() == 0) {
            reader_process(SIZE);
        }
    }

    for (int i = 0; i < num_writers; ++i) {
        if (fork() == 0) {
            writer_process(SIZE);
        }
    }

    for (int i = 0; i < num_readers + num_writers; i++) {
        wait(NULL);
    }

    cleanup_resources();
    return 0;
}
