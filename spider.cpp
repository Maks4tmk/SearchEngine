#include "spider.hpp"

void сreating_tables(pqxx::connection& conn) {
    
    pqxx::work txn(conn);
    txn.exec("CREATE TABLE IF NOT EXISTS documents (id SERIAL PRIMARY KEY, url TEXT UNIQUE)");
    txn.exec("CREATE TABLE IF NOT EXISTS words (id SERIAL PRIMARY KEY, word TEXT UNIQUE)");
    txn.exec("CREATE TABLE IF NOT EXISTS document_words ("
        "document_id INTEGER REFERENCES documents(id) ON DELETE CASCADE,"
        "word_id INTEGER REFERENCES words(id) ON DELETE CASCADE,"
        "frequency INTEGER,"
        "PRIMARY KEY (document_id, word_id))");

    txn.exec("TRUNCATE TABLE document_words RESTART IDENTITY CASCADE");
    txn.exec("TRUNCATE TABLE words RESTART IDENTITY CASCADE");
    txn.exec("TRUNCATE TABLE documents RESTART IDENTITY CASCADE");

    conn.prepare("insert_document", "INSERT INTO documents (url) VALUES ($1) ON CONFLICT (url) DO NOTHING");
    conn.prepare("insert_word", "INSERT INTO words (word) VALUES ($1) ON CONFLICT (word) DO NOTHING");
    conn.prepare("get_word_id", "SELECT id FROM words WHERE word=$1");
    conn.prepare("insert_document_word", "INSERT INTO document_words (document_id, word_id, frequency) VALUES ($1, $2, $3)");

    txn.commit();
}

std::string download_page(net::io_context& ioc, const std::string& url) {
    try {
        std::cout << "Загрузка страницы: " << url << std::endl;
        urls::result<urls::url_view> parsed = urls::parse_uri(url);
        if (!parsed) {
            throw std::runtime_error("Неверный URL-адрес");
        }

        std::string host = parsed->host();
        std::string target = parsed->path();
        if (target.empty()) target = "/";
        unsigned short port = parsed->has_port() ? std::stoi(parsed->port()) : 80;
        tcp::resolver resolver{ ioc };
        auto results = resolver.resolve(host, std::to_string(port));
        tcp::socket socket{ ioc };
        net::connect(socket, results.begin(), results.end());
        http::request<http::string_body> req{ http::verb::get, target, 11 };
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        http::write(socket, req);
        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(socket, buffer, res);
        return beast::buffers_to_string(res.body().data());
    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка при загрузке страницы: " << e.what() << std::endl;
        return "";
    }
}



void index_page(const std::string& content, const std::string& url, pqxx::connection& conn) {
    try {
        std::cout << "Индексация страницы: " << url << std::endl;

        std::string cleaned_text = content;
        cleaned_text = std::regex_replace(cleaned_text, std::regex("<[^>]*>"), "");
        
        boost::locale::generator gen;
        std::locale loc = gen("ru_RU.UTF-8");
        std::locale::global(loc);

        cleaned_text.erase(std::remove_if(cleaned_text.begin(), cleaned_text.end(), 
            [](char c){
                return !std::isalpha(static_cast<unsigned char>(c)) && !std::isspace(static_cast<unsigned char>(c));
            }), cleaned_text.end());

        cleaned_text = boost::locale::to_lower(cleaned_text, loc);

        std::istringstream iss(cleaned_text);
        std::map<std::string, int> word_count;
        std::string word;
        while (iss >> word) {
            if (word.length() >= 3 && word.length() <= 32) {
                ++word_count[word];
            }
        }

        pqxx::work txn(conn);
        txn.exec_prepared("insert_document", url);
        pqxx::result doc_result = txn.exec_params("SELECT id FROM documents WHERE url=$1", url);
        if (doc_result.size() == 0) {
            throw std::runtime_error("Не удалось найти ID документа.");
        }
        int doc_id = doc_result[0][0].as<int>();

        for (const auto& [w, count] : word_count) {
            txn.exec_prepared("insert_word", w);
            pqxx::result word_result = txn.exec_prepared("get_word_id", w);
            if (word_result.size() == 0) {
                throw std::runtime_error("Не удалось найти ID слова.");
            }
            int word_id = word_result[0][0].as<int>();

            txn.exec_prepared("insert_document_word", doc_id, word_id, count);
        }
        txn.commit();
    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка при индексации страницы: " << e.what() << std::endl;
        throw;
    }
}



void crawl(const std::string& start_url, int depth, pqxx::connection& conn) {
    try {
        std::cout << "Запуск рекурсивного обхода!" << std::endl;
        std::queue<std::pair<std::string, int>> q;
        q.push({ start_url, 0 });
        std::unordered_set<std::string> visited;
        boost::asio::io_context ioc;
        while (!q.empty()) {
            auto [url, current_depth] = q.front();
            q.pop();
            if (visited.count(url) || current_depth > depth) continue;
            visited.insert(url);
            std::string content = download_page(ioc, url);
            if (content.empty()) continue;
            index_page(content, url, conn);
            std::regex link_regex(R"(<a\s+(?:[^>]*?\s+)?href=["'](.*?)["'])");
            auto words_begin = std::sregex_iterator(content.begin(), content.end(), link_regex);
            auto words_end = std::sregex_iterator();
            for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
                std::smatch match = *i;
                std::string link = match.str(1);
                if (link.find("http") != 0) {
                    urls::result<urls::url_view> parsed = urls::parse_uri(url);
                    if (parsed) {
                        std::string base_host = parsed->host();
                        link = "http://" + base_host + link;
                    }
                }
                q.push({ link, current_depth + 1 });
            }
        }
        std::cout << "Рекурсивный обход завершен!" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка рекурсивном обходе: " << e.what() << std::endl;
        throw;
    }
}

