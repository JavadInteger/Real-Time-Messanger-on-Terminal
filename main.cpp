#include <iostream>
#include <memory>
#include <set>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include "asio.hpp"

using namespace std;
using asio::ip::tcp;

// ======= Globals =======
class ChatSession;
set<shared_ptr<ChatSession>> sessions;
map<string, set<shared_ptr<ChatSession>>> rooms;
map<string, shared_ptr<ChatSession>> users_by_name;

const vector<string> name_colors = { "\033[36m", "\033[32m", "\033[33m", "\033[35m", "\033[34m" };
const string reset_color = "\033[0m";
int color_index = 0;

enum class Mode { None, Room, Pv };

class ChatSession : public enable_shared_from_this<ChatSession> {
public:
    ChatSession(tcp::socket socket)
        : socket_(std::move(socket)),
          has_name_(false),
          mode_(Mode::None) {
        color_ = name_colors[color_index % name_colors.size()];
        color_index++;
    }

    void start() {
        sessions.insert(shared_from_this());
        deliver("Welcome! Please enter your name: ");
        do_read();
    }

    void deliver(const string& msg) {
        auto self = shared_from_this();
        asio::async_write(
            socket_,
            asio::buffer(msg),
            [this, self](std::error_code ec, size_t) {
                if (ec) {
                    cleanup();
                }
            }
        );
    }

private:
    void do_read() {
        auto self = shared_from_this();
        socket_.async_read_some(
            asio::buffer(buf_),
            [this, self](std::error_code ec, std::size_t length) {
                if (!ec) {
                    string msg(buf_, buf_ + length);
                    trim(msg);
                    if (msg.empty()) {
                        do_read();
                        return;
                    }

                    if (!has_name_) {
                        handle_name(msg);
                    } else {
                        handle_command_or_message(msg);
                    }

                    do_read();
                } else {
                    cleanup();
                }
            }
        );
    }

    void handle_name(const string& name) {
        if (users_by_name.count(name)) {
            deliver("Name already taken. Try another: ");
            return;
        }
        name_ = name;
        has_name_ = true;
        users_by_name[name_] = shared_from_this();

        deliver("Hi " + colored_name() + "! Commands: "
                "/join <room>, /pv <user>, /leave, /whereami, /rooms, /users\n");
        broadcast_all(colored_name() + " joined the server.\n");
    }

    void handle_command_or_message(const string& msg) {
        if (starts_with(msg, "/join ")) {
            string room = msg.substr(6);
            switch_to_room(room);
        } else if (starts_with(msg, "/pv ")) {
            string target = msg.substr(4);
            switch_to_pv(target);
        } else if (msg == "/leave") {
            leave_all();
            deliver("You left all contexts. Mode: none.\n");
        } else if (msg == "/whereami") {
            report_whereami();
        } else if (msg == "/rooms") {
            list_rooms();
        } else if (msg == "/users") {
            list_users();
        } else {
            send_message(msg);
        }
    }

    void switch_to_room(const string& room) {
        leave_all();
        mode_ = Mode::Room;
        active_room_ = room;

        rooms[room].insert(shared_from_this());

        broadcast_room(room, colored_name() + " joined room " + room + ".\n");

        deliver("You are now in room " + room + ". Type to chat here.\n");
    }

    void switch_to_pv(const string& target) {
        if (!users_by_name.count(target)) {
            deliver("User not found.\n");
            return;
        }
        if (target == name_) {
            deliver("You cannot start PV with yourself.\n");
            return;
        }
        leave_all();
        mode_ = Mode::Pv;
        active_pv_ = target;
        deliver("Private chat with " + target + " started. Type to chat.\n");
    }

    void leave_all() {
        if (mode_ == Mode::Room && !active_room_.empty()) {
            auto it = rooms.find(active_room_);
            if (it != rooms.end()) {
                it->second.erase(shared_from_this());
                broadcast_room(active_room_, colored_name() + " left room " + active_room_ + ".\n");
            }
        }
        mode_ = Mode::None;
        active_room_.clear();
        active_pv_.clear();
    }

    void send_message(const string& text) {
        if (mode_ == Mode::Room && !active_room_.empty()) {
            for (auto& s : rooms[active_room_]) {
                if (s.get() == this) continue;
                s->deliver(colored_name() + " [" + active_room_ + "]: " + text + "\n");
            }
        } else if (mode_ == Mode::Pv && !active_pv_.empty()) {
            auto t = users_by_name[active_pv_];
            if (t) {
                t->deliver(colored_name() + " (PV): " + text + "\n");
                t->deliver("You have new message in pv " + name_ + "\n");
            } else {
                deliver("User went offline.\n");
            }
        } else {
            deliver("You are not in a room or pv. Use /join <room> or /pv <user>\n");
        }
    }

    void report_whereami() {
        if (mode_ == Mode::Room) {
            deliver("You are in room: " + active_room_ + "\n");
        } else if (mode_ == Mode::Pv) {
            deliver("You are in pv with: " + active_pv_ + "\n");
        } else {
            deliver("You are in: none\n");
        }
    }

    void list_rooms() {
        string out = "Rooms:\n";
        for (auto& r : rooms) {
            out += "- " + r.first + " (" + to_string(r.second.size()) + " users)\n";
        }
        deliver(out);
    }

    void list_users() {
        string out = "Users:\n";
        for (auto& kv : users_by_name) out += "- " + kv.first + "\n";
        deliver(out);
    }

    void broadcast_all(const string& msg) {
        for (auto& s : sessions) s->deliver(msg);
    }

    void broadcast_room(const string& room, const string& msg) {
        auto it = rooms.find(room);
        if (it == rooms.end()) return;
        for (auto& s : it->second) s->deliver(msg);
    }

    string colored_name() const {
        return color_ + name_ + reset_color;
    }

    void cleanup() {
        sessions.erase(shared_from_this());
        if (!name_.empty() && users_by_name[name_] == shared_from_this()) {
            users_by_name.erase(name_);
        }
        if (mode_ == Mode::Room && !active_room_.empty()) {
            auto it = rooms.find(active_room_);
            if (it != rooms.end()) {
                it->second.erase(shared_from_this());
                broadcast_room(active_room_, colored_name() + " left room " + active_room_ + ".\n");
            }
        }
        mode_ = Mode::None;
        active_room_.clear();
        active_pv_.clear();
        if (has_name_) {
            broadcast_all(colored_name() + " left the server.\n");
        }
    }

    static bool starts_with(const string& s, const string& pre) {
        return s.rfind(pre, 0) == 0;
    }

    static void trim(string& s) {
        s.erase(remove(s.begin(), s.end(), '\r'), s.end());
        s.erase(remove(s.begin(), s.end(), '\n'), s.end());
        auto not_space = [](int ch){ return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    }

    // ======= Fields =======
    tcp::socket socket_;
    char buf_[2048];

    // identity
    string name_;
    string color_;
    bool has_name_;

    // context
    Mode mode_;
    string active_room_;
    string active_pv_;
};

class ChatServer {
public:
    ChatServer(asio::io_context& io, unsigned short port)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    make_shared<ChatSession>(std::move(socket))->start();
                }
                do_accept();
            }
        );
    }

    tcp::acceptor acceptor_;
};

int main() {
    try {
        asio::io_context io;

        ChatServer server(io, 8080);

        cout << "Async Chat Server (Made by JavadInteger) is running on port \"8080\"\n";

        io.run();
    } catch (const exception& e) {
        cerr << "❌ Error: " << e.what() << "\n";
    }
}
