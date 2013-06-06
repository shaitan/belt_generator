\
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <stdarg.h>
#include <vector>
#include <string>
#include <map>


enum command {
    unknown_command,
    write_command,
    read_command,
    remove_command
};

enum output_type
{
    plain,
    http
};

struct bullet_pattern
{
    command cmd;
    unsigned int ioflags;
    unsigned long long cflags;
    const char *groups;
};

inline void http_print_bullet(const bullet_pattern &pattern,
                              const unsigned user,
                              unsigned long long time,
                              const char *key,
                              const char *data)
{
    static char buffer[1024 * 1024];
    static char host[] = "s03h.xxx.yandex.net";
    static char user_agent[] = "podnebesnaya";

    static char add_log[] = "POST /add_log HTTP/1.1\nHost: %s\nUser-Agent: %s\nConnection: Close\nContent-Length: %d\n\n%s\n";
    static char add_log_content[] = "user=%u&data=%s&key=%s";
    static char read_log[] = "POST /get_user_logs HTTP/1.1\nHost: %s\nUser-Agent: %s\nConnection: Close\nContent-Length: %d\n\n%s\n";
    static char read_log_content[] = "user=%u&begin_time=%llu&end_time=%llu";


    static char content[1024 * 1024];
    static int content_size = 0;

    const char *cmd;
    switch (pattern.cmd) {
    case write_command:
        cmd = add_log;
        content_size = snprintf(content, sizeof(content),
                                add_log_content,
                                user,
                                data,
                                key);
        break;
    case read_command:
        cmd = read_log;
        content_size = snprintf(content, sizeof(content),
                                read_log_content,
                                user,
                                time,
                                time);
        break;
    default:
        fprintf(stderr, "can't sprintf: %d\n", int(pattern.cmd));
        fflush(stderr);
        abort();
    }

    const int buffer_size = snprintf(buffer, sizeof(buffer),
                                     cmd,
                                     host,
                                     user_agent,
                                     content_size,
                                     content
        );

    printf("%d %llu\n%s\n",
           buffer_size,
           time,
           buffer);
}

inline void plain_print_bullet(const bullet_pattern &pattern,
                               unsigned long long time,
                               const char *key,
                               const char *data,
                               const char *tag = "")
{
    static char buffer[1024 * 1024];

    static const char write[] = "%llu %u %s %s\nwrite\n%s\n";
    static const char read[] = "%llu %u %s %s\nread\n";
    static const char remove[] = "%llu %u %s %s\nremove\n";

    const char *cmd;
    switch (pattern.cmd) {
    case write_command:
        cmd = write;
        break;
    case read_command:
        cmd = read;
        break;
    case remove_command:
        cmd = remove;
        break;
    default:
        fprintf(stderr, "unknown command: %d\n", int(pattern.cmd));
        fflush(stderr);
        abort();
    }

    const int buffer_size = snprintf(
                buffer, sizeof(buffer),
                cmd,
                static_cast<unsigned long long>(pattern.cflags),
                static_cast<unsigned int>(pattern.ioflags),
                pattern.groups,
                key,
                data);
    if (buffer_size < 0) {
        fprintf(stderr, "can't sprintf: %d\n", buffer_size);
        fflush(stderr);
        abort();
    }

    printf("%d %llu %s\n%s\n",
           buffer_size,
           static_cast<unsigned long long>(time),
           tag,
           buffer);
}


typedef unsigned long long ull;

bool check(double probabilty)
{
    return rand() < (probabilty * RAND_MAX);
}

std::vector<std::string> parse_dates(char *value)
{
    std::vector<std::string> result;
    bool finished = false;
    while (!finished && value && *value) {
        char *delimiter = const_cast<char *>(strchrnul(value, ':'));
        finished = !*delimiter;
        *delimiter = '\0';
        if (delimiter - value > 0)
            result.push_back(value);
        value = delimiter + 1;
    }
    return result;
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s [write_date_suffix] [read_date_suffixes] [output type]\n", argv[0]);
        return 1;
    }

    output_type type = plain;
    if(strcmp(argv[3], "http") == 0)
        type = http;
    else if(strcmp(argv[3], "plain") == 0)
        type = plain;
    else {
        printf("Unknown output type: %s\n", argv[3]);
        return 1;
    }

    srand(time(0));

    const char *date = argv[1];
    const std::vector<std::string> read_dates = parse_dates(argv[2]);
    const char *groups = "1";

    bullet_pattern write_bullet = { write_command, 2 /* DNET_IO_FLAGS_APPEND */, 0, groups };
    bullet_pattern read_bullet = { read_command, 0, 0, groups };

    const ull read_rps = 0;
    const ull write_rps = 20000;

    // процент запросов генерируют активные пользователи
    const double active_requests_part = 0.8;
    // процент активных пользователей
    const double active_users_part = 0.1;
    // процент запросов на чтение
    const double read_part = double(read_rps) / double(read_rps + write_rps);

    // длительность, миллисекунды
    const ull duration = 7 * 60 * 60 * 1000;
    // количество запросов
    const ull requests = (duration / 1000) * (read_rps + write_rps);
    // количество пользователей
    const ull users = 30 * 1000000;
    // количество активных пользователей
    const ull active_users = users * active_users_part;
    // минимальный размер данных на запись
    const size_t min_data_size = 80;
    // максимальный размер данных на запись
    const size_t max_data_size = 120;

    char data[1024];
    char key[1024];
    char alphabet[10 + 26 * 2];
    const size_t alphabet_size = sizeof(alphabet);

    for (int i = 0; i < 10; ++i) {
        alphabet[i] = '0' + i;
    }
    for (int i = 0; i < 26; ++i) {
        alphabet[i + 10] = 'a' + i;
        alphabet[i + 10 + 26] = 'A' + i;
    }

    for (ull i = 0; i < requests; ++i) {
        // время текущего патрона
        const ull time = duration * i / requests;

        // id пользователя
        const unsigned user = check(active_requests_part)
                ? (rand() % active_users)
                : ((rand() % (users - active_users)) + active_users);

        const char *current_date = date;
        const char *current_data = data;

        const bool is_read = check(read_part);

        if (is_read) {
            current_date = read_dates[rand() % read_dates.size()].c_str();
            current_data = "";
        } else {
            // размер данных патрона
            const size_t data_size = min_data_size
                    + (rand() % (max_data_size - min_data_size));

            for (size_t j = 0; j < data_size; ++j) {
                data[j] = alphabet[rand() % alphabet_size];
            }
            data[data_size] = 0;
        }

            snprintf(key, sizeof(key), "%u_%s", user, current_date);

            // выбор патрона
            const bullet_pattern &bullet = is_read ? read_bullet : write_bullet;
            // вывод патрона в лог
            switch(type) {
            case http:  http_print_bullet(bullet, user, time, current_date, current_data);                  break;
            case plain: plain_print_bullet(bullet, time, key, current_data, is_read ? "r_tag" : "w_tag");   break;
            }
    }
    printf("0\n");
    return 0;
}
