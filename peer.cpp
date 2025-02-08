#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <chrono>
#include <random>

std::mutex peers_mutex, ml_mutex;
std::vector<std::string> connected_peers;
std::unordered_map<std::string, std::unordered_set<std::string>> message_list;
const std::string LOG_FILE = "peer_log.txt";
int msg_counter = 0;

void log(const std::string& msg) {
    std::cout << msg << std::endl;
    std::ofstream file(LOG_FILE, std::ios::app);
    file << msg << std::endl;
}

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

void connect_to_peer(const std::string& peer_addr) {
    size_t colon = peer_addr.find(':');
    std::string ip = peer_addr.substr(0, colon);
    int port = stoi(peer_addr.substr(colon + 1));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
        std::lock_guard<std::mutex> guard(peers_mutex);
        connected_peers.push_back(peer_addr);
        log("Connected to peer: " + peer_addr);
    }
    close(sock);
}

void handle_gossip(int client_sock, const std::string& sender_addr) {
    char buffer[1024];
    int bytes_read = read(client_sock, buffer, sizeof(buffer));
    if (bytes_read <= 0) return;

    std::string msg(buffer, bytes_read);
    std::string hash = std::to_string(std::hash<std::string>{}(msg));

    {
        std::lock_guard<std::mutex> guard(ml_mutex);
        if (message_list.find(hash) == message_list.end()) {
            message_list[hash].insert(sender_addr);
            log("Received message: " + msg + " from " + sender_addr);
            
            // Forward to all except sender
            std::vector<std::string> peers;
            {
                std::lock_guard<std::mutex> guard(peers_mutex);
                peers = connected_peers;
            }
            for (const auto& peer : peers) {
                if (peer != sender_addr) {
                    size_t colon = peer.find(':');
                    std::string ip = peer.substr(0, colon);
                    int port = stoi(peer.substr(colon + 1));
                    
                    int sock = socket(AF_INET, SOCK_STREAM, 0);
                    sockaddr_in server_addr{};
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_port = htons(port);
                    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
                    
                    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
                        send(sock, msg.c_str(), msg.size(), 0);
                    }
                    close(sock);
                }
            }
        }
    }
    close(client_sock);
}

void server_thread(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    while (true) {
        int client_sock = accept(server_fd, nullptr, nullptr);
        char buffer[1024];
        int bytes_read = read(client_sock, buffer, sizeof(buffer));
        if (bytes_read <= 0) {
            close(client_sock);
            continue;
        }
        
        std::string sender_addr(buffer, bytes_read);
        std::thread(handle_gossip, client_sock, sender_addr).detach();
    }
}

void ping_peers() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(13));
        std::vector<std::string> peers;
        {
            std::lock_guard<std::mutex> guard(peers_mutex);
            peers = connected_peers;
        }
        
        for (const auto& peer : peers) {
            std::string ip = peer.substr(0, peer.find(':'));
            int result = system(("ping -c 1 " + ip + " > /dev/null").c_str());
            if (result != 0) {
                // Handle dead node logic
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./peer <config_file> <port>" << std::endl;
        return 1;
    }

    // Start server thread
    std::thread(server_thread, atoi(argv[2])).detach();
    
    // Read seed nodes from config file
    // Connect to seeds, register, get peer list
    // Connect to selected peers
    
    // Start message generation thread
    // Start ping thread
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}