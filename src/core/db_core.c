#include <stdio.h>
#include <string.h>
#include <mysql/mysql.h>
#include "logger.h"
#include "db_core.h"
#include "telegram_core.h"


// Sorgu çalıştırma fonksiyonu
static void execute_query(MYSQL *conn, const char *query, const char *msg) {
    if (mysql_query(conn, query)) {
        log_error("%s hatası: %s", msg, mysql_error(conn));
    } else {
        log_info("%s", msg);
    }
}

// Veritabanına bağlanma fonksiyonu
static MYSQL *connect_db() {
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        log_error("MySQL init hatası");
        exit(1);
    }

    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0)) {
        log_error("MySQL bağlantı hatası: %s", mysql_error(conn));
        exit(1);
    }

    return conn;
}

// Kullanıcı ekleme veya güncelleme
void insert_user(long long chat_id, const char *username) {
    MYSQL *conn = connect_db();
    char query[1024];
    snprintf(query, sizeof(query),
             "INSERT INTO users (chat_id, username) "
             "VALUES (%lld, '%s') "
             "ON DUPLICATE KEY UPDATE username=VALUES(username);",
             chat_id, username ? username : "");
    execute_query(conn, query, "Kullanıcı eklendi/güncellendi");
    mysql_close(conn);
}

// Kanal ekleme veya güncelleme
void insert_channel(const char *channel_id, const char *name, const char *link) {
    MYSQL *conn = connect_db();
    char query[2048];
    snprintf(query, sizeof(query),
             "INSERT INTO channels (channel_id, name, link) "
             "VALUES ('%s', '%s', '%s') "
             "ON DUPLICATE KEY UPDATE name=VALUES(name), link=VALUES(link);",
             channel_id, name ? name : "", link ? link : "");
    execute_query(conn, query, "Kanal eklendi/güncellendi");
    mysql_close(conn);
}

// Kullanıcının kanala abone olması
void user_channel_subscribe(long long chat_id, const char *channel_id) {
    MYSQL *conn = connect_db();
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO user_channels (user_chat_id, channel_id) "
             "VALUES (%lld, '%s');",
             chat_id, channel_id);
    execute_query(conn, query, "Kullanıcı kanala abone oldu");
    mysql_close(conn);
}

// Kullanıcının kanala abone olması ve mevcut videolar için durum oluşturulması
void user_channel_subscribe_with_history(long long chat_id, const char *channel_id) {
    MYSQL *conn = connect_db();
    mysql_autocommit(conn, 0);
    
    char subscribe_query[512];
    snprintf(subscribe_query, sizeof(subscribe_query),
             "INSERT IGNORE INTO user_channels (user_chat_id, channel_id) "
             "VALUES (%lld, '%s');",
             chat_id, channel_id);
    
    if (mysql_query(conn, subscribe_query)) {
        log_error("Abonelik hatası: %s", mysql_error(conn));
        mysql_rollback(conn);
        mysql_close(conn);
        return;
    }
    
    // Bu kanalın mevcut tüm videoları için kullanıcıya durum oluştur
    char status_query[1024];
    snprintf(status_query, sizeof(status_query),
             "INSERT IGNORE INTO user_video_status (user_chat_id, video_id, is_sent) "
             "SELECT %lld, v.id, FALSE "
             "FROM videos v "
             "WHERE v.channel_id = '%s';",
             chat_id, channel_id);
    
    if (mysql_query(conn, status_query)) {
        log_error("Video durumu oluşturma hatası: %s", mysql_error(conn));
        mysql_rollback(conn);
        mysql_close(conn);
        return;
    }
    
    int affected_rows = mysql_affected_rows(conn);
    
    mysql_commit(conn);
    mysql_autocommit(conn, 1);
    mysql_close(conn);

    log_info("Kullanıcı %lld kanala abone oldu ve %d video için durum oluşturuldu", 
             chat_id, affected_rows);
}

// Yeni video ekleme ve user_video_status tablosunu otomatik güncelleme
int insert_video_and_create_status(const char *channel_id, const char *video_id, const char *title,
                                   const char *published_at, const char *video_url) {
    MYSQL *conn = connect_db();
    char check_query[512];
    snprintf(check_query, sizeof(check_query),
             "SELECT id FROM videos WHERE video_id = '%s';",
             video_id ? video_id : "");
    
    if (mysql_query(conn, check_query)) {
        log_error("Video kontrolü hatası: %s", mysql_error(conn));
        mysql_rollback(conn);
        mysql_close(conn);
        return -1;
    }
    
    MYSQL_RES *res = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(res);
    
    if (row != NULL) {
        int existing_video_id = atoi(row[0]);
        mysql_free_result(res);
        
        char update_query[4096];
        snprintf(update_query, sizeof(update_query),
                 "UPDATE videos SET title='%s', published_at='%s', video_url='%s' "
                 "WHERE video_id='%s';",
                 title ? title : "",
                 published_at ? published_at : "NULL", 
                 video_url ? video_url : "",
                 video_id ? video_id : "");
        
        if (mysql_query(conn, update_query)) {
            log_error("Video güncelleme hatası: %s", mysql_error(conn));
            mysql_rollback(conn);
            mysql_close(conn);
            return -1;
        }
        
        mysql_commit(conn);
        mysql_close(conn);
        log_info("Video güncellendi (ID: %d)", existing_video_id);
        return existing_video_id;
    }
    
    mysql_free_result(res);
    
    // Yeni video ekle
    char insert_query[4096];
    snprintf(insert_query, sizeof(insert_query),
             "INSERT INTO videos (channel_id, video_id, title, published_at, video_url) "
             "VALUES ('%s', '%s', '%s', '%s', '%s');",
             channel_id,
             video_id ? video_id : "",
             title ? "title" : "",
             published_at ? published_at : "NULL",
             video_url ? video_url : "");
    
    if (mysql_query(conn, insert_query)) {
        log_error("Video ekleme hatası: %s", mysql_error(conn));
        mysql_rollback(conn);
        mysql_close(conn);
        return -1;
    }
    
    int new_video_id = (int)mysql_insert_id(conn);
    
    char status_query[1024];
    snprintf(status_query, sizeof(status_query),
             "INSERT INTO user_video_status (user_chat_id, video_id, is_sent) "
             "SELECT uc.user_chat_id, %d, FALSE "
             "FROM user_channels uc "
             "WHERE uc.channel_id = '%s';",
             new_video_id, channel_id);
    
    if (mysql_query(conn, status_query)) {
        log_error("Kullanıcı video durumu ekleme hatası: %s", mysql_error(conn));
        mysql_rollback(conn);
        mysql_close(conn);
        return -1;
    }
    
    int affected_rows = mysql_affected_rows(conn);
    
    mysql_commit(conn);
    mysql_autocommit(conn, 1);
    mysql_close(conn);

    log_info("Yeni video eklendi (ID: %d) ve %d kullanıcı için durum oluşturuldu", 
             new_video_id, affected_rows);

    return new_video_id;
}

// Tüm kullanıcılar için gönderilmemiş videoları listeleme
void get_all_unsent_videos() {
    MYSQL *conn = connect_db();
    const char *query =
        "SELECT "
        "uvs.user_chat_id, u.username, "
        "v.id AS video_id, v.title, v.video_url, v.published_at, "
        "c.name AS channel_name "
        "FROM user_video_status uvs "
        "JOIN users u ON u.chat_id = uvs.user_chat_id "
        "JOIN videos v ON v.id = uvs.video_id "
        "JOIN channels c ON c.channel_id = v.channel_id "
        "WHERE uvs.is_sent = FALSE "
        "ORDER BY uvs.user_chat_id, v.published_at DESC;";

    if (mysql_query(conn, query)) {
        log_error("Tüm gönderilmemiş videolar sorgusu hatası: %s", mysql_error(conn));
        mysql_close(conn);
        return;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        log_error("Sonuç alınamadı: %s", mysql_error(conn));
        mysql_close(conn);
        return;
    }

    MYSQL_ROW row;
    int count = 0;
    long long current_user = -1;
    while ((row = mysql_fetch_row(res))) {
        long long chat_id = atoll(row[0]);
        const char *username = row[1];
        int video_id = atoi(row[2]);
        const char *title = row[3];
        const char *url = row[4];
        const char *published = row[5];
        const char *channel_name = row[6];

        if (current_user != chat_id) {
            if (current_user != -1) 
            current_user = chat_id;
        }

        count++;
    }
    
    if (count == 0) {
        log_info("Hiç gönderilmemiş video yok.");
    } else {
        log_info("Toplam %d gönderilmemiş video bulundu.", count);
    }

    mysql_free_result(res);
    mysql_close(conn);
}

// Kanalları liste yapısında döndürme
ChannelList* get_channels_list() {
    MYSQL *conn = connect_db();
    const char *query = 
        "SELECT channel_id, name, link, created_at "
        "FROM channels "
        "ORDER BY created_at DESC;";

    if (mysql_query(conn, query)) {
        log_error("Kanallar sorgusu hatası: %s", mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        log_error("Sonuç alınamadı: %s", mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }

    int row_count = mysql_num_rows(res);
    
    ChannelList *list = (ChannelList*)malloc(sizeof(ChannelList));
    if (list == NULL) {
        log_error("Bellek tahsisi hatası");
        mysql_free_result(res);
        mysql_close(conn);
        return NULL;
    }

    list->count = row_count;
    list->channels = NULL;

    if (row_count > 0) {
        list->channels = (Channel*)malloc(sizeof(Channel) * row_count);
        if (list->channels == NULL) {
            log_error("Kanal listesi için bellek tahsisi hatası");
            free(list);
            mysql_free_result(res);
            mysql_close(conn);
            return NULL;
        }

        MYSQL_ROW row;
        int i = 0;
        
        while ((row = mysql_fetch_row(res)) && i < row_count) {
            strncpy(list->channels[i].channel_id, row[0] ? row[0] : "", sizeof(list->channels[i].channel_id) - 1);
            list->channels[i].channel_id[sizeof(list->channels[i].channel_id) - 1] = '\0';
            
            strncpy(list->channels[i].name, row[1] ? row[1] : "", sizeof(list->channels[i].name) - 1);
            list->channels[i].name[sizeof(list->channels[i].name) - 1] = '\0';
            
            strncpy(list->channels[i].link, row[2] ? row[2] : "", sizeof(list->channels[i].link) - 1);
            list->channels[i].link[sizeof(list->channels[i].link) - 1] = '\0';
            
            strncpy(list->channels[i].created_at, row[3] ? row[3] : "", sizeof(list->channels[i].created_at) - 1);
            list->channels[i].created_at[sizeof(list->channels[i].created_at) - 1] = '\0';
            
            i++;
        }
    }

    mysql_free_result(res);
    mysql_close(conn);
    log_debug("%d kanal listesi başarıyla oluşturuldu.\n", list->count);
    return list;
}

// Kanal listesi belleğini serbest bırakma
void free_channels_list(ChannelList *list) {
    if (list != NULL) {
        if (list->channels != NULL) {
            free(list->channels);
        }
        free(list);
        log_info("Kanal listesi belleği serbest bırakıldı.");
    }
}

// Kullanıcı-Kanal listesi belleğini serbest bırakma
void free_user_channels_list(UserChannelList *list) {
    if (list != NULL) {
        if (list->user_channels != NULL) {
            free(list->user_channels);
        }
        free(list);
        log_info("Kullanıcı-Kanal listesi belleği serbest bırakıldı.");
    }
}

// Veritabanı ve tabloları oluşturma
void create_database() {
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        log_error("MySQL init hatası");
        exit(1);
    }

    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, NULL, DB_PORT, NULL, 0)) {
        log_error("MySQL bağlantı hatası: %s", mysql_error(conn));
        exit(1);
    }

    execute_query(conn, "CREATE DATABASE IF NOT EXISTS telegram_youtube_bot CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;",
                  "Veritabanı oluşturuldu");

    if (mysql_select_db(conn, DB_NAME)) {
        log_error("Veritabanı seçilemedi: %s", mysql_error(conn));
        exit(1);
    }

    const char *users_table =
        "CREATE TABLE IF NOT EXISTS users ("
        "chat_id BIGINT UNIQUE PRIMARY KEY NOT NULL,"
        "username VARCHAR(255),"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB;";

    const char *channels_table =
        "CREATE TABLE IF NOT EXISTS channels ("
        "channel_id VARCHAR(255) PRIMARY KEY UNIQUE NOT NULL,"
        "name VARCHAR(255),"
        "link TEXT,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB;";

    const char *user_channels_table =
        "CREATE TABLE IF NOT EXISTS user_channels ("
        "user_chat_id BIGINT NOT NULL,"
        "channel_id VARCHAR(255) NOT NULL,"
        "subscribed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE KEY unique_user_channel (user_chat_id, channel_id),"
        "FOREIGN KEY (user_chat_id) REFERENCES users(chat_id) ON DELETE CASCADE,"
        "FOREIGN KEY (channel_id) REFERENCES channels(channel_id) ON DELETE CASCADE"
        ") ENGINE=InnoDB;";

    const char *videos_table =
        "CREATE TABLE IF NOT EXISTS videos ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "channel_id VARCHAR(255) NOT NULL,"
        "video_id VARCHAR(50) UNIQUE NOT NULL,"
        "title TEXT,"
        "published_at TIMESTAMP NULL DEFAULT NULL,"
        "video_url TEXT,"
        "fetched_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (channel_id) REFERENCES channels(channel_id) ON DELETE CASCADE"
        ") ENGINE=InnoDB;";

    const char *user_video_status_table =
        "CREATE TABLE IF NOT EXISTS user_video_status ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "user_chat_id BIGINT NOT NULL,"
        "video_id INT NOT NULL,"
        "is_sent BOOLEAN DEFAULT FALSE,"
        "sent_at TIMESTAMP NULL DEFAULT NULL,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE KEY unique_user_video (user_chat_id, video_id),"
        "FOREIGN KEY (user_chat_id) REFERENCES users(chat_id) ON DELETE CASCADE,"
        "FOREIGN KEY (video_id) REFERENCES videos(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB;";

    execute_query(conn, users_table, "users tablosu oluşturuldu");
    execute_query(conn, channels_table, "channels tablosu oluşturuldu");
    execute_query(conn, user_channels_table, "user_channels tablosu oluşturuldu");
    execute_query(conn, videos_table, "videos tablosu oluşturuldu");
    execute_query(conn, user_video_status_table, "user_video_status tablosu oluşturuldu");

    mysql_close(conn);
    log_info("Veritabanı ve tablolar başarıyla oluşturuldu.");
}

// Video gönderildi olarak işaretleme
void mark_video_as_sent(long long chat_id, int video_id) {
    MYSQL *conn = connect_db();
    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE user_video_status SET is_sent = TRUE, sent_at = NOW() "
             "WHERE user_chat_id = %lld AND video_id = %d;", 
             chat_id, video_id);
    execute_query(conn, query, "Video gönderildi olarak işaretlendi");
    mysql_close(conn);
}

// Gönderilmemiş videoları kullanıcılara gönder ve durumu güncelle
void send_unsent_videos_to_users() {
    MYSQL *conn = connect_db();
    
    const char *query = 
        "SELECT uvs.user_chat_id, uvs.video_id, v.title, v.video_url, v.published_at, "
        "c.name as channel_name "
        "FROM user_video_status uvs "
        "JOIN videos v ON uvs.video_id = v.id "
        "JOIN channels c ON v.channel_id = c.channel_id "
        "WHERE uvs.is_sent = FALSE "
        "ORDER BY uvs.user_chat_id, v.published_at ASC;";

    if (mysql_query(conn, query)) {
        log_error("Gönderilmemiş videolar sorgusu hatası: %s", mysql_error(conn));
        mysql_close(conn);
        return;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        log_error("Sonuç alınamadı: %s", mysql_error(conn));
        mysql_close(conn);
        return;
    }

    MYSQL_ROW row;
    int sent_count = 0;
    int error_count = 0;
    

    while ((row = mysql_fetch_row(res))) {
        long long chat_id = row[0] ? atoll(row[0]) : 0;
        int video_id = row[1] ? atoi(row[1]) : 0;
        const char *title = row[2] ? row[2] : "(başlık yok)";
        const char *video_url = row[3] ? row[3] : "";
        const char *published_at = row[4] ? row[4] : "";
        const char *channel_name = row[5] ? row[5] : "(kanal adı yok)";
        
        // Telegram mesajını hazırla
        char message[1024];
        snprintf(message, sizeof(message),
                "🎥 *Yeni Video!*\n\n"
                "📺 *Kanal:* %s\n"
                "📋 *Başlık:* %s\n"
                "📅 *Yayın Tarihi:* %s\n"
                "🔗 *Link:* %s\n\n"
                "#YeniVideo #YouTube",
                channel_name, title, published_at, video_url);

        // Telegram mesajını gönder
        int send_result = send_message(chat_id, message);
        
        if (send_result == 0) {
            mark_video_as_sent(chat_id, video_id);
            sent_count++;
            log_info("Video ID %d kullanıcı %lld için gönderildi ve işaretlendi.", video_id, chat_id);
        } else {
            error_count++;
            log_error("Video gönderim hatası (Chat: %lld, Video ID: %d)", chat_id, video_id);
        }
        
    }

    if (sent_count > 0) {
        log_info("%d video başarıyla gönderildi ve durumları güncellendi.", sent_count);
    }
    if (error_count > 0) {
        log_error("%d video gönderiminde hata oluştu, durumları değiştirilmedi.", error_count);
    }

    mysql_free_result(res);
    mysql_close(conn);
}
