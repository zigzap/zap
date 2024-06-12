#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/beast.hpp>
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <iostream>
#include <syncstream>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
using tcp     = net::ip::tcp;
using namespace net::experimental::awaitable_operators;
using executor_t = net::thread_pool::executor_type;
using acceptor_t = net::deferred_t::as_default_on_t<net::basic_socket_acceptor<tcp, executor_t>>;
using socket_t   = net::deferred_t::as_default_on_t<net::basic_stream_socket<tcp, executor_t>>;

[[maybe_unused]] static std::string read_html_file(std::string const& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

static auto const make_response_message = [](bool keep_alive) {
    // Construct an HTTP response with the HTML content
    std::string_view msg = "Hello from C++!!!"; // or read_html_file("hello.html");

    http::response<http::span_body<char const>> res{http::status::ok, 11, msg};
    res.set(http::field::server, "C++ Server");
    res.set(http::field::content_type, "text/html");
    res.keep_alive(keep_alive);
    res.prepare_payload();
    return res;
};

static auto const s_cooked_response = [] {
    static auto const text = boost::lexical_cast<std::string>(make_response_message(true));
    return net::buffer(text);
}();

net::awaitable<void, executor_t> handle_client_async(socket_t socket) try {
    socket.set_option(tcp::no_delay(true)); // no difference observed in benchmark

#ifdef TRUE_HTTP // This affects throughput by only about -10%
    beast::flat_buffer buf;
    for (http::request<http::empty_body> req;; req.clear()) {
        auto [ec, _] = co_await async_read(socket, buf, req, as_tuple(net::deferred));
        if (ec)
            break;
    #ifdef CACHED_RESPONSE
        // emulate caching server
        co_await async_write(socket, s_cooked_response);
    #else
        // This is a more realistic way but probably NOT what Kestrel is doing for the static route
        // It affects throughput by about -25%
        co_await async_write(socket, make_response_message(req.keep_alive()));
    #endif
        if (!req.keep_alive())
            break;
    }
#else
    // Since we're ignoring the requests, we might as well assume they're correct. (INSECURE)
    for (beast::flat_buffer buf;;) {
        auto [ec, n] = co_await async_read_until(socket, buf, "\r\n\r\n", as_tuple(net::deferred));
        if (ec)
            break;
        buf.consume(n);
        co_await async_write(socket, s_cooked_response);
    }
#endif
} catch (beast::system_error const& e) {
    std::osyncstream(std::cerr) << "handle_client_async error: " << e.code().message() << std::endl;
}

net::awaitable<void, executor_t> server(uint16_t port) {
    auto ex = co_await net::this_coro::executor;

    for (acceptor_t acceptor(ex, {{}, port});;)
        co_spawn(ex,                                                    //
                 handle_client_async(co_await acceptor.async_accept()), //
                 net::detached);
}

int main() try {
    // Create a thread pool
    net::thread_pool pool(4);
    executor_t ex = pool.get_executor();

    // Create and bind the acceptor
    co_spawn(ex, server(8070), net::detached);

    std::cout << "Server listening on port 8070..." << std::endl;

    pool.join();
} catch (std::exception const& e) {
    std::cerr << "Main error: " << e.what() << std::endl;
}
