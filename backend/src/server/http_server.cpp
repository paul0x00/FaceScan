#include "server/http_server.hpp"

#include "api/app.hpp"
#include "config/app_config.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace facescan {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace {

void doSession(tcp::socket socket, std::shared_ptr<App> app)
{
    beast::flat_buffer buffer;
    try {
        for (;;) {
            http::request<http::string_body> req;
            http::read(socket, buffer, req);
            http::response<http::string_body> res = app->handle(req);
            const bool close = res.need_eof();
            http::write(socket, res);
            if (close) {
                break;
            }
        }
    } catch (const std::exception&) {
    }
    beast::error_code ec;
    socket.shutdown(tcp::socket::shutdown_send, ec);
}

} // namespace

int runBackend(int argc, char* argv[])
{
    try {
        const AppConfig config = loadAppConfig();
        const unsigned short port = argc > 1 ? static_cast<unsigned short>(std::atoi(argv[1])) : config.backendPort;
        asio::io_context ioc(1);
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), port));
        std::shared_ptr<App> app(new App(config));
        std::cout << "FaceScan backend listening on http://127.0.0.1:" << port << std::endl;
        for (;;) {
            tcp::socket socket(ioc);
            acceptor.accept(socket);
            std::thread(&doSession, std::move(socket), app).detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "Backend failed: " << e.what() << std::endl;
        return 1;
    }
}

} // namespace facescan
