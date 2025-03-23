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

        /*������� �� ������� 
        ������� ��� ��������� �������� ������� ������������� �� ��������.
        ���� �������� ����������� ������ �� ����������� ��������� ��������
        ���������� ����� ������ ����� �������� ��� ��� ����������.*/

        std::ostringstream oss;
        oss << R"(
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
        else {
            oss << "<h2>������� ������</h2>";
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

    /*��������� �������� � ������� ������� ���������� ����� ��� �� ��� ���� (��������� ������ ������ ��� ����� ��������� �������������)*/

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

    std::string sql = "SELECT d.url, SUM(dw.frequency) as relevance "
        "FROM documents d "
        "JOIN document_words dw ON d.id = dw.document_id "
        "JOIN words w ON dw.word_id = w.id "
        "WHERE w.word IN (";


    for (size_t i = 0; i < words.size(); ++i) {
        sql += "$" + std::to_string(i + 1);
        if (i != words.size() - 1) sql += ", ";
    }
    sql += ") GROUP BY d.url ORDER BY relevance DESC LIMIT 10;";

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

    for (auto row : results) {
        try {
            if (!row[0].is_null() && !row[1].is_null()) {
                std::string url = row[0].as<std::string>();
                int relevance = row[1].as<int>();
                oss << "<a href=\"" << url << "\" target=\"_blank\">" << url << "</a> (�������������: " << relevance << ")<br>";
            
            } else { std::cerr << "������������ ������ � ������ ����������." << std::endl; }
        }
        catch (const std::exception& e) {
            std::cerr << "������ ��� ��������� ������ ����������: " << e.what() << std::endl;
            return "<h1>��������� ������ ��� ��������� �����������</h1>";
        }
    }

    return oss.str();

}