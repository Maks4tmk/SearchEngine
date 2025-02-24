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

        if (req.method() == http::verb::get) {
            res.body() = R"(
                <html>
                    <head>
                        <meta charset="utf-8">
                    </head>
                    <body>
                        <form method="post">
                            <input type="text" name="query" placeholder="Enter search query">
                            <input type="submit" value="Search">
                        </form>
                    </body>
                </html>
            )";
        }
        else if (req.method() == http::verb::post) {
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
                res.body() = "������ ��������� ������";
            }
            else {
                res.body() = search_pages(conn, query);
            }
        }
        else {
            res.result(http::status::bad_request);
            res.body() = "������������ ����� �������";
        }

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
        return "<html><head><meta charset=\"utf-8\"></head><body><h1>������ �� ������������</h1></body></html>";
    }

    // ��������� SQL-������
    std::string sql = "SELECT d.url, SUM(dw.frequency) as relevance "
        "FROM documents d "
        "JOIN document_words dw ON d.id = dw.document_id "
        "JOIN words w ON dw.word_id = w.id "
        "WHERE w.word IN (";

    // ��������� ��������� ��� ������� �����
    for (size_t i = 0; i < words.size(); ++i) {
        sql += "$" + std::to_string(i + 1); // ������ ���������� ���������� � 1
        if (i != words.size() - 1) sql += ", ";
    }
    sql += ") GROUP BY d.url ORDER BY relevance DESC LIMIT 10;";

    std::cout << "������������� SQL-������: " << sql << std::endl;

    pqxx::work txn(conn);

    // �������������� ��������� ��� SQL-�������
    pqxx::params params;
    for (const auto& w : words) {
        params.append(w); // ��������� ������ �����
    }
    pqxx::result results = txn.exec_params(sql, params);


    if (results.empty()) {
        std::cout << "����������� ���������� ������� ���." << std::endl;
    }
    else {
        std::cout << "������� �����������: " << results.size() << std::endl;
        for (auto row : results) {
            std::cout << "URL: " << row[0].c_str() << std::endl;
        }
    }

    // ��������� HTML-�����
    std::ostringstream oss;
    oss << "<html><head><meta charset=\"utf-8\"></head><body>";

    if (results.empty()) {
        oss << "<h1>����������� �� �������</h1>";
    }
    else {
        for (auto row : results) {
            oss << "<a href=\"" << row[0].c_str() << "\">" << row[0].c_str() << "</a><br>";
        }
    }

    oss << "</body></html>";
    return oss.str();

}