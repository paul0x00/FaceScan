#include "camera/camera_manager.hpp"
#include "common/file_utils.hpp"
#include "common/json_utils.hpp"
#include "common/models.hpp"
#include "common/time_utils.hpp"
#include "config/app_config.hpp"
#include "database/database.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace facescan {

namespace {

std::string toString(int value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

std::string patientJson(const Patient& p)
{
    std::ostringstream os;
    os << "{"
       << jsonPair("id", p.id) << ","
       << jsonPair("patientNo", p.patientNo) << ","
       << jsonPair("orderNo", p.orderNo) << ","
       << jsonPair("name", p.name) << ","
       << jsonPair("gender", p.gender) << ","
       << jsonPair("age", p.age) << ","
       << jsonPair("phone", p.phone) << ","
       << jsonPair("doctor", p.doctor) << ","
       << jsonPair("remark", p.remark) << ","
       << jsonPair("createdAt", p.createdAt)
       << "}";
    return os.str();
}

std::string orderJson(const Order& order)
{
    std::ostringstream os;
    os << "{"
       << jsonPair("id", order.id) << ","
       << jsonPair("patientId", order.patientId) << ","
       << jsonPair("orderNo", order.orderNo) << ","
       << jsonPair("status", order.status) << ","
       << jsonPair("createdAt", order.createdAt) << ","
       << jsonPair("scanCount", order.scanCount)
       << "}";
    return os.str();
}

Patient patientFromBody(const std::string& body)
{
    Patient p;
    p.id = 0;
    p.patientNo = "";
    p.orderNo = "";
    p.name = jsonStringValue(body, "name");
    p.gender = jsonStringValue(body, "gender");
    p.age = jsonIntValue(body, "age");
    p.phone = jsonStringValue(body, "phone");
    p.doctor = jsonStringValue(body, "doctor");
    p.remark = jsonStringValue(body, "remark");
    p.createdAt = nowText();
    if (p.name.empty()) {
        p.name = "未命名患者";
    }
    return p;
}

class App {
public:
    explicit App(const AppConfig& config)
        : database_(config.databasePath),
          camera_(config.imageRoot),
          imageRoot_(CameraManager::normalizeRoot(config.imageRoot))
    {
    }

    http::response<http::string_body> handle(const http::request<http::string_body>& req)
    {
        const std::string target(req.target().data(), req.target().size());
        const std::string path = pathOnly(target);
        try {
            if (req.method() == http::verb::options) {
                return textResponse(req, http::status::no_content, "");
            }
            if (req.method() == http::verb::post && path == "/api/login") {
                return jsonResponse(req, "{\"token\":\"dev-token\",\"user\":{\"name\":\"管理员\"}}");
            }
            if (req.method() == http::verb::get && path == "/api/health") {
                return jsonResponse(req, "{\"status\":\"ok\"}");
            }
            if (req.method() == http::verb::get && path == "/api/patients") {
                return listPatients(req, target);
            }
            if (req.method() == http::verb::post && path == "/api/patients") {
                Patient p = patientFromBody(req.body());
                const int id = database_.createPatient(p);
                Patient created = database_.patientById(id);
                const int orderId = database_.latestOrderId(id);
                return jsonResponse(req, "{\"id\":" + toString(id)
                    + ",\"orderId\":" + toString(orderId)
                    + ",\"patientNo\":\"" + escapeJson(created.patientNo)
                    + "\",\"orderNo\":\"" + escapeJson(created.orderNo) + "\"}");
            }
            if (req.method() == http::verb::put && path.find("/api/patients/") == 0) {
                const int id = std::atoi(path.substr(std::string("/api/patients/").size()).c_str());
                const bool ok = database_.updatePatient(id, patientFromBody(req.body()));
                return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
            }
            if (req.method() == http::verb::delete_ && path.find("/api/patients/") == 0) {
                const int id = std::atoi(path.substr(std::string("/api/patients/").size()).c_str());
                const bool ok = database_.deletePatient(id);
                return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
            }
            if (req.method() == http::verb::get && path.find("/api/patients/") == 0 && path.find("/orders") != std::string::npos) {
                const std::string prefix = "/api/patients/";
                const std::size_t end = path.find("/orders");
                const int patientId = std::atoi(path.substr(prefix.size(), end - prefix.size()).c_str());
                return listOrders(req, patientId);
            }
            if (req.method() == http::verb::get && path.find("/api/patients/") == 0 && path.find("/scans") != std::string::npos) {
                const std::string prefix = "/api/patients/";
                const std::size_t end = path.find("/scans");
                const int patientId = std::atoi(path.substr(prefix.size(), end - prefix.size()).c_str());
                return listScans(req, patientId);
            }
            if (req.method() == http::verb::post && path == "/api/orders") {
                const int patientId = jsonIntValue(req.body(), "patientId");
                const int orderId = database_.createOrder(patientId);
                return jsonResponse(req, "{\"id\":" + toString(orderId) + ",\"orderNo\":\"" + escapeJson(database_.orderNo(orderId)) + "\"}");
            }
            if (req.method() == http::verb::delete_ && path.find("/api/orders/") == 0) {
                const int id = std::atoi(path.substr(std::string("/api/orders/").size()).c_str());
                const bool ok = database_.deleteOrder(id);
                return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
            }
            if (req.method() == http::verb::post && path.find("/api/orders/") == 0 && path.find("/open-folder") != std::string::npos) {
                const std::string prefix = "/api/orders/";
                const std::size_t end = path.find("/open-folder");
                const int orderId = std::atoi(path.substr(prefix.size(), end - prefix.size()).c_str());
                return openOrderFolder(req, orderId);
            }
            if (req.method() == http::verb::post && path == "/api/camera/start") {
                camera_.start();
                return jsonResponse(req, "{\"streaming\":true}");
            }
            if (req.method() == http::verb::post && path == "/api/camera/stop") {
                camera_.stop();
                return jsonResponse(req, "{\"streaming\":false}");
            }
            if (req.method() == http::verb::get && path == "/api/camera/frame") {
                std::map<std::string, std::string> query = parseQuery(target);
                const std::string view = query.count("view") ? query["view"] : "front";
                return svgResponse(req, camera_.frameSvg(view));
            }
            if (req.method() == http::verb::post && path == "/api/camera/capture") {
                return capture(req);
            }
            if (req.method() == http::verb::post && path == "/api/export/package") {
                return jsonResponse(req, "{\"ok\":true,\"message\":\"数据已完成本地压缩打包（MVP占位）\"}");
            }
            if (req.method() == http::verb::post && path == "/api/export/upload") {
                return jsonResponse(req, "{\"ok\":true,\"message\":\"云端上传接口已预留，第一阶段不启用真实云同步\"}");
            }
            return jsonResponse(req, "{\"error\":\"not found\"}", http::status::not_found);
        } catch (const std::exception& e) {
            return jsonResponse(req, "{\"error\":\"" + escapeJson(e.what()) + "\"}", http::status::internal_server_error);
        }
    }

private:
    Database database_;
    CameraManager camera_;
    std::string imageRoot_;

    http::response<http::string_body> textResponse(
        const http::request<http::string_body>& req,
        http::status status,
        const std::string& body,
        const std::string& contentType = "text/plain; charset=utf-8")
    {
        http::response<http::string_body> res(status, req.version());
        res.set(http::field::server, "FaceScan Backend");
        res.set(http::field::content_type, contentType);
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
        res.set(http::field::access_control_allow_methods, "GET, POST, PUT, DELETE, OPTIONS");
        res.keep_alive(req.keep_alive());
        res.body() = body;
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> jsonResponse(
        const http::request<http::string_body>& req,
        const std::string& body,
        http::status status = http::status::ok)
    {
        return textResponse(req, status, body, "application/json; charset=utf-8");
    }

    http::response<http::string_body> svgResponse(const http::request<http::string_body>& req, const std::string& body)
    {
        return textResponse(req, http::status::ok, body, "image/svg+xml; charset=utf-8");
    }

    http::response<http::string_body> listPatients(const http::request<http::string_body>& req, const std::string& target)
    {
        std::map<std::string, std::string> query = parseQuery(target);
        const std::string keyword = query.count("keyword") ? query["keyword"] : "";
        const std::string date = query.count("date") ? query["date"] : "";
        const std::vector<Patient> list = database_.patients(keyword, date);
        std::ostringstream os;
        os << "{\"items\":[";
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (i) os << ",";
            os << patientJson(list[i]);
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }

    http::response<http::string_body> listScans(const http::request<http::string_body>& req, int patientId)
    {
        const std::vector<ScanResult> scans = database_.scansForPatient(patientId);
        std::ostringstream os;
        os << "{\"items\":[";
        for (std::size_t i = 0; i < scans.size(); ++i) {
            if (i) os << ",";
            os << "{"
               << jsonPair("id", scans[i].id) << ","
               << jsonPair("orderId", scans[i].orderId) << ","
               << jsonPair("previewPath", scans[i].previewPath) << ","
               << jsonPair("createdAt", scans[i].createdAt)
               << "}";
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }

    http::response<http::string_body> listOrders(const http::request<http::string_body>& req, int patientId)
    {
        const std::vector<Order> orders = database_.ordersForPatient(patientId);
        std::ostringstream os;
        os << "{\"items\":[";
        for (std::size_t i = 0; i < orders.size(); ++i) {
            if (i) os << ",";
            os << orderJson(orders[i]);
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }

    http::response<http::string_body> openOrderFolder(const http::request<http::string_body>& req, int orderId)
    {
        std::string folder = parentDirectory(database_.latestPreviewPathForOrder(orderId));
        if (folder.empty()) {
            folder = imageRoot_;
        }
        ensureDirectory(folder);
        const bool ok = openDirectory(folder);
        return jsonResponse(req, std::string("{\"ok\":") + (ok ? "true" : "false")
            + ",\"path\":\"" + escapeJson(folder) + "\"}");
    }

    http::response<http::string_body> capture(const http::request<http::string_body>& req)
    {
        const int patientId = jsonIntValue(req.body(), "patientId");
        const int orderId = jsonIntValue(req.body(), "orderId") > 0 ? jsonIntValue(req.body(), "orderId") : database_.latestOrderId(patientId);
        const std::vector<std::string> paths = camera_.capture(orderId);
        for (std::size_t i = 0; i < paths.size(); ++i) {
            database_.addScanResult(orderId, paths[i]);
        }
        std::ostringstream os;
        os << "{\"orderId\":" << orderId << ",\"paths\":[";
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (i) os << ",";
            os << "\"" << escapeJson(paths[i]) << "\"";
        }
        os << "]}";
        return jsonResponse(req, os.str());
    }
};

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

int main(int argc, char* argv[])
{
    return facescan::runBackend(argc, argv);
}
