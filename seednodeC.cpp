#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <chrono>
#include <random>

using namespace std;

// Helper function to create a composite key from IP and port.
string makeKey(const string &ip, int port) {
    return ip + ":" + to_string(port);
}

class SeedNode {
private:
    // Use unordered_map with composite key ("ip:port") so that multiple peers
    // from the same IP can be uniquely registered.
    unordered_map<string, int> peer_list;     // Composite key → port (redundant but kept for convenience)
    unordered_map<string, int> peer_sockets;    // Composite key → socket FD
    mutex mtx;
    ofstream log_file;
    string self_ip;
    int self_port;

    // Function to generate power-law distributed random numbers.
    vector<int> powerLawDistribution(int n, double alpha = 2.5) {
        random_device rd;
        mt19937 gen(rd());
        uniform_real_distribution<> dis(0, 1);

        vector<int> degrees;
        for (int i = 0; i < n; i++) {
            double u = dis(gen);
            int degree = ceil(pow(((1 - u) * (pow(n, 1 - alpha) - pow(1, 1 - alpha)) + pow(1, 1 - alpha)), 1 / (1 - alpha)));
            degrees.push_back(min(degree, n - 1));
        }
        return degrees;
    }

    // Generate peer list following power-law distribution.
    // The output is a comma-separated string of "IP port" pairs.
    string generatePowerLawPeerList() {
        vector<pair<string, int>> peers;
        {
            lock_guard<mutex> lock(mtx);
            for (const auto& entry : peer_list) {
                // entry.first is composite key "ip:port"
                peers.push_back({entry.first, entry.second});
            }
        }
        if (peers.empty())
            return "";

        vector<int> degrees = powerLawDistribution(peers.size());
        string peer_list_msg;
        for (size_t i = 0; i < peers.size(); i++) {
            // Split the composite key back into ip and port.
            size_t pos = peers[i].first.find(":");
            string ip = peers[i].first.substr(0, pos);
            string portStr = peers[i].first.substr(pos + 1);
            // Append the peer info according to its power-law degree.
            for (int j = 0; j < degrees[i]; j++) {
                peer_list_msg += ip + " " + portStr + ",";
            }
        }
        return peer_list_msg;
    }

    // Check if a peer is alive using our custom ping functionality.
    // The dead node message provides IP and port so we build the composite key.
    bool isAlive(const string& ip, int port) {
        string key = makeKey(ip, port);
        {
            lock_guard<mutex> lock(mtx);
            if (peer_sockets.find(key) == peer_sockets.end()) {
                return false;
            }
        }
        int sockfd = peer_sockets[key];
        string ping_msg = "PING";
        if (send(sockfd, ping_msg.c_str(), ping_msg.length(), MSG_NOSIGNAL) < 0) {
            return false;
        }

        char buffer[10] = {0};
        struct timeval timeout = {2, 0};
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        int bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
        return (bytes_received > 0 && string(buffer) == "PONG");
    }

    // Process a "Dead Node:" message. Uses composite key built from dead node IP and port.
    void handleDeadNode(const string& message) {
        istringstream iss(message);
        string token;
        vector<string> tokens;
        while (getline(iss, token, ':')) {
            tokens.push_back(token);
        }
        if (tokens.size() < 5) {
            log_file << getCurrentTimestamp() << " - Invalid dead node message format: " << message << endl;
            return;
        }
        string dead_ip = tokens[1];
        int dead_port = stoi(tokens[2]);
        string reporting_ip = tokens[4];

        // Double-check that the node is really dead.
        if (!isAlive(dead_ip, dead_port)) {
            string key = makeKey(dead_ip, dead_port);
            lock_guard<mutex> lock(mtx);
            // Erase all entries with the composite key.
            peer_list.erase(key);
            if (peer_sockets.find(key) != peer_sockets.end()) {
                close(peer_sockets[key]);
                peer_sockets.erase(key);
            }
            log_file << getCurrentTimestamp() << " - Dead node removed: " << dead_ip << ":" << dead_port 
                     << " (Reported by: " << reporting_ip << ")" << endl;
        } else {
            log_file << getCurrentTimestamp() << " - False alarm: " << dead_ip << " is still alive. "
                     << "Reported by: " << reporting_ip << endl;
        }
    }

    string getCurrentTimestamp() {
        auto now = chrono::system_clock::now();
        auto time = chrono::system_clock::to_time_t(now);
        string timestamp = ctime(&time);
        timestamp.pop_back(); // Remove newline
        return timestamp;
    }

    // Handle an incoming client connection.
    void handleClient(int client_socket_fd, sockaddr_in client_addr) {
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);
        // Build composite key.
        string key = makeKey(client_ip, client_port);

        // Send power-law distributed peer list.
        string peer_list_msg = generatePowerLawPeerList();
        if (send(client_socket_fd, peer_list_msg.c_str(), peer_list_msg.length(), 0) < 0) {
            log_file << getCurrentTimestamp() << " - Error sending peer list to " << client_ip << endl;
            close(client_socket_fd);
            return;
        }

        {
            lock_guard<mutex> lock(mtx);
            // Register new peer using the composite key.
            peer_list[key] = client_port;
            peer_sockets[key] = client_socket_fd;
            log_file << getCurrentTimestamp() << " - New peer registered: " << client_ip << ":" << client_port << endl;
        }

        // Set socket timeout.
        struct timeval timeout = {5, 0};
        setsockopt(client_socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        // Handle incoming messages.
        char buffer[1024];
        while (true) {
            memset(buffer, 0, sizeof(buffer));
            int bytes_received = recv(client_socket_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received <= 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    lock_guard<mutex> lock(mtx);
                    peer_list.erase(key);
                    peer_sockets.erase(key);
                    log_file << getCurrentTimestamp() << " - Peer disconnected: " << client_ip << ":" << client_port << endl;
                    break;
                }
                continue;
            }
            string message(buffer);
            if (message.find("Dead Node:") == 0) {
                handleDeadNode(message);
            }
        }
        close(client_socket_fd);
    }

public:
    SeedNode(const string& ip, int port) : self_ip(ip), self_port(port) {
        log_file.open("seed_" + ip + "_" + to_string(port) + ".log", ios::app);
    }

    void start() {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            log_file << getCurrentTimestamp() << " - Error creating socket" << endl;
            return;
        }
        int opt = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            log_file << getCurrentTimestamp() << " - Error setting socket options" << endl;
            return;
        }
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(self_port);
        server_addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            log_file << getCurrentTimestamp() << " - Binding failed" << endl;
            return;
        }
        if (listen(sockfd, SOMAXCONN) < 0) {
            log_file << getCurrentTimestamp() << " - Listening failed" << endl;
            return;
        }
        log_file << getCurrentTimestamp() << " - Seed node started on port " << self_port << endl;
        while (true) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket_fd = accept(sockfd, (sockaddr*)&client_addr, &client_len);
            if (client_socket_fd < 0) {
                log_file << getCurrentTimestamp() << " - Error accepting client connection" << endl;
                continue;
            }
            thread t(&SeedNode::handleClient, this, client_socket_fd, client_addr);
            t.detach();
        }
    }
};

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cout << "Usage: " << argv[0] << " <ip> <port>" << endl;
        return 1;
    }
    string ip = argv[1];
    int port = atoi(argv[2]);
    SeedNode seed(ip, port);
    seed.start();
    return 0;
}
