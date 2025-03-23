#include "spider.hpp"

void сreating_tables(pqxx::connection& conn) {
    
    pqxx::work txn(conn);
    txn.exec("DROP TABLE IF EXISTS document_words CASCADE");
    txn.exec("DROP TABLE IF EXISTS words CASCADE");
    txn.exec("DROP TABLE IF EXISTS documents CASCADE");

    txn.exec("CREATE TABLE IF NOT EXISTS documents (id SERIAL PRIMARY KEY, url TEXT UNIQUE, title TEXT)");
    txn.exec("CREATE TABLE IF NOT EXISTS words (id SERIAL PRIMARY KEY, word TEXT UNIQUE)");
    txn.exec("CREATE TABLE IF NOT EXISTS document_words ("
        "document_id INTEGER REFERENCES documents(id) ON DELETE CASCADE,"
        "word_id INTEGER REFERENCES words(id) ON DELETE CASCADE,"
        "frequency INTEGER,"
        "PRIMARY KEY (document_id, word_id))");

    conn.prepare("insert_document", "INSERT INTO documents (url, title) VALUES ($1, $2) ON CONFLICT (url) DO UPDATE SET title=EXCLUDED.title");
    conn.prepare("insert_word", "INSERT INTO words (word) VALUES ($1) ON CONFLICT (word) DO NOTHING");
    conn.prepare("get_word_id", "SELECT id FROM words WHERE word=$1");
    conn.prepare("insert_document_word", "INSERT INTO document_words (document_id, word_id, frequency) VALUES ($1, $2, $3)");

    txn.commit();
}


std::string remove_fragment(const std::string& url) {
    size_t hash_pos = url.find('#');
    if (hash_pos != std::string::npos) {
        return url.substr(0, hash_pos);
    }
    return url;
}


template<typename Stream>
std::string handle_http_request(Stream& stream, net::io_context& ioc, const std::string& host, const std::string& target, const std::string& scheme) {
    http::request<http::string_body> req{ http::verb::get, target, 11 };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    beast::flat_buffer buffer;
    http::response<http::dynamic_body> res;

    http::write(stream, req);
    http::read(stream, buffer, res);

    if (res.result() == http::status::moved_permanently ||
        res.result() == http::status::found ||
        res.result() == http::status::see_other ||
        res.result() == http::status::temporary_redirect ||
        res.result() == http::status::permanent_redirect) {

        auto location = res.base()[http::field::location];
        if (!location.empty()) {
            std::string new_url = std::string(location);

            if (new_url[0] == '/') {
                urls::result<urls::url_view> parsed = urls::parse_uri(scheme + "://" + host);
                if (parsed) {
                    std::string base_scheme = parsed->scheme();
                    std::string base_host = parsed->host();
                    new_url = base_scheme + "://" + base_host + new_url;
                }
            }
            std::cout << "Переход по редиректу на: " << new_url << std::endl;
            return download_page(ioc, new_url);
        }
    }
    return beast::buffers_to_string(res.body().data());
}


std::string download_page(net::io_context& ioc, const std::string& url) {
    try {
        std::cout << "Загрузка страницы: " << url << std::endl;
        urls::result<urls::url_view> parsed = urls::parse_uri(url);
        if (!parsed) {
            throw std::runtime_error("Неверный URL-адрес");
        }

        std::string scheme = parsed->scheme();
        std::string host = parsed->host();
        std::string target = parsed->path();
        if (target.empty()) target = "/";

        unsigned short port = parsed->has_port() ? std::stoi(parsed->port()) : (scheme == "https" ? 443 : 80);

        tcp::resolver resolver{ ioc };
        auto results = resolver.resolve(host, std::to_string(port));

        if (scheme == "https") {
            ssl::context ctx{ ssl::context::tlsv12_client };

            ctx.load_verify_file("certs/cacert.pem");


            ctx.set_options(
                ssl::context::default_workarounds |
                ssl::context::no_sslv2 |
                ssl::context::no_sslv3 |
                ssl::context::no_tlsv1 |
                ssl::context::no_tlsv1_1 |
                ssl::context::single_dh_use
            );

            ssl::stream<tcp::socket> socket{ ioc, ctx };
            net::connect(socket.lowest_layer(), results.begin(), results.end());

            SSL_set_tlsext_host_name(socket.native_handle(), host.c_str());

            try {
                socket.handshake(ssl::stream_base::client);
            }
            catch (const boost::system::system_error& e) {
                std::cerr << "Ошибка handshake: " << e.what() << std::endl;
                throw;
            }

            return handle_http_request(socket, ioc, host, target, scheme);

        } else {
            tcp::socket socket{ ioc };
            net::connect(socket, results.begin(), results.end());
            return handle_http_request(socket, ioc, host, target, scheme);

        }         
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

        std::string title;
        std::smatch match;
        std::regex title_regex(R"(<title>(.*?)<\/title>)");
        if (std::regex_search(content, match, title_regex) && match.size() > 1) {
            title = match.str(1);
            title = boost::locale::to_lower(title, loc);
        }

        pqxx::work txn(conn);
        txn.exec_prepared("insert_document", url, title);
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
        net::io_context ioc;

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

                link = remove_fragment(link);

                if (link.empty() || link[0] == '#') {
                    continue;
                }

                if (!link.empty() && link[0] == '/') {
                    urls::result<urls::url_view> parsed = urls::parse_uri(url);
                    if (parsed) {
                        std::string base_scheme = parsed->scheme();
                        std::string base_host = parsed->host();
                        link = base_scheme + "://" + base_host + link;
                    }
                }

                if (!link.empty() && link.find("http") == 0 || link.find("https") == 0) {
                    q.push({ link, current_depth + 1 });
                }
            }
        }
        std::cout << "Рекурсивный обход завершен!" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка рекурсивном обходе: " << e.what() << std::endl;
        throw;
    }
}

