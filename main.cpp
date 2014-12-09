#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <stdarg.h>
#include <memory>
#include <vector>
#include <string>
#include <map>

static void usage(char *p)
{
	fprintf(stderr, "Usage: %s\n"
	        " -o output_type       - type of output format: {plain, http}\n"
	        " -w write_prefix      - prefix of write key\n"
	        " -r read_prefix       - prefix of read key\n"
	        " -g group             - elliptics group\n"
	        " -p proxy_hand        - write hand of proxy to be called (http only)\n"
	        " -s min_data_size     - minimum size of data in bytes\n"
	        " -S max_data_size     - maximum size of data in bytes\n"
	        " -R read_rps          - rps of read operation\n"
	        " -W write_rps         - rps of write operation\n"
	        " -d duration          - duration of shooting in ms\n"
	        " -H host              - target host (http only)\n"
	        " -k keep-alive        - set Keep-Alive to http header. Othervise Close will be used (http only)\n"
	        " -i                   - io flags for write (plain only)\n"
	        " -I                   - io flags for read (plain only)\n"
	        " -c                   - command flags for write (plain only)\n"
	        " -C                   - command flags for read (plain only)\n"
	        " -h                   - display this help and exit\n"
	        , p);
}


enum command {
	unknown_command,
	write_command,
	read_command,
	remove_command
};

enum output_type
{
	plain,
	http,
	unknown
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
                              const char *data,
                              const char *host,
                              bool keep_alive,
                              const char *hand)
{
	static char buffer[1024 * 1024];
	static char user_agent[] = "podnebesnaya";

	static std::string connection = "Keep-Alive";
	if (!keep_alive)
		connection = "Close";

	static char add_log[] = "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: %s\r\nContent-Length: %d\r\n\r\n%s\r\n";
	static char add_log_content[] = "user=%u&data=%s&key=%s";
	static char read_hand[] = "/get_user_logs";
	static char read_log[] = "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: %s\r\nContent-Length: %d\r\n\r\n%s\r\n";
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
		hand = read_hand;
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
	                                 hand,
	                                 host,
	                                 user_agent,
	                                 connection.c_str(),
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

	const int buffer_size = snprintf(buffer, sizeof(buffer),
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

inline output_type parse_type(const char *type) {
	if (strcmp(type, "http") == 0)
		return http;
	else if(strcmp(type, "plain") == 0)
		return plain;
	else
		return unknown;
}

int main(int argc, char *argv[]) {
	int ch;
	output_type type = plain;
	const char *date;
	std::vector<std::string> read_dates;
	const char *groups = "1";
	const char *proxy_hand = "add_log";
	const char *host = "s03h.xxx.yandex.net";
	// минимальный размер данных на запись
	size_t min_data_size = 80;
	// максимальный размер данных на запись
	size_t max_data_size = 120;

	ull read_rps = 0;
	ull write_rps = 20000;

	// длительность, миллисекунды
	ull duration = 7 * 60 * 60 * 1000;

	bool keep_alive = false;

	unsigned int write_ioflags = 2 /* DNET_IO_FLAGS_APPEND */;
	unsigned long long write_cflags = 0;
	unsigned int read_ioflags = 0;
	unsigned long long read_cflags = 0;

	while ((ch = getopt(argc, argv, "o:w:r:g:p:s:S:R:W:d:H:ki:I:c:C:h")) != -1) {
		switch (ch) {
			case 'o': {
				type = parse_type(optarg);
				if (type == unknown) {
					usage(argv[0]);
					return -1;
				}
			}
			break;
			case 'w': date			= optarg;					break;
			case 'r': read_dates	= parse_dates(optarg);		break;
			case 'g': groups		= optarg;					break;
			case 'p': proxy_hand	= optarg;					break;
			case 's': min_data_size	= strtol(optarg, NULL, 0);	break;
			case 'S': max_data_size	= strtol(optarg, NULL, 0);	break;
			case 'R': read_rps		= strtol(optarg, NULL, 0);	break;
			case 'W': write_rps		= strtol(optarg, NULL, 0);	break;
			case 'd': duration		= strtol(optarg, NULL, 0);	break;
			case 'H': host			= optarg;					break;
			case 'k': keep_alive	= true;						break;
			case 'i': write_ioflags	= strtol(optarg, NULL, 0);	break;
			case 'I': read_ioflags	= strtol(optarg, NULL, 0);	break;
			case 'c': write_cflags	= strtol(optarg, NULL, 0);	break;
			case 'C': read_cflags	= strtol(optarg, NULL, 0);	break;
			case 'h':
			default:
				usage(argv[0]);
				return -1;
		}
	}

	srand(time(0));


	bullet_pattern write_bullet = { write_command, write_ioflags, write_cflags, groups };
	bullet_pattern read_bullet = { read_command, read_ioflags, read_cflags, groups };

	// процент запросов генерируют активные пользователи
	const double active_requests_part = 0.8;
	// процент активных пользователей
	const double active_users_part = 0.1;
	// процент запросов на чтение
	const double read_part = double(read_rps) / double(read_rps + write_rps);

	// количество запросов
	const ull requests = (duration / 1000) * (read_rps + write_rps);
	// количество пользователей
	const ull users = 30 * 1000000;
	// количество активных пользователей
	const ull active_users = users * active_users_part;

    std::unique_ptr<char[]> data( new char[max_data_size + 1] );
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
		const char *current_data = data.get();

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

		snprintf(key, sizeof(key), "%u.%s", user, current_date);

		// выбор патрона
		const bullet_pattern &bullet = is_read ? read_bullet : write_bullet;
		// вывод патрона в лог
		switch(type) {
			case http:
				http_print_bullet(bullet, user, time, current_date, current_data, host, keep_alive, proxy_hand);
			break;
			case plain:
				plain_print_bullet(bullet, time, key, current_data, is_read ? "r_tag" : "w_tag");
			break;
		}
	}
	printf("0\n");
	return 0;
}
