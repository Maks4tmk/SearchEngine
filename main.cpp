#include "search.hpp"
#include "spider.hpp"
#include <iostream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/asio/signal_set.hpp>

namespace property_tree = boost::property_tree;

class session : public std::enable_shared_from_this<session> {
    tcp::socket socket_;
    beast::flat_buffer buffer_{ 8192 };
    http::request<http::dynamic_body> req_;
    pqxx::connection& conn_;
    net::steady_timer timer_;

public:
    session(tcp::socket socket, pqxx::connection& conn, net::io_context& ioc)
        : socket_(std::move(socket)), conn_(conn), timer_(ioc, std::chrono::seconds(30)) {}

    void run() {
        timer_.async_wait(
            [this](beast::error_code ec) {
                if (!ec) {
                    std::cerr << "Таймаут при чтении запроса" << std::endl;
                    return do_close();
                }
            });
        do_read();
    }

private:
    void do_read() {
        auto self = shared_from_this();
        timer_.expires_after(std::chrono::seconds(30));
        timer_.async_wait(
            [this, self](beast::error_code ec) {
                if (!ec) {
                    std::cerr << "Таймаут при чтении запроса" << std::endl;
                    return do_close();
                }
            });

        std::cout << "Начало чтения запроса..." << std::endl;
        http::async_read(socket_, buffer_, req_,
            [this, self](beast::error_code ec, std::size_t bytes_transferred) {
                std::cout << "Завершение чтения запроса..." << std::endl;
                boost::ignore_unused(bytes_transferred);
                timer_.cancel();
                if (!ec) {
                    std::cout << "Запрос успешно прочитан: " << req_.method_string() << " " << req_.target() << std::endl;
                    buffer_.consume(buffer_.size());
                    do_handle_request();
                }
                else if (ec != http::error::end_of_stream) {
                    std::cerr << "Ошибка при чтении запроса: " << ec.message() << std::endl;
                }
                do_close();
            });
    }

    void do_handle_request() {
        auto self = shared_from_this();
        try {
            std::cout << "Начинаем обработку запроса..." << std::endl;
            auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req_.version());
            handle_request(socket_, conn_, req_ , *res);
            std::cout << "Обработка запроса завершена." << std::endl;
            http::async_write(socket_, *res,
                [this, self, res](beast::error_code ec, std::size_t bytes_transferred) {
                    if (ec) {
                        std::cerr << "Ошибка при отправке ответа: " << ec.message() << std::endl;
                    }
                    else {
                        std::cout << "Отправлено байт: " << bytes_transferred << std::endl;
                    }
                    do_close(); // Закрытие соединения после отправки ответа
                });
        }
        catch (const std::exception& e) {
            std::cerr << "Ошибка при обработке запроса: " << e.what() << std::endl;

            auto res = std::make_shared<http::response<http::string_body>>(http::status::internal_server_error, req_.version());
            res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res->set(http::field::content_type, "text/plain");
            res->body() = "Произошла ошибка: " + std::string(e.what());
            res->prepare_payload();
            http::async_write(socket_, *res,
                [this, self, res](beast::error_code ec, std::size_t) {
                    if (ec) {
                        std::cerr << "Ошибка при отправке ответа об ошибке: " << ec.message() << std::endl;
                    }
                    do_close();
                });
        }
    }

    void do_close() {
        beast::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_send, ec);
        if (ec && ec != beast::errc::not_connected) {
            std::cerr << "Ошибка при закрытии соединения: " << ec.message() << std::endl;
        }
    }
};


void do_accept(tcp::acceptor& acceptor, pqxx::connection& conn, net::io_context& ioc) {
    acceptor.async_accept(
        [&acceptor, &conn, &ioc](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<session>(std::move(socket), conn, ioc)->run();
                std::cout << "Принято новое соединение." << std::endl;
            }
            else {
                std::cerr << "Ошибка при принятии соединения: " << ec.message() << std::endl;
            }
            do_accept(acceptor, conn, ioc);
        });
}

void parse_config(const std::string& filename, property_tree::ptree& config) {
    try {
        std::cout << "parse_config" << std::endl;
        property_tree::read_ini(filename, config);
    }
    catch (const property_tree::ptree_error& e) {
        std::cerr << "Ошибка при чтении конфигурационного файла: " << e.what() << std::endl;
        throw;
    }
}



int main() {

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    try {
        property_tree::ptree config;
        parse_config("config.ini", config);

        std::string db_host = config.get<std::string>("database.host");
        int db_port = config.get<int>("database.port");
        std::string db_name = config.get<std::string>("database.dbname");
        std::string db_user = config.get<std::string>("database.user");
        std::string db_password = config.get<std::string>("database.password");

        std::string start_url = config.get<std::string>("spider.start_url");
        int depth = config.get<int>("spider.max_depth");

        start_url = remove_fragment(start_url);

        int server_port = config.get<int>("server.port");

        std::string connection_string = "host=" + db_host + " port=" + std::to_string(db_port) +
            " dbname=" + db_name + " user=" + db_user + " password=" + db_password;

        pqxx::connection conn(connection_string);
        if (conn.is_open()) {std::cout << "Успешное подключение к базе данных: " << conn.dbname() << std::endl;}
        else {std::cerr << "Сбой подключения!" << std::endl; return 1;}

        сreating_tables(conn);

        std::thread spider_thread([&conn, start_url, depth]() {
            try {
                crawl(start_url, depth, conn);
            }
            catch (const std::exception& e) {
                std::cerr << "Ошибка рекурсивного обхода: " << e.what() << std::endl;
            }
            });


        boost::asio::io_context ioc{ 1 };

        tcp::acceptor acceptor{ ioc, {tcp::v4(), static_cast<unsigned short>(server_port)} };
        std::cout << "Сервер слушает на порту " << server_port << std::endl;


        boost::locale::generator gen;
        std::locale loc;
        try {
            loc = gen("ru_RU.UTF-8");
        }
        catch (const std::exception& e) {
            std::cerr << "Ошибка при инициализации локали: " << e.what() << std::endl;
            return 1;
        }
        std::locale::global(loc);


        do_accept(acceptor, conn, ioc);

        net::signal_set signals{ ioc, SIGINT, SIGTERM };
        signals.async_wait([&ioc](beast::error_code, int) {
            ioc.stop();
            });

        ioc.run();

        std::cout << "Отключение от базы данных: " << conn.dbname() << std::endl;
        conn.close();
    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}