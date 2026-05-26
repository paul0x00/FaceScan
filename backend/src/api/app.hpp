#pragma once

#include "config/app_config.hpp"

#include <boost/beast/http.hpp>

#include <memory>

namespace facescan {

class App {
public:
    explicit App(const AppConfig& config);
    ~App();

    boost::beast::http::response<boost::beast::http::string_body> handle(
        const boost::beast::http::request<boost::beast::http::string_body>& req);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace facescan
