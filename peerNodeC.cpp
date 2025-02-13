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

struct MessageEntry
{
    string hash;
    set<string> sentTo;
    set<string> receivedFrom;
};

class PeerNode
{
private:
    string self_ip;
    int self_port;
    int msg_count = 0;
    // Stores peers discovered via seed responses (IP -> Port)
    unordered_map<string, int> connected_peers;  // Composite key (IP:Port) → Port (redundant but kept)
    unordered_map<string, int> seed_connections; // Composite key (IP:Port) → Socket FD
    unordered_map<string, int> peer_connections; // Composite key (IP:Port) → Socket FD
    unordered_map<string, int> ping_failures;    // Composite key (IP:Port)
    unordered_map<string, bool> ping_received;   // Composite key (IP:Port)

    unordered_map<string, MessageEntry> message_list;
    mutex mtx;
    ofstream log_file;

    string generateMessage()
    {
        auto now = chrono::system_clock::now();
        auto timestamp = chrono::system_clock::to_time_t(now);
        return to_string(timestamp) + ":" + self_ip + ":" + to_string(msg_count++);
    }

    string calculateHash(const string &message)
    {
        hash<string> hasher;
        return to_string(hasher(message));
    }

    // Custom ping functionality:
    // For each peer connection, send a "PING" message,
    // wait for a short period for a "PONG" response (handled in handleIncomingMessages),
    // and update a failure counter. If a peer fails 3 rounds, it is marked dead.
    string makeKey(const string &ip, int port)
    {
        return ip + ":" + to_string(port);
    }
    void pingPeers()
    {
        while (true)
        {
            // Wait before starting a ping round.
            this_thread::sleep_for(chrono::seconds(13));
            vector<string> dead_peers;

            {
                lock_guard<mutex> lock(mtx);
                // For each peer, send a "PING" message and mark as not received.
                for (auto &p : peer_connections)
                {
                    send(p.second, "PING", 4, 0);
                    ping_received[p.first] = false;
                }
            }
            // Wait 2 seconds for responses.
            this_thread::sleep_for(chrono::seconds(2));

            {
                lock_guard<mutex> lock(mtx);
                for (auto &p : peer_connections)
                {
                    if (!ping_received[p.first])
                    {
                        ping_failures[p.first]++;
                        log_file << "No PONG received from " << p.first
                                 << ". Failure count: " << ping_failures[p.first] << endl;
                        if (ping_failures[p.first] >= 3)
                        {
                            dead_peers.push_back(p.first);
                        }
                    }
                    else
                    {
                        ping_failures[p.first] = 0; // Reset on success.
                    }
                    // Reset flag for next round.
                    ping_received[p.first] = false;
                }
            }

            // Handle dead peers.
            for (const string &dead_peer : dead_peers)
            {
                auto now = chrono::system_clock::now();
                auto timestamp = chrono::system_clock::to_time_t(now);
                string dead_msg = "Dead Node:" + dead_peer + ":" +
                                  to_string(connected_peers[dead_peer]) + ":" +
                                  to_string(timestamp) + ":" + self_ip;
                {
                    lock_guard<mutex> lock(mtx);
                    // Notify all connected seeds about the dead node.
                    for (const auto &seed : seed_connections)
                    {
                        send(seed.second, dead_msg.c_str(), dead_msg.length(), 0);
                    }
                    if (peer_connections.count(dead_peer))
                    {
                        close(peer_connections[dead_peer]);
                        peer_connections.erase(dead_peer);
                        ping_failures.erase(dead_peer);
                        ping_received.erase(dead_peer);
                    }
                    connected_peers.erase(dead_peer);
                    log_file << "Dead node detected: " << dead_peer << endl;
                }
            }
        }
    }

    void generateAndBroadcastMessages()
    {
        for (int i = 0; i < 10; i++)
        {
            this_thread::sleep_for(chrono::seconds(5));
            string message = generateMessage();
            string msg_hash = calculateHash(message);

            {
                lock_guard<mutex> lock(mtx);
                message_list[msg_hash] = MessageEntry{msg_hash, {}, {}};
                // Broadcast the message to all connected peers.
                for (const auto &peer : peer_connections)
                {
                    send(peer.second, message.c_str(), message.length(), 0);
                    message_list[msg_hash].sentTo.insert(peer.first);
                }
                log_file << "Generated and broadcast message: " << message << endl;
            }
        }
    }

    // This function continuously reads messages from each peer.
    // It distinguishes between PING/PONG (for liveness) and gossip messages.
    void handleIncomingMessages()
    {
        while (true)
        {
            vector<pair<string, int>> connections;
            {
                lock_guard<mutex> lock(mtx);
                for (auto &peer : peer_connections)
                {
                    connections.push_back({peer.first, peer.second});
                }
            }

            for (const auto &peer : connections)
            {
                char buffer[1024] = {0};
                int bytes_read = recv(peer.second, buffer, sizeof(buffer), MSG_DONTWAIT);
                if (bytes_read > 0)
                {
                    string message(buffer, bytes_read);
                    if (message == "PING")
                    {
                        send(peer.second, "PONG", 4, 0);
                    }
                    else if (message == "PONG")
                    {
                        lock_guard<mutex> lock(mtx);
                        ping_received[peer.first] = true; // peer.first is composite key
                    }
                    else
                    {
                        string msg_hash = calculateHash(message);
                        {
                            lock_guard<mutex> lock(mtx);
                            if (!message_list.count(msg_hash))
                            {
                                message_list[msg_hash] = MessageEntry{msg_hash, {}, {peer.first}};
                                log_file << "Received message from " << peer.first << endl;
                                for (const auto &fwd_peer : peer_connections)
                                {
                                    if (fwd_peer.first != peer.first)
                                    {
                                        send(fwd_peer.second, message.c_str(), message.length(), 0);
                                        message_list[msg_hash].sentTo.insert(fwd_peer.first);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }

    // Weighted selection and connection to a subset of peers.
    void connectToPeers(const map<string, int> &peer_freq)
    {
        vector<pair<string, int>> candidates; // Store composite keys (IP:Port) and frequency
        for (const auto &entry : peer_freq)
        {
            // entry.first is composite key (IP:Port)
            candidates.push_back({entry.first, entry.second});
        }

        random_device rd;
        mt19937 gen(rd());
        vector<double> weights;
        for (const auto &p : candidates)
            weights.push_back(p.second);
        discrete_distribution<> dist(weights.begin(), weights.end());

        set<string> selected;
        int num_to_connect = min(10, (int)candidates.size());
        while ((int)selected.size() < num_to_connect)
        {
            int idx = dist(gen);
            selected.insert(candidates[idx].first);
        }

        // Connect to selected peers
        for (const string &composite_key : selected)
        {
            // Split composite key into IP and Port
            size_t pos = composite_key.find(":");
            string ip = composite_key.substr(0, pos);
            int port = stoi(composite_key.substr(pos + 1));

            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in peer_addr;
            peer_addr.sin_family = AF_INET;
            peer_addr.sin_port = htons(port);
            peer_addr.sin_addr.s_addr = inet_addr(ip.c_str());

            if (connect(sockfd, (sockaddr *)&peer_addr, sizeof(peer_addr)) >= 0)
            {
                lock_guard<mutex> lock(mtx);
                peer_connections[composite_key] = sockfd;
                ping_failures[composite_key] = 0;
                ping_received[composite_key] = false;
                log_file << "Connected to peer: " << composite_key << endl;
            }
        }
    }

    // Accept incoming connections from peers.
    void acceptPeerConnections()
    {
        int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_sock < 0)
        {
            cerr << "Error creating listening socket" << endl;
            return;
        }

        int opt = 1;
        if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            cerr << "Error setting socket options" << endl;
            close(listen_sock);
            return;
        }

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(self_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(listen_sock, (sockaddr *)&addr, sizeof(addr)) < 0)
        {
            cerr << "Error binding listening socket" << endl;
            close(listen_sock);
            return;
        }

        if (listen(listen_sock, 10) < 0)
        {
            cerr << "Error listening on socket" << endl;
            close(listen_sock);
            return;
        }

        {
            lock_guard<mutex> lock(mtx);
            log_file << "Peer listening for incoming connections on port " << self_port << endl;
        }

        while (true)
        {
            sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            int client_sock = accept(listen_sock, (sockaddr *)&client_addr, &addrlen);
            if (client_sock < 0)
                continue;

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            int client_port = ntohs(client_addr.sin_port); // Get peer's port
            string composite_key = makeKey(client_ip, client_port);

            {
                lock_guard<mutex> lock(mtx);
                if (peer_connections.find(composite_key) == peer_connections.end())
                {
                    peer_connections[composite_key] = client_sock;
                    ping_failures[composite_key] = 0;
                    ping_received[composite_key] = false;
                    log_file << "Accepted connection from peer: " << composite_key << endl;
                }
            }
        }
    }

public:
    PeerNode(const string &ip, int port) : self_ip(ip), self_port(port)
    {
        log_file.open("peer_" + ip + "_" + to_string(port) + ".log", ios::app);
        if (!log_file)
        {
            cerr << "Failed to open log file" << endl;
        }
    }

    void start()
    {
        // Start a thread to accept incoming peer connections.
        thread accept_thread(&PeerNode::acceptPeerConnections, this);
        accept_thread.detach();

        // Read seed nodes from configuration.
        ifstream config("Config.txt");
        vector<pair<string, int>> seed_nodes;
        string line;
        while (getline(config, line))
        {
            stringstream ss(line);
            string ip;
            int port;
            ss >> ip >> port;
            seed_nodes.push_back({ip, port});
        }

        // Connect to ⌊(n/2)⌋+1 randomly chosen seed nodes.
        int n = seed_nodes.size();
        int required_seeds = (n / 2) + 1;
        shuffle(seed_nodes.begin(), seed_nodes.end(), mt19937(random_device{}()));

        // Aggregate peer lists (with frequency count) from seed responses.
        map<string, int> aggregated_peer_freq;
        for (int i = 0; i < required_seeds && i < (int)seed_nodes.size(); i++)
        {
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0)
                continue;
            sockaddr_in seed_addr;
            seed_addr.sin_family = AF_INET;
            seed_addr.sin_port = htons(seed_nodes[i].second);
            seed_addr.sin_addr.s_addr = inet_addr(seed_nodes[i].first.c_str());

            if (connect(sockfd, (sockaddr *)&seed_addr, sizeof(seed_addr)) >= 0)
            {
                {
                    lock_guard<mutex> lock(mtx);
                    seed_connections[seed_nodes[i].first] = sockfd;
                }

                // Receive the peer list from the seed.
                char buffer[4096] = {0};
                int bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
                if (bytes_received <= 0)
                    continue;
                string peer_list_str(buffer, bytes_received);
                {
                    lock_guard<mutex> lock(mtx);
                    log_file << "Received peer list from seed " << seed_nodes[i].first
                             << ": " << peer_list_str << endl;
                }

                // Process the comma-separated peer list.
                stringstream ss(peer_list_str);
                string peer_entry;
                while (getline(ss, peer_entry, ','))
                {
                    if (peer_entry.empty())
                        continue;
                    stringstream ps(peer_entry);
                    string peer_ip;
                    int peer_port;
                    ps >> peer_ip >> peer_port;
                    string composite_key = makeKey(peer_ip, peer_port);
                    aggregated_peer_freq[composite_key]++; // Track composite keys
                    connected_peers[composite_key] = peer_port;
                }
            }
        }

        // Connect to peers based on the aggregated list.
        connectToPeers(aggregated_peer_freq);

        // Start threads for pinging, generating messages, and handling incoming messages.
        thread ping_thread(&PeerNode::pingPeers, this);
        thread msg_thread(&PeerNode::generateAndBroadcastMessages, this);
        thread recv_thread(&PeerNode::handleIncomingMessages, this);

        ping_thread.detach();
        msg_thread.detach();
        recv_thread.detach();

        // Keep main thread alive.
        while (true)
        {
            this_thread::sleep_for(chrono::seconds(1));
        }
    }
};

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " <ip> <port>" << endl;
        return 1;
    }
    PeerNode peer(argv[1], atoi(argv[2]));
    peer.start();
    return 0;
}
