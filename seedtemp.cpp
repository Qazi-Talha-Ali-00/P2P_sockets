#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <chrono>
#include <random>
#include <unordered_map>

using namespace std;

mutex file;
mutex peers_mtx;

class SeedNode {
private:
    string self_ip;
    int self_port;
    ofstream log_file;
    unordered_map<string, int> peer_list;  // member variable: ip:port -> port
    unordered_map<string, int> peer_sockets;

    string getCurrentTimestamp() {
        auto now = chrono::system_clock::now();
        auto time = chrono::system_clock::to_time_t(now);
        string timestamp = ctime(&time);
        timestamp.pop_back();
        return timestamp;
    }

    vector<int> powerLawDistribution(int n, double alpha = 2.5) {
        random_device rd;
        mt19937 gen(rd());
        uniform_real_distribution<> dis(0.0, 1.0);

        vector<int> degrees;
        for (int i = 0; i < n; i++) {
            double u = dis(gen);
            double x = pow(1 - u, -1.0 / (alpha - 1));
            degrees.push_back(min((int)ceil(x), n - 1));
        }
        return degrees;
    }

    string generatePowerLawPeerList() {
        vector<string> peers;
        {
            lock_guard<mutex> lock(peers_mtx);
            for (auto &[key, _] : peer_list) {
                peers.push_back(key);
            }
        }

        if (peers.empty()) return "";

        vector<int> degrees = powerLawDistribution(peers.size());
        string result;
        for (size_t i = 0; i < peers.size(); i++) {
            for (int j = 0; j < degrees[i]; j++) {
                result += peers[i] + ",";
            }
        }
        if (!result.empty()) result.pop_back();
        return result;
    }

    bool isAlive(const string &ip, int port) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        timeval timeout{2, 0};
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        bool alive = (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0);
        close(sock);
        return alive;
    }

    void handleDeadNode(const string &message) {
        vector<string> parts;
        stringstream ss(message);
        string part;
        while (getline(ss, part, ':')) {
            parts.push_back(part);
        }
        if (parts.size() < 5) return;

        string dead_ip = parts[1];
        int dead_port = stoi(parts[2]);
        string reporter = parts[4];

        if (!isAlive(dead_ip, dead_port)) {
            string key = dead_ip + ":" + to_string(dead_port);
            {
                lock_guard<mutex> lock(peers_mtx);
                peer_list.erase(key);
                if (peer_sockets.count(key)) {
                    close(peer_sockets[key]);
                    peer_sockets.erase(key);
                }
            }
            log_file << getCurrentTimestamp() << " - Removed dead node: " << key 
                    << " (Reported by: " << reporter << ")" << endl;
        }
    }

    void handleClient(int client_fd, sockaddr_in client_addr) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));

        // Send peer list (rename local variable to avoid shadowing)
        string peer_list_str = generatePowerLawPeerList();
        send(client_fd, peer_list_str.c_str(), peer_list_str.size(), 0);

        // Receive peer's listening port
        char buffer[1024];
        int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            close(client_fd);
            return;
        }
        buffer[bytes] = '\0';
        int port = stoi(buffer);
        string key = string(ip_str) + ":" + to_string(port);

        {
            lock_guard<mutex> lock(peers_mtx);
            // Use the member variable "peer_list"
            peer_list[key] = port;
            peer_sockets[key] = client_fd;
        }
        log_file << getCurrentTimestamp() << " - Registered peer: " << key << endl;

        // Handle dead node reports
        while (true) {
            bytes = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            buffer[bytes] = '\0';
            string msg(buffer);
            if (msg.find("Dead Node:") == 0) {
                handleDeadNode(msg);
            }
        }

        {
            lock_guard<mutex> lock(peers_mtx);
            peer_list.erase(key);
            peer_sockets.erase(key);
        }
        close(client_fd);
    }

public:
    SeedNode(string ip, int port) : self_ip(ip), self_port(port) {
        log_file.open("seed_" + ip + "_" + to_string(port) + ".log", ios::app);
    }

    void start() {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            log_file << "Error creating socket" << endl;
            return;
        }

        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(self_port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            log_file << "Binding failed" << endl;
            close(sockfd);
            return;
        }

        listen(sockfd, SOMAXCONN);
        log_file << getCurrentTimestamp() << " - Seed started on " << self_ip << ":" << self_port << endl;

        while (true) {
            sockaddr_in client_addr{};
            socklen_t len = sizeof(client_addr);
            int client_fd = accept(sockfd, (sockaddr*)&client_addr, &len);
            if (client_fd < 0) continue;

            thread([this, client_fd, client_addr]() {
                handleClient(client_fd, client_addr);
            }).detach();
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cout << "Usage: " << argv[0] << " <IP> <PORT>" << endl;
        return 1;
    }
    SeedNode seed(argv[1], atoi(argv[2]));
    seed.start();
    return 0;
}
