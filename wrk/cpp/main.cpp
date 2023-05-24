#include <iostream>
#include <fstream>
#include <string>
#include <boost/beast.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

std::string read_html_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file) {
        return "File not found: " + file_path;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content;
}

int main() {
    try {
        net::io_context io_context;

        // Create an endpoint to bind to
        tcp::endpoint endpoint(tcp::v4(), 8070);

        // Create and bind the acceptor
        tcp::acceptor acceptor(io_context, endpoint);
        std::cout << "Server listening on port 8070..." << std::endl;

        while (true) {
            // Wait for a client to connect
            tcp::socket socket(io_context);
            acceptor.accept(socket);

            // Read HTML content from a file (e.g., "index.html")
            std::string html_content = read_html_file("hello.html");

            // Construct an HTTP response with the HTML content
            http::response<http::string_body> response;
            response.version(11);
            response.result(http::status::ok);
            response.reason("OK");
            response.set(http::field::server, "C++ Server");
            response.set(http::field::content_type, "text/html");
            response.body() = html_content;
            response.prepare_payload();

            // Send the response to the client
            http::write(socket, response);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
