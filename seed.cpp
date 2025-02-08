#include <iostream>
#include <fstream>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

std::mutex pl_mutex;
std::unordered_set<std::string> peer_list;
const std::string LOG_FILE = "seed_log.txt";

void log(const std::string& msg) {
    std::cout << msg << std::endl;
    std::ofstream file(LOG_FILE, std::ios::app);
    file << msg << std::endl;
}

void handle_client(int client_sock) {
    char buffer[1024];
    int bytes_read = read(client_sock, buffer, sizeof(buffer));
    if (bytes_read <= 0) return;

    std::string msg(buffer, bytes_read);
    if (msg.find("REGISTER:") == 0) {
        std::string peer_addr = msg.substr(9);
        {
            std::lock_guard<std::mutex> guard(pl_mutex);
            peer_list.insert(peer_addr);
        }
        log("Registered peer: " + peer_addr);
        send(client_sock, "OK", 2, 0);
    } else if (msg.find(" qREQUEST_PEERS") == 0) {
        std::string response = "PEERS_LIST:";
        {
            std::lock_guard<std::mutex> guard(pl_mutex);
            for (const auto& peer : peer_list) {
                response += peer + ",";
            }
        }
        send(client_sock, response.c_str(), response.size(), 0);
    } else if (msg.find("DEAD:") == 0) {
        std::string dead_node = msg.substr(5);
        {
            std::lock_guard<std::mutex> guard(pl_mutex);
            peer_list.erase(dead_node);
        }
        log("Removed dead node: " + dead_node);
    }
    close(client_sock);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: ./seed <port>" << std::endl;
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    log("Seed node started on port " + std::string(argv[1]));
    
    while (true) {
        int client_sock = accept(server_fd, nullptr, nullptr);
        std::thread(handle_client, client_sock).detach();
    }
}