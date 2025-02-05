#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <sstream>
#include <chrono>
#include <cstdlib>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <iomanip>
#include <map>
#include <numeric>
#include <cmath>
#include <sys/statvfs.h>
#include <string>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

std::mutex cpu_mutex;

struct CPUStats {
    unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
};

struct NetworkStats {
    unsigned long rx_bytes;
    unsigned long tx_bytes;
};

CPUStats parse_cpu_line(const std::string& line) {
    std::istringstream iss(line);
    std::string cpu;
    CPUStats stats = {0, 0, 0, 0, 0, 0, 0, 0};
    iss >> cpu >> stats.user >> stats.nice >> stats.system >> stats.idle 
        >> stats.iowait >> stats.irq >> stats.softirq >> stats.steal;
    return stats;
}

std::string human_readable_size(unsigned long bytes) {
    const char* suffix[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double size = bytes;
    while (size > 1024 && i < 4) {
        size /= 1024;
        i++;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << size << " " << suffix[i];
    return out.str();
}

std::string get_disk_capacity(const std::string& path = "/") {
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0) {
        std::cerr << "Error retrieving disk capacity for path: " << path << " (errno: " << errno << ")\n";
        return "Disk capacity info unavailable\n";
    }

    unsigned long total = stat.f_blocks * stat.f_frsize;
    unsigned long free = stat.f_bfree * stat.f_frsize;
    unsigned long used = total - free;

    std::ostringstream oss;
    oss << "Disk Capacity: " << human_readable_size(used) << " used / "
        << human_readable_size(total) << " total (" 
        << human_readable_size(free) << " free)\n";

    return oss.str();
}

std::map<std::string, NetworkStats> get_network_stats() {
    std::ifstream netfile("/proc/net/dev");
    if (!netfile.is_open()) {
        std::cerr << "Error: Unable to open /proc/net/dev\n";
        return {};
    }

    std::map<std::string, NetworkStats> stats;
    std::string line;
    
    std::getline(netfile, line); // Skip first line
    std::getline(netfile, line); // Skip second line

    while (std::getline(netfile, line)) {
        std::istringstream iss(line);
        std::string iface;
        unsigned long rx_bytes, tx_bytes;

        iss >> iface >> rx_bytes;
        iss.ignore(64, ' '); // Skip unnecessary columns
        iss >> tx_bytes;

        if (!iface.empty() && iface.back() == ':') iface.pop_back();
        stats[iface] = {rx_bytes, tx_bytes};
    }
    return stats;
}

std::vector<CPUStats> get_cpu_times() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open /proc/stat\n";
        return {};
    }

    std::string line;
    std::vector<CPUStats> cpu_stats;
    while (std::getline(file, line)) {
        if (line.find("cpu") != 0) break;
        cpu_stats.push_back(parse_cpu_line(line));
    }
    return cpu_stats;
}

double calculate_cpu_usage(const CPUStats& prev, const CPUStats& curr) {
    unsigned long prev_idle = prev.idle + prev.iowait;
    unsigned long curr_idle = curr.idle + curr.iowait;
    unsigned long prev_total = prev.user + prev.nice + prev.system + prev.idle +
                               prev.iowait + prev.irq + prev.softirq + prev.steal;
    unsigned long curr_total = curr.user + curr.nice + curr.system + curr.idle +
                               curr.iowait + curr.irq + curr.softirq + curr.steal;

    unsigned long total_diff = curr_total - prev_total;
    unsigned long idle_diff = curr_idle - prev_idle;

    if (total_diff == 0) return 0.0;
    return std::round((100.0 * (1.0 - (static_cast<double>(idle_diff) / total_diff))) * 100) / 100.0;
}

std::string collect_system_stats() {
    std::ostringstream stats;
    struct sysinfo info;
    
    if (sysinfo(&info) != 0) {
        return "Error: Unable to retrieve system stats\n";
    }

    static std::vector<CPUStats> prev_stats = get_cpu_times();
    
    if (prev_stats.empty()) return "Error: Unable to read CPU stats\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::vector<CPUStats> curr_stats = get_cpu_times();
    
    if (curr_stats.empty() || prev_stats.size() != curr_stats.size()) {
        return "Error: CPU stats mismatch\n";
    }

    stats << std::fixed << std::setprecision(2);
    stats << "CPU Load: " << calculate_cpu_usage(prev_stats[0], curr_stats[0]) << "% [";
    for (size_t i = 1; i < prev_stats.size(); ++i) {
        if (i > 1) stats << ", ";
        stats << i << ": " << calculate_cpu_usage(prev_stats[i], curr_stats[i]) << "%";
    }
    stats << "]\n";

        stats << "Memory Usage: "
              << "Used " << human_readable_size(info.totalram - info.freeram) << " / "
              << "Total " << human_readable_size(info.totalram) << " / "
              << "Free " << human_readable_size(info.freeram) << "\n";
        stats << "Swap Usage: " 
              << human_readable_size(info.totalswap - info.freeswap) << " / "
              << human_readable_size(info.totalswap) << " \n";
    // ✅ **Update `prev_stats` for the next call**
    prev_stats = curr_stats;

    stats << get_disk_capacity();

// Network BPS Calculation with high-precision timing
auto prev_net_stats = get_network_stats();
auto start_time = std::chrono::steady_clock::now();

std::this_thread::sleep_for(std::chrono::milliseconds(500));

auto curr_net_stats = get_network_stats();
auto end_time = std::chrono::steady_clock::now();
double interval_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

// Avoid division by zero
if (interval_ms == 0) interval_ms = 1;

stats << "Network Bandwidth Usage:\n";

// Define interfaces to exclude
std::unordered_set<std::string> excluded_interfaces = {"lo", "pvpnksintrf0"};

for (const auto& [iface, curr] : curr_net_stats) {
    if (prev_net_stats.find(iface) != prev_net_stats.end()) {
        // Skip excluded interfaces
        if (excluded_interfaces.count(iface)) continue;

        const auto& prev = prev_net_stats[iface];

        // Ensure no underflow
        unsigned long rx_diff = (curr.rx_bytes >= prev.rx_bytes) ? (curr.rx_bytes - prev.rx_bytes) : 0;
        unsigned long tx_diff = (curr.tx_bytes >= prev.tx_bytes) ? (curr.tx_bytes - prev.tx_bytes) : 0;

        // Avoid division by zero
        double rx_bps = (interval_ms > 0) ? ((rx_diff * 8.0 * 1000) / interval_ms) : 0;
        double tx_bps = (interval_ms > 0) ? ((tx_diff * 8.0 * 1000) / interval_ms) : 0;

        // Convert to human-readable format
        auto format_bps = [](double bps) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2);  // Ensure 2 decimal places
            if (bps >= 1e9) {
                out << (bps / 1e9) << " Gbps";
            } else if (bps >= 1e6) {
                out << (bps / 1e6) << " Mbps";
            } else if (bps >= 1e3) {
                out << (bps / 1e3) << " Kbps";
            } else {
                out << bps << " bps";
            }
            return out.str();
        };

        // Format output
        stats << iface << " - RX: " << format_bps(rx_bps) << ", TX: " << format_bps(tx_bps) << "\n";
    }
}

return stats.str();
}

// Function to determine MIME type
std::string get_mime_type(const std::string& path) {
    static const std::unordered_map<std::string, std::string> mime_types = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".json", "application/json"},
        {".txt", "text/plain"}
    };

    size_t ext_pos = path.find_last_of('.');
    if (ext_pos == std::string::npos) return "application/octet-stream"; // Default

    std::string ext = path.substr(ext_pos);
    auto it = mime_types.find(ext);
    return (it != mime_types.end()) ? it->second : "application/octet-stream";
}

void handle_http_request(http::request<http::string_body>& req, http::response<http::string_body>& res) {
    std::string path(req.target().data(), req.target().size());  // ✅ Convert target to std::string

    if (path == "/stats") {
        // ✅ Serve system stats as plain text
        res.version(req.version());
        res.result(http::status::ok);
        res.set(http::field::server, "SystemMonitorServer");
        res.set(http::field::content_type, "text/plain");
        res.body() = collect_system_stats();
    } else {
        if (path == "/") path = "/index.html";  // Default to index.html

        // ✅ Prevent path traversal attacks
        if (path.find("..") != std::string::npos) {
            res.result(http::status::forbidden);
            res.set(http::field::content_type, "text/plain");
            res.body() = "403 - Forbidden";
        } else {
            // ✅ Serve static files from www/
            std::string full_path = "www" + path;
            std::ifstream file(full_path, std::ios::binary);
            if (!file.is_open()) {
                res.result(http::status::not_found);
                res.set(http::field::content_type, "text/plain");
                res.body() = "404 - Not Found";
            } else {
                std::stringstream buffer;
                buffer << file.rdbuf();
                res.result(http::status::ok);
                res.set(http::field::content_type, get_mime_type(full_path));
                res.body() = buffer.str();
            }
        }
    }
    res.prepare_payload();
}



void do_session(std::shared_ptr<tcp::socket> socket, ssl::context& ctx) {
    try {
        beast::ssl_stream<tcp::socket> stream(std::move(*socket), ctx);
        stream.handshake(ssl::stream_base::server);

        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(stream, buffer, req);
        
        http::response<http::string_body> res;
        handle_http_request(req, res);
        http::write(stream, res);
    } catch (std::exception& e) {
        std::cerr << "Session error: " << e.what() << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: ./server <address> <port> <cert_file> <key_file>\n";
        return EXIT_FAILURE;
    }

    net::io_context ioc{1};
    ssl::context ctx{ssl::context::tlsv12};
    ctx.use_certificate_chain_file(argv[3]);
    ctx.use_private_key_file(argv[4], ssl::context::pem);
    ctx.load_verify_file("ca.pem");

    auto const address = net::ip::make_address(argv[1]);
    auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    std::cout << "Server listening on https://" << address << ":" << port << std::endl;

    tcp::acceptor acceptor(ioc, {address, port});
    for (;;) {
        try {
            auto socket = std::make_shared<tcp::socket>(ioc);
            acceptor.accept(*socket);
            std::thread(&do_session, socket, std::ref(ctx)).detach();
        } catch (std::exception& e) {
            std::cerr << "Accept error: " << e.what() << "\n";
        }
    }
}
