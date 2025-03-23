#ifndef SPIDER_HPP
#define SPIDER_HPP

#include <iostream>
#include <string>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/locale.hpp>
#include <boost/url.hpp>
#include <pqxx/pqxx>
#include <regex>
#include <sstream>
#include <map>
#include <algorithm>
#include <cctype>
#include <queue>
#include <unordered_set>

#pragma execution_character_set("utf-8")

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace urls = boost::urls;
using tcp = net::ip::tcp;

void ñreating_tables(pqxx::connection& conn);
std::string remove_fragment(const std::string& url);

template <typename Stream>
std::string handle_http_request(Stream& stream, net::io_context& ioc, const std::string& host, const std::string& target, const std::string& scheme);

std::string download_page(net::io_context& ioc, const std::string& url);
void index_page(const std::string& content, const std::string& url, pqxx::connection& conn);
void crawl(const std::string& start_url, int depth, pqxx::connection& conn);

#endif