#include "search.hpp"

void do_close(tcp::socket& socket) {
    beast::error_code ec;
    socket.shutdown(tcp::socket::shutdown_send, ec);
    if (ec && ec != beast::errc::not_connected) {
        std::cerr << "������ ��� �������� ����������: " << ec.message() << std::endl;
    }
}


void handle_request(tcp::socket& socket, pqxx::connection& conn, const http::request<http::dynamic_body>& req, http::response<http::string_body>& res) {
    try {
        if (!socket.is_open()) {
            std::cerr << "����� ��� ������!" << std::endl;
            res.result(http::status::bad_request);
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/plain; charset=utf-8");
            res.body() = "����� ��� ������!";
            res.prepare_payload();
            return;
        }

        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html; charset=utf-8");

        std::ostringstream oss;
        oss << R"(
                <html>
                    <head>
                        <meta charset="utf-8">
                        <title>�����</title>
                        <style>
                            body {
                                font-family: Arial, sans-serif;
                                margin: 20px;
                                background-color: #f4f4f4;
                            }
                            form {
                                margin-bottom: 20px;
                            }
                            input[type="text"] {
                                width: 300px;
                                padding: 8px;
                                margin-right: 10px;
                            }
                            input[type="submit"] {
                                padding: 8px 16px;
                            }
                        </style>
                    </head>
                    <body>
                        <form method="post">
                            <input type="text" name="query" placeholder="������� ��������� ������">
                            <input type="submit" value="�����">
                        </form>
                    </body>
                </html>
            )";


        if (req.method() == http::verb::post) {
            std::string body = beast::buffers_to_string(req.body().data());
            std::cout << "���������� ������: " << body << std::endl;

            std::string query;
            std::string key, value;
            std::istringstream iss(body);
            std::string pair;
            while (std::getline(iss, pair, '&')) {
                size_t pos = pair.find('=');
                if (pos != std::string::npos) {
                    key = pair.substr(0, pos);
                    value = pair.substr(pos + 1);
                    if (key == "query") {
                        std::replace(value.begin(), value.end(), '+', ' ');
                        query = boost::locale::to_lower(value);
                        break;
                    }
                }
            }

            if (query.empty()) {
                std::cerr << "������ ��������� ������" << std::endl;
                res.result(http::status::bad_request);
                oss << "<h1>������ ��������� ������</h1>";
            }
            else {
                oss << search_pages(conn, query);
            }
        }

        oss << "</body></html>";
        res.body() = oss.str();
        res.prepare_payload();
        std::cout << "����� �����������." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "������ � handle_request: " << e.what() << std::endl;
        res.result(http::status::internal_server_error);
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain; charset=utf-8");
        res.body() = "��������� ������: " + std::string(e.what());
        res.prepare_payload();
    }
}




std::string search_pages(pqxx::connection& conn, const std::string& query) {
    std::vector<std::string> words;
    std::istringstream iss(query);
    std::string word;
    while (iss >> word) {
        words.push_back(word);
    }


    //=======================================================
    std::cout << "���������� ������: " << query << std::endl;
    std::cout << "����� �� �������: ";
    for (const auto& w : words) {
        std::cout << "\"" << w << "\" ";
    }
    std::cout << std::endl;
    //=======================================================

    if (words.empty()) {
        return "<h1>������ �� ������������</h1>";
    }

    std::string sql = "SELECT d.url, d.title, SUM(dw.frequency) as relevance "
        "FROM documents d "
        "JOIN document_words dw ON d.id = dw.document_id "
        "JOIN words w ON dw.word_id = w.id "
        "WHERE w.word IN (";


    for (size_t i = 0; i < words.size(); ++i) {
        sql += "$" + std::to_string(i + 1);
        if (i != words.size() - 1) sql += ", ";
    }
    sql += ") GROUP BY d.url, d.title ORDER BY relevance DESC;";

    std::cout << "������������� SQL-������: " << sql << std::endl;

    pqxx::work txn(conn);

    pqxx::params params;
    for (const auto& w : words) {
        params.append(w);
    }
    pqxx::result results = txn.exec_params(sql, params);


    std::ostringstream oss;


    if (results.empty()) {
        std::cout << "����������� ���������� ������� ���." << std::endl;
        return "<h1>����������� ������ �� �������</h1>";
    }
    else {
        std::cout << "������� �����������: " << results.size() << std::endl;
        for (auto row : results) {
            std::cout << "URL: " << row[0].c_str() << std::endl;
        }
    }

    {
        oss << R"(
        <style>
            body {
                font-family: Arial, sans-serif;
                margin: 20px;
                background-color: #f4f4f4;
            }
            .search-result {
                margin-bottom: 15px;
                padding: 15px;
                border: 1px solid #ddd;
                border-radius: 5px;
                background-color: #fff;
                box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
            }
            .search-header {
                display: flex;
                justify-content: space-between;
                align-items: center;
            }
            .search-title {
                font-size: 1.1em;
                margin: 0;
            }
            .search-title a {
                color: #007BFF;
                text-decoration: none;
            }
            .search-title a:hover {
                text-decoration: underline;
            }
            .search-relevance {
                font-size: 0.9em;
                color: #555;
                margin-left: 10px;
            }
            .search-url {
                font-size: 0.7em;
                color: #007BFF;
                text-decoration: none;
                margin-top: 5px;
                display: block;
            }
        </style>
    )";
    }

    for (auto row : results) {
        try {
            if (!row[0].is_null() && !row[1].is_null() && !row[2].is_null()) {
                std::string url = row[0].as<std::string>();
                std::string title = row[1].as<std::string>();
                int relevance = row[2].as<int>();

                oss << R"(<div class="search-result">)";
                oss << R"(<div class="search-header">)";
                oss << R"(<h2 class="search-title"><a href=")" << url << R"(" target="_blank" class="search-title">)" << title << R"(</a></h2>)";
                oss << R"(<span class="search-relevance">�������������: )" << relevance << R"(</span>)";
                oss << R"(</div>)";
                oss << R"(<a href=")" << url << R"(" target="_blank" class="search-url">)" << url << R"(</a>)";
                oss << R"(</div>)";

            } 
            else { std::cerr << "������������ ������ � ������ ����������." << std::endl; }
        }
        catch (const std::exception& e) {
            std::cerr << "������ ��� ��������� ������ ����������: " << e.what() << std::endl;
            return "<h1>��������� ������ ��� ��������� �����������</h1>";
        }
    }

    return oss.str();

}