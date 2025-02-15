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
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <errno.h>

using namespace std;

class SeedNode
{
private:
    int self_port;
    string self_ip;
    mutex peerListMutex;
    unordered_map<string, int> peerList;
    ofstream logFile;

    string getCurrentTimestamp()
    {
        auto now = chrono::system_clock::now();
        time_t t = chrono::system_clock::to_time_t(now);
        string ts = ctime(&t);
        ts.pop_back();
        return ts;
    }

    void logMessage(const string &msg)
    {
        string timestamp = getCurrentTimestamp();
        string logMsg = "[" + timestamp + "] " + msg;
        cout << logMsg << endl;
        if (logFile.is_open())
            logFile << logMsg << endl;
    }

    string generatePeerList()
    {
        ostringstream oss;
        lock_guard<mutex> lock(peerListMutex);
        for (const auto &entry : peerList)
        {
            oss << entry.first << ":" << entry.second << "\n";
        }
        return oss.str();
    }

    void handleDeadNode(const string &message)
    {
        try
        {
            istringstream iss(message);
            string token;
            vector<string> tokens;
            while (getline(iss, token, ':'))
            {
                tokens.push_back(token);
            }
            if (tokens.size() < 5)
            {
                logMessage("[!] Invalid dead node message format: " + message);
                return;
            }
            string deadIP = tokens[1];
            int deadPort = stoi(tokens[2]);

            lock_guard<mutex> lock(peerListMutex);
            if (peerList.erase(deadIP))
            {
                logMessage("[*] Removed dead node: " + deadIP + ":" + to_string(deadPort));
            }
        }
        catch (const exception &e)
        {
            logMessage("[!] Error processing dead node message: " + string(e.what()));
        }
    }

    void handleClient(int clientSocket, string clientIP)
    {
        try
        {
            // Set socket timeout
            struct timeval timeout;
            timeout.tv_sec = 5; // 5 seconds timeout
            timeout.tv_usec = 0;
            setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            // First receive the peer's port
            char buffer[1024] = {0};
            int bytes = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0)
            {
                logMessage("[!] Error receiving port from " + clientIP);
                close(clientSocket);
                return;
            }

            // Parse port number
            int peerPort = stoi(string(buffer));

            // Add to peer list

            

            // Send current peer list
            string list = generatePeerList();
            if (send(clientSocket, list.c_str(), list.length(), 0) < 0)
            {
                logMessage("[!] Error sending peer list to " + clientIP);
                close(clientSocket);
                return;
            }
            {
                lock_guard<mutex> lock(peerListMutex);
                peerList[clientIP] = peerPort;
            }
            logMessage("[+] Registered peer: " + clientIP + ":" + to_string(peerPort));

            // Keep connection open for dead node notifications
            while (true)
            {
                memset(buffer, 0, sizeof(buffer));
                bytes = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytes == 0)
                {
                    logMessage("[*] Connection closed by " + clientIP);
                    break;
                }
                else if (bytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    logMessage("[!] Error receiving from " + clientIP + ": " + strerror(errno));
                    break;
                }

                string msg(buffer, bytes);
                if (msg.find("Dead Node:") == 0)
                {
                    handleDeadNode(msg);
                }
            }

            // Remove peer from list when connection closes
            {
                lock_guard<mutex> lock(peerListMutex);
                peerList.erase(clientIP);
            }
        }
        catch (const exception &e)
        {
            logMessage("[!] Error in client handler for " + clientIP + ": " + string(e.what()));
        }

        close(clientSocket);
    }

public:
    SeedNode(int port, string ip) : self_port(port), self_ip(ip)
    {
        string logFileName = "seed_" + to_string(port) + "_output.txt";
        logFile.open(logFileName, ios::app);
        if (!logFile)
            cerr << "[!] Error opening log file " << logFileName << endl;
    }

    void start()
    {
        try
        {
            int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (serverSocket < 0)
            {
                throw runtime_error("Socket creation failed");
            }

            // Allow socket reuse
            int opt = 1;
            if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            {
                close(serverSocket);
                throw runtime_error("setsockopt failed");
            }

            sockaddr_in serverAddr;
            memset(&serverAddr, 0, sizeof(serverAddr));
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(self_port);
            serverAddr.sin_addr.s_addr = INADDR_ANY;

            if (bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
            {
                close(serverSocket);
                throw runtime_error("Bind failed on port " + to_string(self_port));
            }

            if (listen(serverSocket, SOMAXCONN) < 0)
            {
                close(serverSocket);
                throw runtime_error("Listen failed");
            }

            logMessage("[*] Seed node started on port " + to_string(self_port));

            while (true)
            {
                sockaddr_in clientAddr;
                socklen_t addrLen = sizeof(clientAddr);
                int clientSocket = accept(serverSocket, (sockaddr *)&clientAddr, &addrLen);
                if (clientSocket < 0)
                {
                    logMessage("[!] Accept failed: " + string(strerror(errno)));
                    continue;
                }

                string clientIP = inet_ntoa(clientAddr.sin_addr);
                logMessage("[*] New connection from " + clientIP);

                thread t(&SeedNode::handleClient, this, clientSocket, clientIP);
                t.detach();
            }
        }
        catch (const exception &e)
        {
            logMessage("[!] Fatal error: " + string(e.what()));
        }
    }
};

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        cout << "Usage: " << argv[0] << " <self_ip> <self_port>\n";
        return 1;
    }

    try
    {
        string ip = argv[1];
        int port = stoi(argv[2]);
        SeedNode seed(port, ip);
        seed.start();
    }
    catch (const exception &e)
    {
        cerr << "Fatal error: " << e.what() << endl;
        return 1;
    }

    return 0;
}