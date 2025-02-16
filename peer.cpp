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
    set<string> connectedPeers;       // neighbors we have established connections to
    unordered_set<string> ML;           // Message List to prevent duplicate forwarding
    mutex mlMutex;
    mutex peerMutex;
    ofstream logFile;
    unordered_map<string, int> pingFailures;
    vector<pair<string, int>> seedConnections;  // Persistent connections to seeds (if any)

    // Returns current timestamp as a string.
    string getCurrentTimestamp() {
        auto now = chrono::system_clock::now();
        time_t t = chrono::system_clock::to_time_t(now);
        string ts = ctime(&t);
        ts.pop_back();
        return ts;
    }

    // Logs a message to console and log file.
    void logMessage(const string &msg) {
        string timestamp = getCurrentTimestamp();
        string logMsg = "[" + timestamp + "] " + msg;
        cout << logMsg << endl;
        if (logFile.is_open())
            logFile << logMsg << endl;
    }

    // Reads seed node details from Config.txt (each line: "<seed_IP> <seed_Port>")
    vector<pair<string, int>> readSeedList() {
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

    // Connects to a seed node, registers self (by sending our listening port), and receives its peer list.
    bool connectToSeed(const string &seedIP, int seedPort, set<string> &seedPeers) {
        try {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0)
                throw runtime_error("Socket creation failed");

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

            // Send our listening port.
            string portStr = to_string(self_port) + "\n";
            if (send(sock, portStr.c_str(), portStr.length(), 0) < 0) {
                close(sock);
                throw runtime_error("Failed to send port");
            }

            // Receive peer list from seed.
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
                if (!peer.empty() && peer.find(":") != string::npos)
                    seedPeers.insert(peer);
            }
            // We close the socket after reading.
            close(sock);
            return true;
        } catch (const exception &e) {
            logMessage("[!] Error connecting to seed " + seedIP + ":" + to_string(seedPort) + " - " + e.what());
            return false;
        }
    }

    // Connects to a subset of peers (neighbors) using a simple power-law inspired selection.
    void connectToPeers(const set<string> &availablePeers) {
        if (availablePeers.empty()) {
            logMessage("[*] No other peers available to connect to");
            return;
        }
        // Simple power-law selection: choose ceil(log2(n+1)) peers.
        vector<string> peers(availablePeers.begin(), availablePeers.end());
        int numPeers = peers.size();
        int connectCount = ceil(log2(numPeers + 1));
        connectCount = min(connectCount, numPeers);

        random_device rd;
        mt19937 gen(rd());
        shuffle(peers.begin(), peers.end(), gen);

        for (int i = 0; i < connectCount; i++) {
            string peer = peers[i];
            size_t pos = peer.find(':');
            if (pos == string::npos)
                continue;
            string peerIP = peer.substr(0, pos);
            int peerPort;
            try {
                peerPort = stoi(peer.substr(pos + 1));
            } catch (...) {
                continue;
            }
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0)
                continue;
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(peerPort);
            inet_pton(AF_INET, peerIP.c_str(), &addr.sin_addr);
            if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0) {
                lock_guard<mutex> lock(peerMutex);
                connectedPeers.insert(peer);
                logMessage("[+] Connected to peer: " + peer);
            }
            close(sock);
        }
    }

    // Registers with ⌊(n/2)⌋+1 randomly selected seed nodes and updates the neighbor list.
    void registerWithSeeds() {
        vector<pair<string, int>> seeds = readSeedList();
        if (seeds.empty()) {
            logMessage("[!] No seed nodes found in Config.txt!");
            return;
        }
        random_device rd;
        mt19937 gen(rd());
        shuffle(seeds.begin(), seeds.end(), gen);
        int numToConnect = (seeds.size() / 2) + 1;
        set<string> allPeers;
        int connectedSeeds = 0;
        logMessage("[*] Connecting to " + to_string(numToConnect) + " randomly selected seed nodes...");
        for (int i = 0; i < numToConnect && i < seeds.size(); i++) {
            set<string> seedPeers;
            if (connectToSeed(seeds[i].first, seeds[i].second, seedPeers)) {
                connectedSeeds++;
                for (const auto &peer : seedPeers) {
                    // Do not add ourself.
                    if (peer != (self_ip + ":" + to_string(self_port)))
                        allPeers.insert(peer);
                }
            }
        }
        if (connectedSeeds < numToConnect) {
            logMessage("[!] Warning: Only connected to " + to_string(connectedSeeds) + " seeds out of required " + to_string(numToConnect));
        }
        // Update neighbor list.
        connectToPeers(allPeers);
    }

    // Periodically update the neighbor list by re-registering with seeds.
    void updateNeighbors() {
        while (true) {
            this_thread::sleep_for(chrono::seconds(2));  // Update every 2 seconds.
            logMessage("[*] Updating neighbor list from seeds...");
            registerWithSeeds();
        }
    }

    // ----- GOSSIP FUNCTIONS -----
    void forwardGossip(const string &message, const string &excludeIP = "") {
        lock_guard<mutex> lock(peerMutex);
        for (const auto &peer : connectedPeers) {
            size_t pos = peer.find(':');
            if (pos == string::npos)
                continue;
            string peerIP = peer.substr(0, pos);
            if (peerIP == excludeIP)
                continue;
            try {
                int peerPort = stoi(peer.substr(pos + 1));
                int sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock < 0)
                    continue;
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

    void handleGossipConnection(int clientSocket, const string &senderIP) {
        char buffer[1024] = {0};
        int bytes = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            close(clientSocket);
            return;
        }
        buffer[bytes] = '\0';
        string message(buffer, bytes);

        // Handle PING and STATUS messages.
        if (message == "PING\n") {
            send(clientSocket, "PONG\n", 5, 0);
            close(clientSocket);
            return;
        }
        if (message == "STATUS\n") {
            send(clientSocket, "ALIVE\n", 6, 0);
            close(clientSocket);
            return;
        }

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
            logMessage("[Gossip] New message from " + senderIP + ": " + message +
                       " | Received at: " + localTime);
            forwardGossip(message, senderIP);
        } else {
            logMessage("[Gossip] Duplicate message from " + senderIP + " ignored.");
        }
        close(clientSocket);
    }

    void acceptGossipConnections() {
        int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket < 0) {
            logMessage("[!] Failed to create gossip server socket");
            return;
        }
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
            if (clientSocket < 0)
                continue;
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
            thread t(&PeerNode::handleGossipConnection, this, clientSocket, string(clientIP));
            t.detach();
        }
        close(serverSocket);
    }

    void gossipSender() {
        this_thread::sleep_for(chrono::seconds(2)); // Initial delay to allow neighbor updates
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
    // ----- END GOSSIP FUNCTIONS -----

    // ----- PING FUNCTIONALITY -----
    bool pingPeer(const string &peer) {
        size_t pos = peer.find(':');
        if (pos == string::npos)
            return false;
        string peerIP = peer.substr(0, pos);
        int peerPort;
        try {
            peerPort = stoi(peer.substr(pos + 1));
        } catch (...) {
            return false;
        }
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
            return false;
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
        send(sock, pingMsg.c_str(), pingMsg.length(), 0);
        char buffer[10] = {0};
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        close(sock);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            return (string(buffer) == "PONG\n");
        }
        return false;
    }

    void pingConnectedPeers() {
        while (true) {
            this_thread::sleep_for(chrono::seconds(13));
            lock_guard<mutex> lock(peerMutex);
            for (auto it = connectedPeers.begin(); it != connectedPeers.end();) {
                bool alive = pingPeer(*it);
                if (!alive) {
                    pingFailures[*it]++;
                    logMessage("[!] Peer " + *it + " missed ping (" + to_string(pingFailures[*it]) + "/3)");
                    if (pingFailures[*it] >= 3) {
                        size_t pos = it->find(':');
                        if (pos != string::npos) {
                            string deadIP = it->substr(0, pos);
                            int deadPort = stoi(it->substr(pos + 1));
                            logMessage("[!] Peer " + *it + " considered dead. Notifying seeds...");
                            notifySeedsDeadNode(deadIP, deadPort);
                        }
                        it = connectedPeers.erase(it);
                        continue;
                    }
                } else {
                    pingFailures[*it] = 0;
                }
                ++it;
            }
        }
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
    // ----- END PING FUNCTIONALITY -----

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
            // Start a thread to periodically update neighbor list.
            thread updateThread(&PeerNode::updateNeighbors, this);
            updateThread.detach();
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

int main(int argc, char *argv[]){
    if (argc < 3) {
        cout << "Usage: " << argv[0] << " <self_ip> <self_port>\n";
        return 1;
    }
    try {
        string ip = argv[1];
        int port = stoi(argv[2]);
        PeerNode peer(ip, port);
        peer.start();
        while (true)
            this_thread::sleep_for(chrono::seconds(1));
    } catch (const exception &e) {
        cerr << "Fatal error: " << e.what() << endl;
        return 1;
    }
    return 0;
}
