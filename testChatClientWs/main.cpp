#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <cstdlib>
#include <iostream>
#include <queue>

#include "ChatMessageWrapper.hpp"

namespace beast = boost::beast;
using boost::asio::ip::tcp;

constexpr int BUFF_SIZE = 32;

class Client {
   public:
    Client(boost::asio::io_context& io_context,
           tcp::resolver::results_type& endpoints, std::string nickname)
        : ws_(boost::asio::make_strand(io_context)), nickname_(nickname) {
        connect(endpoints);
    }

    void deliver(const ChatMessageWrapper& chatMessage) {
        std::lock_guard<std::mutex> lock(writeMsgsMutex_);
        bool writeInProgress = !writeMsgs_.empty();
        writeMsgs_.push_back(chatMessage);
        std::cout << "queue cnt: " << writeMsgs_.size() << std::endl;
        if (!writeInProgress) {
            write();
        }
    }

    void write() {
        std::cout << "write called " << std::endl;
        auto chatMessage = writeMsgs_.front();
        auto sendBytes = chatMessage.encode();
        ws_.binary(true);
        ws_.async_write(boost::asio::buffer(sendBytes),
                        [this](beast::error_code ec, std::size_t) {
                            if (!ec) {
                                std::cout << "send success" << std::endl;
                                writeMsgs_.pop_front();
                            } else {
                                std::cerr << "error in writing" << std::endl;
                            }
                        });
    }

    void read() {
        ws_.binary(true);
        ws_.async_read(buffer_, [this](beast::error_code ec, std::size_t) {
            if (!ec) {
                std::cout << "read success" << std::endl;
                auto bytesData = beast::buffers_to_string(buffer_.data());
                ChatMessageWrapper chatMsg;
                chatMsg.decode(bytesData);
                std::cout << "msg: " << chatMsg.getMessageText() << std::endl;
                read();
            } else {
                std::cerr << "error in reading" << ec.message() << std::endl;
            }
        });
    }

    void connect(const tcp::resolver::results_type& endpoints) {
        beast::get_lowest_layer(ws_).async_connect(
            endpoints, [this](beast::error_code ec, const tcp::endpoint&) {
                if (!ec) {
                    std::cout << "connect success" << std::endl;
                    handshake();
                } else {
                    std::cerr << "error in connect" << std::endl;
                }
            });
    }

    void handshake() {
        beast::get_lowest_layer(ws_).expires_never();
        std::string host = "localhost:4001";
        ws_.async_handshake(host, "/", [this](beast::error_code ec) {
            if (!ec) {
                std::cout << "handshake success " << std::endl;
                connected_.store(true);
                read();
            } else {
                std::cerr << "error in handshake" << std::endl;
            }
        });
    }

    bool isConnected() const { return connected_.load(); }

   private:
    boost::beast::websocket::stream<beast::tcp_stream> ws_;

    std::atomic<bool> connected_{false};
    std::string nickname_;

    std::deque<ChatMessageWrapper> writeMsgs_;
    std::mutex writeMsgsMutex_;

    beast::flat_buffer buffer_;
};

int main(int argc, const char* argv[]) {
    try {
        std::string port = "4001";
        std::string host = "localhost";

        boost::asio::io_context io_context;
        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(host, port);

        std::cout << "Enter your nickname : ";
        std::string nickname;
        std::getline(std::cin, nickname);

        Client client(io_context, endpoints, nickname);

        std::thread t([&client, nickname]() {
            while (!client.isConnected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            while (true) {
                std::string msg;
                std::cin >> msg;

                std::chrono::system_clock::time_point now =
                    std::chrono::system_clock::now();
                auto duration = now.time_since_epoch();
                uint32_t timestamp =
                    std::chrono::duration_cast<std::chrono::seconds>(duration)
                        .count();

                ChatMessageWrapper chatMessage(nickname, timestamp,
                                               chat::DataType::TEXT);
                chatMessage.setTextMessage(msg);

                client.deliver(chatMessage);
            }
        });

        io_context.run();
        t.join();

    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
