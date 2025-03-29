#include <vector>
#include <map>
#include <unordered_set>
#include <mutex>

#include <condition_variable>

#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <iostream>
#include <thread>

#include <memory>
#include <deque>

#include <algorithm>
#include <string>
#include <functional>
#include <fstream>

using boost::asio::ip::tcp;

#define WIN32_LEAN_AND_MEAN
#ifndef YOUR_HEADER_FILE_H 
#define YOUR_HEADER_FILE_H
#endif  //YOUR_HEADER_FILE_H

struct ClientInfo
{
    std::shared_ptr<tcp::socket> socket;
};

class my_room
{
public:
    std::string name;
    int unique_id;

    std::vector<ClientInfo> players_in_this_room;
    std::shared_ptr<tcp::socket> admim;
};

std::vector < my_room > rooms;


int generate_unique_int()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    static std::mt19937_64 rng(millis);
    std::uniform_int_distribution<int> dist(0, 10000);

    int random_part = dist(rng);
    int unique_id = static_cast<int>(millis % 100000000) + random_part;

    return unique_id;
}


std::string serialize_rooms()
{
    if (rooms.empty())
        return " ";

    std::ostringstream oss;
    for (const auto& room : rooms)
    {
        oss << room.name << ",";
        oss << room.unique_id << ";";
    }

    std::string result = oss.str();
    if (!result.empty() && result.back() == ';')
        result.pop_back();

    return result;
}


std::shared_ptr<boost::asio::io_context> io_context;

bool IsRequest(std::string socket, std::string message)
{
    return socket.find(message) != std::string::npos;
}

class ChatServer
{
public:

    ChatServer(boost::asio::io_context& io_context, const std::string& ip, short port, const std::string& nickname)
        : acceptor_(io_context, tcp::endpoint(boost::asio::ip::make_address(ip), port)), nickname_(nickname)
    {
        do_accept();
    }

    void send_message_to_all(const std::string& message)
    {
        for (auto& client : clients_)
        {
            boost::asio::async_write(*client.socket, boost::asio::buffer(message + "\n"),
                [](boost::system::error_code, std::size_t) {});
        }
    }
    void send_message_to_socket(const std::string& message, std::shared_ptr<tcp::socket> socket)
    {
        if (!socket || !socket->is_open())
        {
            std::cerr << "Ошибка: попытка отправить сообщение на закрытый сокет\n";
            return;
        }

        auto safe_message = std::make_shared<std::string>(message.empty() ? "\n" : message + "\n");

        boost::asio::async_write(*socket, boost::asio::buffer(*safe_message),
            [safe_message](boost::system::error_code ec, std::size_t /*bytes_transferred*/)
            {
                if (ec)
                    std::cerr << "Ошибка отправки: " << ec.message() << "\n";
            });
    }
    void send_message_to_all_members_of_room(const std::string& message, std::shared_ptr<tcp::socket> socket)
    {

        auto safe_message = std::make_shared<std::string>(message.empty() ? "\n" : message + "\n");

 
        auto room_that_we_want_to_find =

            std::find_if(rooms.begin(), rooms.end(), [&](const my_room& target)
                {
                    for (int i = 0; i < target.players_in_this_room.size(); i++)
                    {
                        return target.players_in_this_room[i].socket == socket;
                    }

                });


        for (auto& client : room_that_we_want_to_find->players_in_this_room)
        {
            if (socket == client.socket)
                continue;

            boost::asio::async_write(*client.socket, boost::asio::buffer(*safe_message),
                [safe_message](boost::system::error_code ec, std::size_t /*bytes_transferred*/)
                {
                    if (ec)
                        std::cerr << "Ошибка отправки: " << ec.message() << "\n";
                });
        }
    }



private:

    void do_accept()
    {
        auto socket = std::make_shared<tcp::socket>(acceptor_.get_executor());
        acceptor_.async_accept(*socket, [this, socket](boost::system::error_code ec)
            {
                if (!ec)
                {
                    clients_.push_back({ socket });
                    do_read(socket);
                }
                do_accept();
            });
    }

    void do_read(std::shared_ptr<tcp::socket> socket)
    {
        auto buffer = std::make_shared<boost::asio::streambuf>();
        boost::asio::async_read_until(*socket, *buffer, "\n", [this, socket, buffer](boost::system::error_code ec, std::size_t) {
            if (!ec)
            {
                std::istream is(buffer.get());
                std::string buffer_message;
                std::getline(is, buffer_message);

                if (IsRequest(buffer_message, "c.s:create_room:"))
                {
                    std::string inner_data = buffer_message.erase(0, 16);
                    rooms.push_back
                    (
                        { 
                            inner_data, 
                            generate_unique_int()                           
                        }
                    );
                    rooms.at(rooms.size() - 1).players_in_this_room.push_back({ socket });
                    rooms.at(rooms.size() - 1).admim = socket;
                }
                else if (IsRequest(buffer_message, "c.s:get_rooms"))
                {
                    std::cout << "User requested rooms : " + serialize_rooms() << " : ";
                    send_message_to_socket("#ROOMS:" + serialize_rooms(), socket);
                }
                else if (IsRequest(buffer_message, "c.s:close_room"))
                {
                    std::cout << "User (admin) closed his room";

                    auto room_that_we_want_to_delete = 

                    std::find_if(rooms.begin(), rooms.end(), [&](const my_room& target)
                        {
                            return target.admim == socket;
                        });
                    rooms.erase(room_that_we_want_to_delete);
                }
                else if (IsRequest(buffer_message, "CHAT:"))
                {
                    std::string inner_data = buffer_message.erase(0, 5);
                    std::cout << "Chat message - " + inner_data;
                    send_message_to_all_members_of_room("#CHAT:" + inner_data, socket);
                }
                else
                {
                    send_message_to_all_members_of_room(buffer_message, socket);
                }


                for (auto& room : rooms)
                {
                    std::cout << room.name.c_str() << "\n";
                }
                std::cout << "\n";

                do_read(socket);
            }
            else {
                handle_disconnect(socket);
            }
            });
    }

    void handle_disconnect(std::shared_ptr<tcp::socket> socket)
    {

    }

    tcp::acceptor           acceptor_;
    std::vector<ClientInfo> clients_;
    std::string             nickname_;
};
//server
std::shared_ptr<ChatServer> server;


void run_server()
{
    // Создаём новый io_context и executor
    io_context = std::make_shared<boost::asio::io_context>();

    // Создаём новый сервер с новым io_context
    server = std::make_shared<ChatServer>(*io_context, "127.0.0.1", 1287, "central-component-server");

    io_context->run();
}

void async_create_control_server()
{
    run_server();
}

int main()
{

    async_create_control_server();    

	return 0;
}