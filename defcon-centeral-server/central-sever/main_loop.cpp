#include <vector>
#include <map>
#include <unordered_set>
#include <mutex>
#include <chrono>  
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
    std::string nickname = "";
    int id;
};

class my_room
{
public:
    std::string name;
    int unique_id;

    std::vector<ClientInfo> players_in_this_room;
    std::shared_ptr<tcp::socket> admin;
    std::chrono::steady_clock::time_point last_message_time;
};

std::vector < my_room > rooms;

std::string serialize_vector_players(my_room room)
{
    std::ostringstream oss;
    for (int i = 0; i < room.players_in_this_room.size(); i++)
    {
        room.players_in_this_room[i].id = i;

        // Сериализация основных данных игрока
        oss << i << "," << room.players_in_this_room[i].nickname << "," << "0" << "," << "0" ;

        oss << ";";
    }
    return oss.str();
}

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

    auto find_room_by_socket(const std::shared_ptr<tcp::socket>& socket)
    {
        // Поиск комнаты, в которой находится отправитель
        auto room_it = std::find_if(rooms.begin(), rooms.end(),
            [&socket](const my_room& room)
            {
                return std::any_of(room.players_in_this_room.begin(), room.players_in_this_room.end(),
                [&socket](const ClientInfo& player)
                    {
                        return player.socket == socket;
                    });
            });

        return room_it;
    }

    void send_message_to_all_members_of_room(const std::string& message, const std::shared_ptr<tcp::socket>& sender_socket)
    {
        auto safe_message = std::make_shared<std::string>(message.empty() ? "\n" : message + "\n");

        // Поиск комнаты, в которой находится отправитель
        auto room_it = std::find_if(rooms.begin(), rooms.end(),
            [&sender_socket](const my_room& room)
            {
                return std::any_of(room.players_in_this_room.begin(), room.players_in_this_room.end(),
                [&sender_socket](const ClientInfo& player)
                    {
                        return player.socket == sender_socket;
                    });
            });

        if (room_it != rooms.end())
        {
            // Отправка сообщения всем участникам комнаты, кроме отправителя
            for (auto& player : room_it->players_in_this_room)
            {
                if (player.socket != sender_socket && player.socket && player.socket->is_open())
                {
                    boost::asio::async_write(*player.socket, boost::asio::buffer(*safe_message),
                        [safe_message](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/)
                        {
                            if (ec)
                            {
                                std::cerr << "Ошибка отправки: " << ec.message() << "\n";
                            }
                        });
                }
            }
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
                    
                system("cls"); // for linux - "clear"

                //non-rooms processing
                if      (IsRequest(buffer_message, "c.s:create_room:"))
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
                    rooms.at(rooms.size() - 1).admin = socket;
                    rooms.at(rooms.size() - 1).last_message_time = std::chrono::steady_clock::now();
                    send_message_to_socket("#ROOMS:" + serialize_rooms(), socket);
                    send_message_to_socket("#CONNECTED", socket);

                    std::cout << "USER created server : " << inner_data.c_str() << "\n" << "\n";

                    std::cout << "Server list : " << "\n";
                    for (int i = 0; i < rooms.size(); i++)
                    {
                        std::cout << i << "." << rooms[i].name << "\n";
                    }
                    std::cout << "\n";

                }
                else if (IsRequest(buffer_message, "c.s:close_room"))
                {
                    send_message_to_all_members_of_room("#SERVER:CLOSED_CONNECTION", socket);

                    std::cout << "User (admin) closed his room" << "\n";

                    auto room_that_we_want_to_delete =

                        std::find_if(rooms.begin(), rooms.end(), [&](const my_room& target)
                            {
                                return target.admin == socket;
                            });
                    rooms.erase(room_that_we_want_to_delete);
                    std::cout << "\n";

                }
                else if (IsRequest(buffer_message, "c.s:join_room:"))
                {
                    std::string inner_data = buffer_message.erase(0, 14);

                    auto room_that_we_want_to_find =
                        std::find_if(rooms.begin(), rooms.end(), [&](const my_room& target)
                            {
                                return target.unique_id == std::atoi(inner_data.c_str());

                            });

                    std::cout << "User wants to join server : " << room_that_we_want_to_find->name << "\n";

                    ClientInfo c;
                    c.socket = socket;
                    room_that_we_want_to_find->players_in_this_room.push_back(c);
                    send_message_to_socket("#CONNECTED", socket);

                    std::cout << "User joined server" << "\n";
                    std::cout << "\n";
                }
                else if (IsRequest(buffer_message, "c.s:get_rooms"))
                {
                    if (rooms.size() != 0)
                    {
                        std::cout << "User requested rooms : " + serialize_rooms() << "" << "\n";
                        std::cout << "\n";
                        send_message_to_socket("#ROOMS:" + serialize_rooms(), socket);
                    }
                    else
                    {
                        send_message_to_socket("#ROOMS:" + serialize_rooms(), socket);
                        std::cout << "User requested rooms, but they were empty :(" << "\n";
                        std::cout << "\n";
                    }
                }
                else if (IsRequest(buffer_message, "c.s:leave_room"))
                {
                    auto room = find_room_by_socket(socket);

                    if (room != rooms.end())
                    {
                        room->last_message_time = std::chrono::steady_clock::now();
                        send_message_to_all_members_of_room("#CLASS.PLAYERS_VECTOR:" + serialize_vector_players(*room), socket);

                        auto player_it = std::find_if(room->players_in_this_room.begin(), room->players_in_this_room.end(),
                            [&socket](const ClientInfo& player)
                            {
                                return player.socket == socket;
                            });

                        room->players_in_this_room.erase(player_it);
                    }
                }

                //rooms processing
                else if (IsRequest(buffer_message, "c.s:updating_nickname:"))
                {
                    std::string inner_data = buffer_message.erase(0, 22);

                    std::cout << "USER JOINED SERVER AND CREATED PROFILE : " << inner_data.c_str() << "\n";

                    for (int i = 0; i < rooms.size(); i++)
                    {
                        for (int i2 = 0; i2 < rooms[i].players_in_this_room.size(); i2++)
                        {
                            if (rooms[i].players_in_this_room[i2].socket == socket)
                            {
                                rooms[i].players_in_this_room[i2].nickname = inner_data;
                                rooms[i].last_message_time = std::chrono::steady_clock::now();
                                send_message_to_all_members_of_room("#CLASS.PLAYERS_VECTOR:" + serialize_vector_players(rooms[i]), rooms[i].players_in_this_room[i2].socket);
                                send_message_to_socket("#CLASS.PLAYERS_VECTOR:" + serialize_vector_players(rooms[i]), rooms[i].players_in_this_room[i2].socket);
                                
                                std::cout << "Users room list : " << "\n";
                                for (int d = 0; d < rooms[i].players_in_this_room.size(); d++)
                                {
                                    std::cout << d << "." << rooms[i].players_in_this_room.at(d).nickname << "\n";
                                }
                            }
                        }
                    }
                    std::cout << "\n";

                }
                else if (IsRequest(buffer_message, "CHAT:"))
                {
                    std::string inner_data = buffer_message.erase(0, 5);
                    std::cout << "Chat message - " + inner_data << "\n";
                    send_message_to_all_members_of_room("#CHAT:" + inner_data, socket);
                    std::cout << "\n";

                    auto room = find_room_by_socket(socket);

                    if (room != rooms.end())
                        room->last_message_time = std::chrono::steady_clock::now();
                }
                else
                {
                    auto room = find_room_by_socket(socket);

                    if (room != rooms.end())
                        room->last_message_time = std::chrono::steady_clock::now();

                    std::cout << "INGAME MSG: " + buffer_message << "\n";
                    send_message_to_all_members_of_room(buffer_message, socket);
                    std::cout << "\n";
                }


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

void control_rooms()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto now = std::chrono::steady_clock::now();

        for (int i = 0; i < rooms.size(); i++)
        {
            //time activity check
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - rooms[i].last_message_time).count();

            if (elapsed > 230)
            {
                std::cout << "Room " << rooms[i].name << " Has been deleted for being not active" << std::endl;
                server->send_message_to_all_members_of_room("#SERVER:CLOSED_CONNECTION", rooms[i].players_in_this_room[0].socket);
                server->send_message_to_socket("#SERVER:CLOSED_CONNECTION", rooms[i].players_in_this_room[0].socket);
                rooms.erase(rooms.begin() + i);
                continue;
            }

            //users count check
            if (rooms[i].players_in_this_room.size() == 0)
            {
                std::cout << "Room " << rooms[i].name << " Has been closed by AUTOCLOSE (Reason : No Users)" << std::endl;
                rooms.erase(rooms.begin() + i);
                continue;
            }
        }
    }
}
void async_create_control_server()
{
    std::thread rooms_control_thread(control_rooms);

    run_server();

    rooms_control_thread.join();
}

int main()
{
    std::cout << "Server is working..." << "\n";

    setlocale(LC_ALL, "Russian");
    async_create_control_server();    

	return 0;
}