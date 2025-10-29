#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "telegram_core.h"
#include "logger.h"

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, struct mem *userp) {
    size_t realsize = size * nmemb;
    char *ptr = realloc(userp->data, userp->size + realsize + 1);
    if (!ptr) {
        log_error("Yetersiz bellek!");
        return 0;
    }

    userp->data = ptr;
    memcpy(&(userp->data[userp->size]), contents, realsize);
    userp->size += realsize;
    userp->data[userp->size] = 0;

    return realsize;
}

char* http_get(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    struct mem chunk = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        log_error("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        free(chunk.data);
        chunk.data = NULL;
    }

    curl_easy_cleanup(curl);
    return chunk.data;
}

int send_message(long long chat_id, const char *text) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", TOKEN);

    struct json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "chat_id", json_object_new_int64(chat_id));
    json_object_object_add(jobj, "text", json_object_new_string(text));
    const char *json_str = json_object_to_json_string(jobj);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        log_error("Telegram mesaj gönderme hatası: %s", curl_easy_strerror(res));

    curl_slist_free_all(headers);
    json_object_put(jobj);
    curl_easy_cleanup(curl);
    return 0;
}