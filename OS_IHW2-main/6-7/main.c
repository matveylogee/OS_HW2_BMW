#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

#define SHM_NAME "/shm_example"
#define ARRAY_SIZE 10

typedef struct {
    int data[ARRAY_SIZE];
    int readcount;
    int writecount;
    sem_t w, r, mutex1, mutex2;
} shared_data;

shared_data *shm = NULL;
int shm_fd = -1;

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

void cleanup_resources() {
    if (shm != NULL) {
        sem_destroy(&shm->w);
        sem_destroy(&shm->r);
        sem_destroy(&shm->mutex1);
        sem_destroy(&shm->mutex2);
        if (munmap(shm, sizeof(shared_data)) == -1) {
            perror("Incorrect munmap");
        }
        shm = NULL;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_unlink(SHM_NAME);
        shm_fd = -1;
    }
}

void signal_handler(int sig) {
    cleanup_resources();
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <number of readers> <number of writers>\n", argv[0]);
        return 1;
    }

    int N = atoi(argv[1]);
    int K = atoi(argv[2]);

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Can't create shared memory");
        exit(10);
    }
    if (ftruncate(shm_fd, sizeof(shared_data)) == -1) {
        perror("Can't truncate shared memory");
        exit(10);
    }
    shm = mmap(NULL, sizeof(shared_data), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    sem_init(&shm->w, 1, 1);
    sem_init(&shm->r, 1, 1);
    sem_init(&shm->mutex1, 1, 1);
    sem_init(&shm->mutex2, 1, 1);
    shm->readcount = 0;
    shm->writecount = 0;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        shm->data[i] = i + 1;
    }

    for (int i = 0; i < N + K; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            srand(time(NULL) ^ (getpid() << 16));
            if (i < N) {
                sem_wait(&shm->r);
                sem_wait(&shm->mutex1);
                shm->readcount++;
                if (shm->readcount == 1)
                    sem_wait(&shm->w);
                sem_post(&shm->mutex1);
                sem_post(&shm->r);

                printf("Reader PID %d reading...\n", getpid());
                sleep(2);
                int index = rand() % ARRAY_SIZE;
                printf("Reader [PID: %d] read DB[index = %d] = %d, Fibnacci[%d] = %lld\n", getpid(), index, shm->data[index], shm->data[index], fib(shm->data[index])) ;

                sem_wait(&shm->mutex1);
                shm->readcount--;
                if (shm->readcount == 0)
                    sem_post(&shm->w);
                sem_post(&shm->mutex1);
                exit(0);
            } else {
                sem_wait(&shm->mutex2);
                shm->writecount++;
                if (shm->writecount == 1)
                    sem_wait(&shm->r);
                sem_post(&shm->mutex2);

                sem_wait(&shm->w);

                int index = rand() % ARRAY_SIZE;
                int old_value = shm->data[index];
                int new_value = rand() % 100 + 1;
                shm->data[index] = new_value;
                printf("Writer [PID:  %d] updated DB[index= %d] = %d to %d\n", getpid(), index, old_value, new_value);
                sleep(2);
                sem_post(&shm->w);

                sem_wait(&shm->mutex2);
                shm->writecount--;
                if (shm->writecount == 0)
                    sem_post(&shm->r);
                sem_post(&shm->mutex2);
                exit(0);
            }
        }
    }

    // Ожидание завершения всех процессов
    for (int i = 0; i < N + K; i++) {
        wait(NULL);
    }

    // Освобождение ресурсов
    cleanup_resources();

    return 0;
}
