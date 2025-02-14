#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>


struct Config{
	std::string db_host;
	int db_port;
	std::string db_name;
	std::string db_user;
	std::string db_password;
	std::string start_url;
	int max_depth;
	int server_port;
};

Config parse_config(const std::string& filename) {
	Config config;

	std::ifstream file(filename);
	if (!file.is_open()) {
		throw std::runtime_error("Не удалось открыть конфигурационный файл: " + filename);
	}

	std::string line;
	std::string current_section = "";

	while (std::getline(file, line)) {
		line.erase(0, line.find_first_not_of(" \t"));
		line.erase(line.find_last_not_of(" \t") + 1);

		if (line.empty() || line[0] == '#') {
			continue;
		}

		if (line[0] == '[' && line[line.size() - 1] == ']') {
			current_section = line.substr(1, line.size() - 2);
			continue;
		}
		
		size_t delimiter_pos = line.find('=');
		if (delimiter_pos == std::string::npos) {
			continue;
		}

		std::string key = line.substr(0, delimiter_pos);
		std::string value = line.substr(delimiter_pos + 1);

		key.erase(std::remove(key.begin(), key.end(), ' '), key.end());
		value.erase(std::remove(value.begin(), value.end(), ' '), value.end());

		if (current_section == "database") {
			if (key == "host") config.db_host = value;
			else if (key == "port") config.db_port = std::stoi(value);
			else if (key == "dbname") config.db_name = value;
			else if (key == "user") config.db_user = value;
			else if (key == "password") config.db_password = value;
		}
		else if (current_section == "spider") {
			if (key == "start_url") config.start_url = value;
			else if (key == "max_depth") config.max_depth = std::stoi(value);
		}
		else if(current_section == "server") {
			if (key == "port") config.server_port = std::stoi(value);
		}
	}
	return config;
}

int main() {

	setlocale(LC_ALL, "Russian");

	try {
		Config config = parse_config("config.ini");

		std::cout << "Database Host: " << config.db_host << "\n";
		std::cout << "Database Port: " << config.db_port << "\n";
		std::cout << "Start URL: " << config.start_url << "\n";
		std::cout << "Max Depth: " << config.max_depth << "\n";
		std::cout << "Server Port: " << config.server_port << "\n";
	}
	catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
	return 0;
}