#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <stdarg.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <unistd.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

static void usage(const char *p)
{
	std::cerr << "Usage: " << p << std::endl <<
	        " -c config_path       - path to config\n";
}


typedef unsigned long long ull;

enum output_type
{
	plain,
	http,
	unknown
};

struct config_params
{
	// type of output format: {plain, http}
	output_type type;
	// minimum size of data in bytes
	size_t min_data_size;
	// maximum size of data in bytes
	size_t max_data_size;
	// rps of read operation
	ull read_rps;
	// rps of write operation
	ull write_rps;
	// prefix of write key
	std::string write_prefix;
	// prefixes of read key
	std::vector<std::string> read_prefix;
	// elliptics groups
	std::string groups;
	// duration of shooting in sec
	ull duration;
	// write hand of proxy to be called (http only)
	std::string proxy_hand;
	// target host (http only)
	std::string host;
	// set Keep-Alive to http header. Othervise Close will be used (http only)
	bool keep_alive;
	// io flags for write (plain only)
	unsigned int write_ioflags;
	// command flags for write (plain only)
	ull write_cflags;
	// io flags for read (plain only)
	unsigned int read_ioflags;
	// command flags for read (plain only)
	ull read_cflags;
};

enum command {
	unknown_command,
	write_command,
	read_command,
	remove_command
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
			      const config_params &params)
{
	static char buffer[1024 * 1024];
	static char user_agent[] = "podnebesnaya";

	static std::string connection = "Keep-Alive";
	if (!params.keep_alive)
		connection = "Close";

	static char add_log[] = "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: %s\r\nContent-Length: %d\r\n\r\n%s\r\n";
	static char add_log_content[] = "user=%u&data=%s&key=%s";
	static char read_hand[] = "/get_user_logs";
	static char read_log[] = "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: %s\r\nContent-Length: %d\r\n\r\n%s\r\n";
	static char read_log_content[] = "user=%u&begin_time=%llu&end_time=%llu";


	static char content[1024 * 1024];
	static int content_size = 0;
	const char *hand = params.proxy_hand.c_str();

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
	                                 params.host.c_str(),
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
	std::ostringstream ss;
    
	ss << static_cast<unsigned long long>(pattern.cflags) << ' '
	   << static_cast<unsigned int>(pattern.ioflags) << ' '
	   << pattern.groups << ' '
	   << key << '\n';

	switch (pattern.cmd) {
		case write_command:
			ss << "write\n" << data;
			break;
		case read_command:
			ss << "read";
			break;
		case remove_command:
			ss << "remove";
			break;
		default:
			fprintf(stderr, "unknown command: %d\n", int(pattern.cmd));
			fflush(stderr);
			abort();
	}
	ss << '\n';

	std::string buffer = std::move(ss.str());

	std::cout << buffer.size() << ' '
		  << static_cast<unsigned long long>(time) << ' '
		  << tag << '\n'
		  << buffer << '\n';
}


bool check(double probabilty)
{
	return rand() < (probabilty * RAND_MAX);
}

std::vector<std::string> parse_prefixes(const char *value)
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

class config
{
public:
	template<typename T>
	T get(const char *key) const
	{
		return m_ptree.get<T>(key);
	}

	bool parse_config(const char *config_path)
	{
		std::ifstream file(config_path);
		if (!file.is_open()) {
			std::cerr << "Config::ParseConfig: couldn't open " << config_path << std::endl;
			return false;
		}
		try {
			boost::property_tree::read_json(file, m_ptree);
		} catch (std::exception &e) {
			std::cerr << "Config::ParseConfig: " << e.what() << std::endl;
			return false;
		}
		return true;
	}

private:
	boost::property_tree::ptree m_ptree;
};

#define GET_CONFIG_PARAM(param_name, param_type) \
	params.param_name = cfg.get<param_type>(#param_name);

int main(int argc, char *argv[]) {
	char ch;
	const char *config_path = nullptr;

	while ((ch = getopt(argc, argv, "c:")) != -1) {
		switch (ch) {
			case 'c': config_path	= optarg;	break;
			case 'h':
			default:
				usage(argv[0]);
				return -1;
		}
	}

	if (!config_path) {
		usage(argv[0]);
		return -1;
	}

	config_params params;
	try {
		config cfg;
		cfg.parse_config( config_path );

		params.type = parse_type( cfg.get<std::string>("output_type").c_str() );
		GET_CONFIG_PARAM(min_data_size, ull);
		GET_CONFIG_PARAM(max_data_size, ull);
		GET_CONFIG_PARAM(read_rps, ull);
	        GET_CONFIG_PARAM(write_rps, ull);
	        GET_CONFIG_PARAM(write_prefix, std::string);
		params.read_prefix = parse_prefixes( cfg.get<std::string>("write_prefix").c_str() );
		GET_CONFIG_PARAM(groups, std::string);
	        GET_CONFIG_PARAM(duration, ull);
	        GET_CONFIG_PARAM(proxy_hand, std::string);
	        GET_CONFIG_PARAM(host, std::string);
	        GET_CONFIG_PARAM(keep_alive, bool);
	        GET_CONFIG_PARAM(write_ioflags, unsigned int);
		GET_CONFIG_PARAM(write_cflags, ull);
		GET_CONFIG_PARAM(read_ioflags, unsigned int);
		GET_CONFIG_PARAM(read_cflags, ull);
	} catch(std::exception &e) {
		std::cerr << e.what() << std::endl;
	}

	srand(time(0));


	bullet_pattern write_bullet = { write_command, params.write_ioflags, params.write_cflags, params.groups.c_str() };
	bullet_pattern read_bullet = { read_command, params.read_ioflags, params.read_cflags, params.groups.c_str() };

	const double read_part = double(params.read_rps) / double(params.read_rps + params.write_rps);

	// количество запросов
	const ull num_read_requests = params.duration * params.read_rps;
	const ull num_write_requests = params.duration * params.write_rps;
	const ull num_requests = num_read_requests + num_write_requests;

	std::string data(params.max_data_size + 1, '\0');
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

	const char *current_prefix = params.write_prefix.c_str();
	const char *current_data = NULL;

	for (ull i = 0; i < num_requests; ++i) {
		// время текущего патрона
		const ull time = (params.duration * 1000) * i / num_requests;

		const bool is_read = params.write_prefix.empty() ? true : check(read_part);

		// id пользователя
		const unsigned user = is_read ? (rand() % num_read_requests) : i;

		if (is_read) {
			current_prefix = params.read_prefix[rand() % params.read_prefix.size()].c_str();
			current_data = "";
		} else {
			// размер данных патрона
			const size_t data_size = params.min_data_size
			+ (rand() % (params.max_data_size - params.min_data_size));

			for (size_t j = 0; j < data_size; ++j) {
				data[j] = alphabet[rand() % alphabet_size];
			}
			data[data_size] = '\0';
			current_data = data.c_str();
		}

		snprintf(key, sizeof(key), "%u.%s", user, current_prefix);

		// выбор патрона
		const bullet_pattern &bullet = is_read ? read_bullet : write_bullet;
		// вывод патрона в лог
		switch(params.type) {
			case http:
				http_print_bullet(bullet, user, time, current_prefix, current_data, params);
			break;
			case plain:
				plain_print_bullet(bullet, time, key, current_data, is_read ? "r_tag" : "w_tag");
			break;
		}
	}
	printf("0\n");
	return 0;
}
