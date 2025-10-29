#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <semaphore.h>
#include <curl/curl.h>
#include <json-c/json.h>

#include "telegram_core.h"
#include "db_core.h"
#include "logger.h"

// gcc src/programs/telegram_listener.c src/core/telegram_core.c src/utils/logger.c src/core/db_core.c -o build/telegram_listener -Iinclude -I/usr/include/mysql -lcurl -ljson-c -lmysqlclient $(pkg-config --cflags --libs libxml-2.0)

#define SHARED_MEM_NAME "/ytBot_shared_memory"
#define SEM_PRODUCER_NAME "/ytBot_mutex_producer"
#define SEM_CONSUMER_NAME "/ytBot_mutex_consumer"

#define QUEUE_BUFFER_SIZE 10

void exit_sys(const char *msg);

struct SHARED_OBJECT
{
    struct current_message qbuf[QUEUE_BUFFER_SIZE];
    size_t head;
    size_t tail;
};

int fdshm;
sem_t *sem_producer;
sem_t *sem_consumer;
void *shmaddr;
struct SHARED_OBJECT *so;
struct current_message cm;

void telegram_bot()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    long offset = 0;

    while (1)
    {
        char url[512];
        snprintf(url, sizeof(url),
                 "https://api.telegram.org/bot%s/getUpdates?offset=%ld&timeout=30",
                 TOKEN, offset);

        char *resp = http_get(url);
        if (!resp)
            continue;

        struct json_object *root = json_tokener_parse(resp);
        free(resp);
        if (!root)
            continue;

        struct json_object *ok = NULL;
        if (!json_object_object_get_ex(root, "ok", &ok) || !json_object_get_boolean(ok))
        {
            json_object_put(root);
            continue;
        }

        struct json_object *result = NULL;
        if (json_object_object_get_ex(root, "result", &result) &&
            json_object_is_type(result, json_type_array))
        {

            int len = json_object_array_length(result);
            for (int i = 0; i < len; i++)
            {
                struct json_object *update = json_object_array_get_idx(result, i);

                struct json_object *update_id_obj = NULL;
                if (json_object_object_get_ex(update, "update_id", &update_id_obj))
                    offset = json_object_get_int64(update_id_obj) + 1;

                struct json_object *message = NULL;
                if (json_object_object_get_ex(update, "message", &message))
                {
                    struct json_object *chat = NULL, *text = NULL;
                    if (json_object_object_get_ex(message, "chat", &chat) &&
                        json_object_object_get_ex(message, "text", &text))
                    {

                        struct json_object *chat_id_obj = NULL;
                        if (json_object_object_get_ex(chat, "id", &chat_id_obj))
                        {

                            long long chat_id = json_object_get_int64(chat_id_obj);
                            const char *msg_text = json_object_get_string(text);

                            struct json_object *username_obj = NULL;
                            const char *username = NULL;
                            if (json_object_object_get_ex(chat, "username", &username_obj))
                            {
                                username = json_object_get_string(username_obj);
                            }

                            struct json_object *first_name_obj = NULL;
                            const char *first_name = NULL;
                            if (json_object_object_get_ex(chat, "first_name", &first_name_obj))
                            {
                                first_name = json_object_get_string(first_name_obj);
                            }

                            cm.chat_id = chat_id;
                            strncpy(cm.msg_text, msg_text, sizeof(cm.msg_text) - 1);
                            cm.msg_text[sizeof(cm.msg_text) - 1] = '\0';

                            if (username)
                            {
                                strncpy(cm.username, username, sizeof(cm.username) - 1);
                            }
                            else if (first_name)
                            {
                                strncpy(cm.username, first_name, sizeof(cm.username) - 1);
                            }
                            else
                            {
                                strcpy(cm.username, "Unknown");
                            }
                            cm.username[sizeof(cm.username) - 1] = '\0';

                            if (sem_wait(sem_producer) == -1)
                            {
                                perror("sem_wait");
                                continue;
                            }

                            so->qbuf[so->tail] = cm;
                            so->tail = (so->tail + 1) % QUEUE_BUFFER_SIZE;

                            if (sem_post(sem_consumer) == -1)
                            {
                                perror("sem_post");
                                continue;
                            }

                            char reply[256];
                            snprintf(reply, sizeof(reply), "Merhaba %s! Mesajınız alındı ve rss akışı oluşturulmaya başlandı: %s", cm.username, msg_text);
                            send_message(chat_id, reply);
                        }
                    }
                }
            }
        }
        json_object_put(root);
    }
    curl_global_cleanup();
}

int main(void)
{
    log_init("logs/telegram_listener.log", LOG_LEVEL_DEBUG);

    create_database();
    if ((fdshm = shm_open(SHARED_MEM_NAME, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
        exit_sys("shm_open");

    if (ftruncate(fdshm, 4096) == -1)
    {
        perror("ftruncate");
        goto EXIT1;
    }

    if ((shmaddr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fdshm, 0)) == MAP_FAILED)
    {
        perror("mmap");
        goto EXIT2;
    }

    if ((sem_producer = sem_open(SEM_PRODUCER_NAME, O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, QUEUE_BUFFER_SIZE)) == NULL)
    {
        perror("sem_open");
        goto EXIT3;
    }

    if ((sem_consumer = sem_open(SEM_CONSUMER_NAME, O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 0)) == NULL)
    {
        perror("sem_open");
        goto EXIT4;
    }

    // Initialize shared object
    so = (struct SHARED_OBJECT *)shmaddr;
    so->head = 0;
    so->tail = 0;

    telegram_bot();

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

void exit_sys(const char *msg)
{
    log_error(msg);

    exit(EXIT_FAILURE);
}
