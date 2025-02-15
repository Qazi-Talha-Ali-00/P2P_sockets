#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <chrono>
#include <random>
#include <thread>
#include <sstream>
#include <cstring>
#include <errno.h>

using namespace std;

class PeerNode {
private:
    string self_ip;
    int self_port;
    set<string> connectedPeers;
    unordered_set<string> ML;
    mutex mlMutex;
    mutex peerMutex;
    ofstream logFile;
    unordered_map<string, int> pingFailures;
    vector<pair<string, int>> seedConnections;  // Maintains persistent connections to seeds

    string getCurrentTimestamp() {
        auto now = chrono::system_clock::now();
        time_t t = chrono::system_clock::to_time_t(now);
        string ts = ctime(&t);
        ts.pop_back();
        return ts;
    }

    void logMessage(const string &msg) {
        string timestamp = getCurrentTimestamp();
        string logMsg = "[" + timestamp + "] " + msg;
        cout << logMsg << endl;
        if (logFile.is_open())
            logFile << logMsg << endl;
    }

    vector<pair<string, int>>  readSeedList() {
        vector<pair<string, int>> seeds;
        ifstream file("Config.txt");
        if (!file) {
            logMessage("[!] Error: Unable to open Config.txt");
            return seeds;
        }
        
        string line;
        while (getline(file, line)) {
            istringstream iss(line);
            string ip;
            string port;
            while (iss >> ip >> port) {
                try {
                    seeds.push_back({ip, stoi(port)});
                } catch (const exception &e) {
                    logMessage("[!] Invalid seed entry: " + ip + " " + port);
                }
            }
        }
        return seeds;
    }

    bool connectToSeed(const string &seedIP, int seedPort, set<string> &seedPeers) {
        try {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                throw runtime_error("Socket creation failed");
            }

            // Set socket timeout
            struct timeval timeout;
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(seedPort);
            if (inet_pton(AF_INET, seedIP.c_str(), &addr.sin_addr) <= 0) {
                close(sock);
                throw runtime_error("Invalid seed IP address");
            }

            if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
                close(sock);
                throw runtime_error("Connection failed");
            }

            // Send our port first
            string portStr = to_string(self_port) + "\n";
            if (send(sock, portStr.c_str(), portStr.length(), 0) < 0) {
                close(sock);
                throw runtime_error("Failed to send port");
            }

            // Receive peer list
            char buffer[4096] = {0};
            int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) {
                close(sock);
                throw runtime_error("Failed to receive peer list");
            }

            buffer[bytes] = '\0';
            istringstream iss(buffer);
            string peer;
            while (getline(iss, peer)) {
                if (!peer.empty() && peer.find(":") != string::npos) {
                    seedPeers.insert(peer);
                }
            }

            // Keep the socket open for future communications
            seedConnections.push_back({seedIP, sock});
            return true;

        } catch (const exception &e) {
            logMessage("[!] Error connecting to seed " + seedIP + ":" + to_string(seedPort) + 
                      " - " + string(e.what()));
            return false;
        }
    }

    void registerWithSeeds() {
        vector<pair<string, int>> seeds = readSeedList();
        if (seeds.empty()) {
            logMessage("[!] No seed nodes found in Config.txt!");
            return;
        }

        // Randomly select ⌊(n/2)⌋ + 1 seeds
        random_device rd;
        mt19937 gen(rd());
        shuffle(seeds.begin(), seeds.end(), gen);
        int numToConnect = (seeds.size() / 2) + 1;
        
        logMessage("[*] Connecting to " + to_string(numToConnect) + " randomly selected seed nodes...");
        
        set<string> allPeers;
        int connectedSeeds = 0;
        
        for (int i = 0; i < numToConnect && i < seeds.size(); i++) {
            set<string> seedPeers;
            if (connectToSeed(seeds[i].first, seeds[i].second, seedPeers)) {
                connectedSeeds++;
                for (const auto &peer : seedPeers) {
                    if (peer != (self_ip + ":" + to_string(self_port))) {
                        allPeers.insert(peer);
                    }
                }
            }
        }

        if (connectedSeeds < numToConnect) {
            logMessage("[!] Warning: Only connected to " + to_string(connectedSeeds) + 
                      " seeds out of required " + to_string(numToConnect));
        }

        // Connect to peers following power-law distribution
        connectToPeers(allPeers);
    }

    void connectToPeers(const set<string> &availablePeers) {
        if (availablePeers.empty()) {
            logMessage("[*] No other peers available to connect to");
            return;
        }

        // Implement power-law distribution for peer selection
        vector<string> peers(availablePeers.begin(), availablePeers.end());
        int numPeers = peers.size();
        
        // Simple power-law implementation: connect to ceil(log(n)) peers
        int connectCount = ceil(log2(numPeers + 1));
        connectCount = min(connectCount, numPeers);

        random_device rd;
        mt19937 gen(rd());
        shuffle(peers.begin(), peers.end(), gen);

        for (int i = 0; i < connectCount; i++) {
            string peer = peers[i];
            size_t pos = peer.find(':');
            if (pos == string::npos) continue;

            string peerIP = peer.substr(0, pos);
            int peerPort;
            try {
                peerPort = stoi(peer.substr(pos + 1));
            } catch (...) {
                continue;
            }

            // Try to establish connection
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) continue;

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(peerPort);
            if (inet_pton(AF_INET, peerIP.c_str(), &addr.sin_addr) <= 0) {
                close(sock);
                continue;
            }

            if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0) {
                lock_guard<mutex> lock(peerMutex);
                connectedPeers.insert(peer);
                logMessage("[+] Connected to peer: " + peer);
            }
            close(sock);
        }
    }

    void forwardGossip(const string &message, const string &excludeIP = "") {
        lock_guard<mutex> lock(peerMutex);
        for (const auto &peer : connectedPeers) {
            size_t pos = peer.find(':');
            if (pos == string::npos) continue;

            string peerIP = peer.substr(0, pos);
            if (peerIP == excludeIP) continue;

            try {
                int peerPort = stoi(peer.substr(pos + 1));
                int sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock < 0) continue;

                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(peerPort);
                inet_pton(AF_INET, peerIP.c_str(), &addr.sin_addr);

                struct timeval timeout;
                timeout.tv_sec = 2;
                timeout.tv_usec = 0;
                setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

                if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0) {
                    send(sock, message.c_str(), message.length(), 0);
                }
                close(sock);
            } catch (...) {
                continue;
            }
        }
    }

// Inside PeerNode::handleGossipConnection
void handleGossipConnection(int clientSocket, const string &senderIP) {
    char buffer[1024] = {0};
    int bytes = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close(clientSocket);
        return;
    }

    buffer[bytes] = '\0';
    string message(buffer, bytes);

    // Handle PING messages
    if (message == "PING\n") {
        send(clientSocket, "PONG\n", 5, 0);
        close(clientSocket);
        return;
    }

    // Existing gossip message handling
    bool isNew = false;
    {
        lock_guard<mutex> lock(mlMutex);
        if (ML.find(message) == ML.end()) {
            ML.insert(message);
            isNew = true;
        }
    }

    if (isNew) {
        string localTime = getCurrentTimestamp();
        logMessage("[Gossip] New message from " + senderIP + ": " + message);
        forwardGossip(message, senderIP);
    }
    close(clientSocket);
}

    void acceptGossipConnections() {
        int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket < 0) {
            logMessage("[!] Failed to create gossip server socket");
            return;
        }

        // Allow socket reuse
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(self_port);
        serverAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
            logMessage("[!] Gossip bind failed");
            close(serverSocket);
            return;
        }

        if (listen(serverSocket, SOMAXCONN) < 0) {
            logMessage("[!] Gossip listen failed");
            close(serverSocket);
            return;
        }

        logMessage("[*] Listening for incoming gossip on port " + to_string(self_port));

        while (true) {
            sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int clientSocket = accept(serverSocket, (sockaddr *)&clientAddr, &addrLen);
            if (clientSocket < 0) continue;

            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
            thread t(&PeerNode::handleGossipConnection, this, clientSocket, string(clientIP));
            t.detach();
        }
    }

    void gossipSender() {
        this_thread::sleep_for(chrono::seconds(2)); // Initial delay to allow connections
        for (int i = 1; i <= 10; i++) {
            string message = getCurrentTimestamp() + ":" + self_ip + ":" + to_string(i);
            {
                lock_guard<mutex> lock(mlMutex);
                ML.insert(message);
            }
            logMessage("[Gossip] Generated: " + message);
            forwardGossip(message);
            this_thread::sleep_for(chrono::seconds(5));
        }
    }

    bool pingPeer(const string &peer) {
        size_t pos = peer.find(':');
        if (pos == string::npos) return false;

        string peerIP = peer.substr(0, pos);
        int peerPort;
        try {
            peerPort = stoi(peer.substr(pos + 1));
        } catch (...) {
            return false;
        }

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;

        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(peerPort);
        inet_pton(AF_INET, peerIP.c_str(), &addr.sin_addr);

        if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
            close(sock);
            return false;
        }

        string pingMsg = "PING\n";
        if (send(sock, pingMsg.c_str(), pingMsg.length(), 0) < 0) {
            close(sock);
            return false;
        }

        char buffer[10] = {0};
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        close(sock);

        if (bytes > 0) {
            buffer[bytes] = '\0';
            return (string(buffer) == "PONG\n");
        }
        return false;
    }

    void notifySeedsDeadNode(const string &deadIP, int deadPort) {
        string deadMsg = "Dead Node:" + deadIP + ":" + to_string(deadPort) + ":" +
                        getCurrentTimestamp() + ":" + self_ip + "\n";

        for (const auto &seedConn : seedConnections) {
            try {
                send(seedConn.second, deadMsg.c_str(), deadMsg.length(), 0);
                logMessage("[*] Notified seed " + seedConn.first + " about dead node");
            } catch (...) {
                logMessage("[!] Failed to notify seed " + seedConn.first);
            }
        }
    }

    void pingConnectedPeers() {
        while (true) {
            this_thread::sleep_for(chrono::seconds(13));
            lock_guard<mutex> lock(peerMutex);
            
            for (auto it = connectedPeers.begin(); it != connectedPeers.end();) {
                string peer = *it;
                bool alive = pingPeer(peer);
                
                if (!alive) {
                    pingFailures[peer]++;
                    logMessage("[!] Peer " + peer + " missed ping (" + 
                             to_string(pingFailures[peer]) + "/3)");
                    
                    if (pingFailures[peer] >= 3) {
                        size_t pos = peer.find(':');
                        if (pos != string::npos) {
                            string deadIP = peer.substr(0, pos);
                            int deadPort = stoi(peer.substr(pos + 1));
                            notifySeedsDeadNode(deadIP, deadPort);
                        }
                        it = connectedPeers.erase(it);
                        continue;
                    }
                } else {
                    pingFailures[peer] = 0;
                }
                ++it;
            }
        }
    }

public:
    PeerNode(string ip, int port) : self_ip(ip), self_port(port) {
        string logFileName = "peer_" + to_string(port) + "_output.txt";
        logFile.open(logFileName, ios::app);
        if (!logFile)
            cerr << "[!] Error opening log file " << logFileName << endl;
    }

    void start() {
        try {
            registerWithSeeds();
            
            thread acceptThread(&PeerNode::acceptGossipConnections, this);
            acceptThread.detach();
            
            thread gossipThread(&PeerNode::gossipSender, this);
            gossipThread.detach();
            
            thread pingThread(&PeerNode::pingConnectedPeers, this);
            pingThread.detach();
            
            logMessage("[*] Peer node started successfully");
        } catch (const exception &e) {
            logMessage("[!] Error starting peer: " + string(e.what()));
        }
    }
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        cout << "Usage: " << argv[0] << " <self_ip> <self_port>\n";
        return 1;
    }

    try {
        string ip = argv[1];
        int port = stoi(argv[2]);
        PeerNode peer(ip, port);
        peer.start();
        
        // Keep main thread alive
        while (true) {
            this_thread::sleep_for(chrono::seconds(1));
        }
    } catch (const exception &e) {
        cerr << "Fatal error: " << e.what() << endl;
        return 1;
    }

    return 0;
}