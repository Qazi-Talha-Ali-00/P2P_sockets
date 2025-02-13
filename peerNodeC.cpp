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

using namespace std;

struct MessageEntry {
    string hash;
    set<string> sentTo;
    set<string> receivedFrom;
};

class PeerNode {
private:
    string self_ip;
    int self_port;
    int msg_count = 0;
    unordered_map<string, int> connected_peers;  // IP -> Port
    unordered_map<string, int> seed_connections; // IP -> Socket FD
    unordered_map<string, int> peer_connections; // IP -> Socket FD
    unordered_map<string, MessageEntry> message_list;
    mutex mtx;
    ofstream log_file;

    string generateMessage() {
        auto now = chrono::system_clock::now();
        auto timestamp = chrono::system_clock::to_time_t(now);
        return to_string(timestamp) + ":" + self_ip + ":" + to_string(msg_count++);
    }

    string calculateHash(const string& message) {
        hash<string> hasher;
        return to_string(hasher(message));
    }

    void pingPeers() {
        while (true) {
            this_thread::sleep_for(chrono::seconds(13));
            vector<string> dead_peers;
            
            mtx.lock();
            for (const auto& peer : peer_connections) {
                int failed_attempts = 0;
                string peer_ip = peer.first;
                
                for (int i = 0; i < 3; i++) {
                    string cmd = "ping -c 1 -W 2 " + peer_ip;
                    if (system((cmd + " >/dev/null 2>&1").c_str()) != 0) {
                        failed_attempts++;
                    }
                }

                if (failed_attempts == 3) {
                    dead_peers.push_back(peer_ip);
                }
            }
            mtx.unlock();

            for (const string& dead_peer : dead_peers) {
                auto now = chrono::system_clock::now();
                auto timestamp = chrono::system_clock::to_time_t(now);
                string dead_msg = "Dead Node:" + dead_peer + ":" + 
                                to_string(connected_peers[dead_peer]) + ":" +
                                to_string(timestamp) + ":" + self_ip;
                
                mtx.lock();
                // Notify seeds
                for (const auto& seed : seed_connections) {
                    send(seed.second, dead_msg.c_str(), dead_msg.length(), 0);
                }
                
                // Cleanup
                close(peer_connections[dead_peer]);
                peer_connections.erase(dead_peer);
                connected_peers.erase(dead_peer);
                
                log_file << "Dead node detected: " << dead_peer << endl;
                mtx.unlock();
            }
        }
    }

    void generateAndBroadcastMessages() {
        for (int i = 0; i < 10; i++) {
            this_thread::sleep_for(chrono::seconds(5));
            string message = generateMessage();
            string msg_hash = calculateHash(message);
            
            mtx.lock();
            message_list[msg_hash] = MessageEntry{msg_hash, {}, {}};
            
            // Broadcast to peers
            for (const auto& peer : peer_connections) {
                send(peer.second, message.c_str(), message.length(), 0);
                message_list[msg_hash].sentTo.insert(peer.first);
            }
            
            log_file << "Generated and broadcast message: " << message << endl;
            mtx.unlock();
        }
    }

    void handleIncomingMessages() {
        while (true) {
            mtx.lock();
            auto connections = peer_connections;
            mtx.unlock();

            for (const auto& peer : connections) {
                char buffer[1024] = {0};
                int bytes_read = recv(peer.second, buffer, sizeof(buffer), MSG_DONTWAIT);
                
                if (bytes_read > 0) {
                    string message(buffer, bytes_read);
                    string msg_hash = calculateHash(message);
                    
                    mtx.lock();
                    if (!message_list.count(msg_hash)) {
                        message_list[msg_hash] = MessageEntry{msg_hash, {}, {peer.first}};
                        log_file << "Received new message: " << message << " from " << peer.first << endl;
                        
                        // Forward to other peers
                        for (const auto& fwd_peer : peer_connections) {
                            if (fwd_peer.first != peer.first) {
                                send(fwd_peer.second, message.c_str(), message.length(), 0);
                                message_list[msg_hash].sentTo.insert(fwd_peer.first);
                            }
                        }
                    }
                    mtx.unlock();
                }
            }
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }

    // Connect to selected peers with power-law distribution
    void connectToPeers(const map<string, int>& peer_freq) {
        vector<pair<string, int>> candidates(peer_freq.begin(), peer_freq.end());
        
        // Weighted random selection based on frequency
        random_device rd;
        mt19937 gen(rd());
        vector<double> weights;
        for (const auto& p : candidates) weights.push_back(p.second);
        discrete_distribution<> dist(weights.begin(), weights.end());

        set<string> selected;
        while (selected.size() < min(10, (int)candidates.size())) { // Connect to up to 10 peers
            int idx = dist(gen);
            selected.insert(candidates[idx].first);
        }

        // Establish TCP connections
        for (const string& peer_ip : selected) {
            int peer_port = connected_peers[peer_ip];
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in peer_addr;
            peer_addr.sin_family = AF_INET;
            peer_addr.sin_port = htons(peer_port);
            peer_addr.sin_addr.s_addr = inet_addr(peer_ip.c_str());

            if (connect(sockfd, (sockaddr*)&peer_addr, sizeof(peer_addr)) >= 0) {
                mtx.lock();
                peer_connections[peer_ip] = sockfd;
                mtx.unlock();
            }
        }
    }

public:
    PeerNode(const string& ip, int port) : self_ip(ip), self_port(port) {
        log_file.open("peer_" + ip + "_" + to_string(port) + ".log");
    }

    void start() {
        // Read seed nodes from config
        ifstream config("Config.txt");
        vector<pair<string, int>> seed_nodes;
        string line;
        while (getline(config, line)) {
            stringstream ss(line);
            string ip; int port;
            ss >> ip >> port;
            seed_nodes.push_back({ip, port});
        }

        // Connect to required seeds
        int n = seed_nodes.size();
        int required_seeds = (n / 2) + 1;
        shuffle(seed_nodes.begin(), seed_nodes.end(), mt19937(random_device{}()));
        
        for (int i = 0; i < required_seeds; i++) {
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in seed_addr;
            seed_addr.sin_family = AF_INET;
            seed_addr.sin_port = htons(seed_nodes[i].second);
            seed_addr.sin_addr.s_addr = inet_addr(seed_nodes[i].first.c_str());

            if (connect(sockfd, (sockaddr*)&seed_addr, sizeof(seed_addr)) >= 0) {
                seed_connections[seed_nodes[i].first] = sockfd;
                
                // Receive peer list
                char buffer[4096] = {0};
                recv(sockfd, buffer, sizeof(buffer), 0);
                string peer_list(buffer);
                
                // Count peer frequencies (power-law distribution)
                map<string, int> peer_freq;
                stringstream ss(peer_list);
                string peer_entry;
                while (getline(ss, peer_entry, ',')) {
                    if (peer_entry.empty()) continue;
                    stringstream ps(peer_entry);
                    string ip; int port;
                    ps >> ip >> port;
                    peer_freq[ip]++;  // Frequency = degree
                    connected_peers[ip] = port;
                }
                
                // Connect to peers
                connectToPeers(peer_freq);
            }
        }

        // Start threads
        thread ping_thread(&PeerNode::pingPeers, this);
        thread msg_thread(&PeerNode::generateAndBroadcastMessages, this);
        thread recv_thread(&PeerNode::handleIncomingMessages, this);

        ping_thread.detach();
        msg_thread.detach();
        recv_thread.detach();

        while (true) this_thread::sleep_for(chrono::seconds(1));
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cout << "Usage: " << argv[0] << " <ip> <port>" << endl;
        return 1;
    }

    PeerNode peer(argv[1], atoi(argv[2]));
    peer.start();
    return 0;
}