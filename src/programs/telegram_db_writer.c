#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <json-c/json.h>

#include "telegram_core.h"
#include "logger.h"
#include "db_core.h"

// gcc src/programs/telegram_db_writer.c src/core/telegram_core.c src/core/db_core.c src/utils/logger.c -Iinclude -I/usr/include/mysql -o build/telegram_writer $(pkg-config --cflags --libs libxml-2.0) -lmysqlclient -ljson-c -lcurl -pthread -lrt

#define SHARED_MEM_NAME            "/ytBot_shared_memory"
#define SEM_PRODUCER_NAME        "/ytBot_mutex_producer"
#define SEM_CONSUMER_NAME        "/ytBot_mutex_consumer"

#define QUEUE_BUFFER_SIZE   10

void exit_sys(const char* msg);

struct SHARED_OBJECT {
    struct current_message qbuf[QUEUE_BUFFER_SIZE];
    size_t head;
    size_t tail;
};

int main(void)
{
    log_init("logs/telegram_db_writer.log", LOG_LEVEL_DEBUG);

    int fdshm;
    sem_t *sem_producer;
    sem_t *sem_consumer;
    void *shmaddr;
    struct SHARED_OBJECT *so;

    if ((fdshm = shm_open(SHARED_MEM_NAME, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
        exit_sys("shm_open");

    if (ftruncate(fdshm, 4096) == -1) {
        perror("ftruncate");
        goto EXIT1;
    }
    
    if ((shmaddr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fdshm, 0)) == MAP_FAILED) {
        perror("mmap");
        goto EXIT2;
    }

    if ((sem_producer = sem_open(SEM_PRODUCER_NAME, O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, QUEUE_BUFFER_SIZE)) == NULL) {
        perror("sem_open");
        goto EXIT3;
    }

    if ((sem_consumer = sem_open(SEM_CONSUMER_NAME, O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, 0)) == NULL) {
        perror("sem_open");
        goto EXIT4;
    }

    so = (struct SHARED_OBJECT *)shmaddr;
    log_info("Consumer başlatıldı. Telegram mesajları bekleniyor...");

    for (;;) {
        if (sem_wait(sem_consumer) == -1) {
            perror("sem_wait");
            goto EXIT5;
        }
        
        // Read message
        struct current_message msg = so->qbuf[so->head];
        so->head = (so->head + 1) % QUEUE_BUFFER_SIZE;
        
        if (sem_post(sem_producer) == -1) {
            perror("sem_post");
            goto EXIT5;
        }

        log_debug("Yeni mesaj alındı: Chat ID: %lld, Username: %s, Channel: %s", msg.chat_id, msg.username, msg.msg_text);
        insert_user(msg.chat_id, msg.username);
        insert_channel(msg.msg_text, "msg.channel_name", "msg.channel_link");

        user_channel_subscribe(msg.chat_id, msg.msg_text);
    }
    
EXIT5:
    sem_close(sem_consumer);
    if (sem_unlink(SEM_CONSUMER_NAME) == -1 && errno != ENOENT)
        exit_sys("sem_unlink");
EXIT4:
    sem_close(sem_producer);
    if (sem_unlink(SEM_PRODUCER_NAME) == -1 && errno != ENOENT)
        exit_sys("sem_unlink");
EXIT3:
    if (munmap(shmaddr, 4096) == -1)
        exit_sys("munmap");
EXIT2:
    close(fdshm);
EXIT1:
    if (shm_unlink(SHARED_MEM_NAME) == -1 && errno != ENOENT)
        exit_sys("shm_unlink");

    return 0;
}

void exit_sys(const char* msg)
{
    log_error(msg);

    exit(EXIT_FAILURE);
}
