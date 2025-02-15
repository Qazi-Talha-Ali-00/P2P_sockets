#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <random>
#include <unordered_set>

using namespace std;

class PeerNode {
private:
    string self_ip;
    int self_port;
    ofstream log_file;
    unordered_map<string, int> connected_peers;  // ip:port -> port
    unordered_map<string, int> seed_connections; // ip -> socket FD
    unordered_map<string, int> connected_fd;
    unordered_set<string> ml;  // Message List
    mutex ml_mtx;
    mutex file;
    mutex peers_mtx;
    mutex seeds_mtx;

    string getCurrentTimestamp() {
        auto now = chrono::system_clock::now();
        auto time = chrono::system_clock::to_time_t(now);
        string timestamp = ctime(&time);
        timestamp.pop_back();
        return timestamp;
    }

    string makeKey(const string &ip, int port) {
        return ip + ":" + to_string(port);
    }

    vector<string> split(const string &s, char delimiter) {
        vector<string> tokens;
        string token;
        istringstream tokenStream(s);
        while (getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    void connectToPeers(const map<string, int> &peer_freq) {
        vector<string> candidates;
        for (const auto &entry : peer_freq) {
            for (int i = 0; i < entry.second; i++) {
                candidates.push_back(entry.first);
            }
        }

        if (candidates.empty()) return;

        // Generate own degree using power-law
        random_device rd;
        mt19937 gen(rd());
        uniform_real_distribution<> dis(0.0, 1.0);
        double u = dis(gen);
        int degree = ceil(pow(1 - u, -1.0/(2.5 - 1)));
        degree = min(degree, (int)candidates.size());
        degree = max(degree, 1);

        shuffle(candidates.begin(), candidates.end(), gen);
        
        for (int i = 0; i < degree && i < (int)candidates.size(); i++) {
            vector<string> parts = split(candidates[i], ':');
            if (parts.size() != 2) continue;
            
            string ip = parts[0];
            int port = stoi(parts[1]);
            string key = makeKey(ip, port);

            if (key == makeKey(self_ip, self_port)) continue;

            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) continue;

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

            if (connect(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
                close(sockfd);
                continue;
            }

            {
                lock_guard<mutex> lock(peers_mtx);
                if (connected_peers.find(key) == connected_peers.end()) {
                    connected_peers[key] = port;
                    connected_fd[key] = sockfd;
                    log_file << getCurrentTimestamp() << " - Connected to peer: " << key << endl;
                }
            }
        }
    }

    void handleIncomingMessages() {
        while (true) {
            vector<int> fds;
            {
                lock_guard<mutex> lock(peers_mtx);
                for (auto &[_, fd] : connected_fd) fds.push_back(fd);
            }

            fd_set read_set;
            FD_ZERO(&read_set);
            int max_fd = 0;
            for (int fd : fds) {
                FD_SET(fd, &read_set);
                if (fd > max_fd) max_fd = fd;
            }

            timeval timeout{1, 0};
            select(max_fd + 1, &read_set, nullptr, nullptr, &timeout);

            for (int fd : fds) {
                if (!FD_ISSET(fd, &read_set)) continue;

                char buffer[1024];
                int len = recv(fd, buffer, sizeof(buffer) - 1, 0);
                if (len <= 0) continue;
                buffer[len] = '\0';
                string msg(buffer);

                // Use fully qualified std::hash to avoid parsing issues.
                string msg_hash = to_string((std::hash<string>{})(msg));
                bool exists;
                {
                    lock_guard<mutex> lock(ml_mtx);
                    exists = ml.count(msg_hash);
                    if (!exists) {
                        ml.insert(msg_hash);
                        log_file << getCurrentTimestamp() << " - Received: " << msg << " from " << fd << endl;
                    }
                }

                if (!exists) {
                    lock_guard<mutex> lock(peers_mtx);
                    for (auto &[key, dest_fd] : connected_fd) {
                        if (dest_fd != fd) {
                            send(dest_fd, msg.c_str(), msg.size(), 0);
                        }
                    }
                }
            }
        }
    }

    void generateAndBroadcastMessages() {
        int msg_count = 0;
        while (msg_count < 10) {
            this_thread::sleep_for(chrono::seconds(5));
            string msg = "Hello from " + self_ip + ":" + to_string(self_port) + " - Message " + to_string(msg_count+1);
            // Use fully qualified std::hash here as well.
            string msg_hash = to_string((std::hash<string>{})(msg));
            {
                lock_guard<mutex> lock(ml_mtx);
                if (!ml.count(msg_hash)) {
                    ml.insert(msg_hash);
                    log_file << getCurrentTimestamp() << " - Broadcasting: " << msg << endl;
                }
            }

            {
                lock_guard<mutex> lock(peers_mtx);
                for (auto &[key, fd] : connected_fd) {
                    send(fd, msg.c_str(), msg.size(), 0);
                }
            }
            msg_count++;
        }
    }

    void pingPeers() {
        unordered_map<string, int> failures;
        while (true) {
            this_thread::sleep_for(chrono::seconds(13));
            {
                lock_guard<mutex> lock(peers_mtx);
                for (auto &[key, _] : connected_peers) {
                    vector<string> parts = split(key, ':');
                    if (parts.size() != 2) continue;
                    
                    string ip = parts[0];
                    int port = stoi(parts[1]);

                    int sock = socket(AF_INET, SOCK_STREAM, 0);
                    if (sock < 0) continue;

                    sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(port);
                    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

                    timeval timeout{2, 0};
                    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

                    bool alive = false;
                    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
                        if (send(sock, "PING", 4, 0) > 0) {
                            char buf[4];
                            if (recv(sock, buf, 4, 0) > 0) {
                                alive = true;
                            }
                        }
                    }
                    close(sock);

                    if (!alive) failures[key]++;
                    else failures[key] = 0;
                }
            }

            for (auto it = failures.begin(); it != failures.end();) {
                if (it->second >= 3) {
                    string dead_msg = "Dead Node:" + it->first + ":" + getCurrentTimestamp() + ":" + self_ip + ":" + to_string(self_port);
                    {
                        lock_guard<mutex> lock(seeds_mtx);
                        for (auto &[_, fd] : seed_connections) {
                            send(fd, dead_msg.c_str(), dead_msg.size(), 0);
                        }
                    }
                    {
                        lock_guard<mutex> lock(peers_mtx);
                        connected_peers.erase(it->first);
                        if (connected_fd.count(it->first)) {
                            close(connected_fd[it->first]);
                            connected_fd.erase(it->first);
                        }
                    }
                    log_file << getCurrentTimestamp() << " - Reported dead node: " << it->first << endl;
                    it = failures.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    void acceptPeerConnections() {
        int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_sock < 0) {
            log_file << "Error creating listening socket" << endl;
            return;
        }

        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(self_port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            log_file << "Binding failed" << endl;
            close(listen_sock);
            return;
        }

        listen(listen_sock, SOMAXCONN);
        log_file << "Listening for peers on " << self_ip << ":" << self_port << endl;

        while (true) {
            sockaddr_in client_addr{};
            socklen_t len = sizeof(client_addr);
            int client_fd = accept(listen_sock, (sockaddr*)&client_addr, &len);
            if (client_fd < 0) continue;

            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
            
            char buffer[1024];
            int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                int port = stoi(buffer);
                string key = makeKey(ip_str, port);

                {
                    lock_guard<mutex> lock(peers_mtx);
                    if (connected_peers.find(key) == connected_peers.end()) {
                        connected_peers[key] = port;
                        connected_fd[key] = client_fd;
                        log_file << "Accepted connection from " << key << endl;
                    }
                }
            }
        }
        close(listen_sock);
    }

public:
    PeerNode(string ip, int port) : self_ip(ip), self_port(port) {
        log_file.open("peer_" + ip + "_" + to_string(port) + ".log", ios::app);
    }

    void start() {
        thread(&PeerNode::acceptPeerConnections, this).detach();

        // Connect to seeds
        ifstream config("Config.txt");
        vector<pair<string, int>> seeds;
        string line;
        while (getline(config, line)) {
            istringstream iss(line);
            string ip;
            int port;
            iss >> ip >> port;
            seeds.emplace_back(ip, port);
        }

        int n = seeds.size();
        int required = (n / 2) + 1;
        shuffle(seeds.begin(), seeds.end(), mt19937(random_device{}()));

        map<string, int> peer_freq;
        for (int i = 0; i < required && i < (int)seeds.size(); i++) {
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) continue;

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(seeds[i].second);
            inet_pton(AF_INET, seeds[i].first.c_str(), &addr.sin_addr);

            if (connect(sockfd, (sockaddr*)&addr, sizeof(addr)) >= 0) {
                {
                    lock_guard<mutex> lock(seeds_mtx);
                    seed_connections[seeds[i].first] = sockfd;
                }

                // Get peer list from seed
                char buffer[4096];
                int bytes = recv(sockfd, buffer, sizeof(buffer), 0);
                if (bytes > 0) {
                    string peer_list_str(buffer, bytes);
                    istringstream iss(peer_list_str);
                    string entry;
                    while (getline(iss, entry, ',')) {
                        if (entry.empty()) continue;
                        vector<string> parts = split(entry, ' ');
                        if (parts.size() != 2) continue;
                        string key = parts[0] + ":" + parts[1];
                        peer_freq[key]++;
                    }
                }

                // Send our listening port
                string port_str = to_string(self_port);
                send(sockfd, port_str.c_str(), port_str.size(), 0);
            }
        }

        connectToPeers(peer_freq);

        thread(&PeerNode::generateAndBroadcastMessages, this).detach();
        thread(&PeerNode::handleIncomingMessages, this).detach();
        thread(&PeerNode::pingPeers, this).detach();

        // Use chrono literals; if not enabled, add: using namespace std::chrono_literals;
        while (true) this_thread::sleep_for(chrono::hours(1));
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cout << "Usage: " << argv[0] << " <IP> <PORT>" << endl;
        return 1;
    }
    string ip = argv[1];
    int port = atoi(argv[2]);
    
    PeerNode peer(ip, port);
    peer.start();
    return 0;
}
