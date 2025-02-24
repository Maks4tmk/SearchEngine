#ifndef SEARCH_HPP
#define SEARCH_HPP

#include <iostream>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <pqxx/pqxx>
#include <atomic>

#pragma execution_character_set("utf-8")

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

void do_close(tcp::socket& socket);
void handle_request(tcp::socket& socket, pqxx::connection& conn, const http::request<http::dynamic_body>& req, http::response<http::string_body>& res);
std::string search_pages(pqxx::connection& conn, const std::string& query);

#endif