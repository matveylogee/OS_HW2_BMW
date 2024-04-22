#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <string.h>

#define SHM_NAME "/shm_example"
#define SEM_WRITER "/sem_writer"
#define SEM_READER "/sem_reader"
#define SEM_MUTEX1 "/sem_mutex1"
#define SEM_MUTEX2 "/sem_mutex2"
#define ARRAY_SIZE 10

typedef struct {
    int data[ARRAY_SIZE];
    int readcount;
    int writecount;
} shared_data;

shared_data *shm = NULL;
sem_t *w, *r, *mutex1, *mutex2;
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
    if (sem_close(w) == -1) {
        perror("Incorrect close of w semaphore");
    }
    if (sem_close(r) == -1) {
        perror("Incorrect close of r semaphore");
    }
    if (sem_close(mutex1) == -1) {
        perror("Incorrect close of mutex1 semaphore");
    }
    if (sem_close(mutex2) == -1) {
        perror("Incorrect close of mutex2 semaphore");
    }
    sem_unlink(SEM_WRITER);
    sem_unlink(SEM_READER);
    sem_unlink(SEM_MUTEX1);
    sem_unlink(SEM_MUTEX2);
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


    w = sem_open(SEM_WRITER, O_CREAT, 0666, 1);
    r = sem_open(SEM_READER, O_CREAT, 0666, 1);
    mutex1 = sem_open(SEM_MUTEX1, O_CREAT, 0666, 1);
    mutex2 = sem_open(SEM_MUTEX2, O_CREAT, 0666, 1);


    if ((shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666)) == -1) {
        perror("Can't create shared memory<-\n");
        exit(10);
    }

    if (ftruncate(shm_fd, sizeof(shared_data)) == -1) {
        perror("Can't truncate shared memory<-\n");
        exit(10);
    }

    shm = mmap(NULL, sizeof(shared_data), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    for (int i = 0; i < ARRAY_SIZE; i++) {
        shm->data[i] = i + 1;
    }
    shm->readcount = 0;
    shm->writecount = 0;

    for (int i = 0; i < N + K; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            srand(time(NULL) ^ (getpid() << 16));
            if (i < N) {
                sem_wait(r);
                sem_wait(mutex1);
                shm->readcount++;
                if (shm->readcount == 1)
                    sem_wait(w);
                sem_post(mutex1);
                sem_post(r);

                printf("Reader PID %d reading...\n", getpid());
                sleep(2);
                int index = rand() % ARRAY_SIZE;
                printf("Reader [PID: %d] read DB[index = %d] = %d, Fibnacci[%d] = %lld\n", getpid(), index,
                       shm->data[index], shm->data[index], fib(shm->data[index]));

                sem_wait(mutex1);
                shm->readcount--;
                if (shm->readcount == 0)
                    sem_post(w);
                sem_post(mutex1);
                exit(0);
            } else {
                sem_wait(mutex2);
                shm->writecount++;
                if (shm->writecount == 1)
                    sem_wait(r);
                sem_post(mutex2);

                sem_wait(w);

                int index = rand() % ARRAY_SIZE;
                int old_value = shm->data[index];
                int new_value = rand() % 100 + 1;
                shm->data[index] = new_value;
                printf("Writer [PID:  %d] updated DB[index= %d] = %d to %d\n", getpid(), index, old_value, new_value);
                sleep(2);
                sem_post(w);

                sem_wait(mutex2);
                shm->writecount--;
                if (shm->writecount == 0)
                    sem_post(r);
                sem_post(mutex2);
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
