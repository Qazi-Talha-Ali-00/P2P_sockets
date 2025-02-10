#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include<fstream>

using namespace std;
map<string, int> peer_list;     // Stores {IP → Port}
map<string, int> peer_sockets;  // Stores {IP → socket FD}
mutex m;
bool isalive(string ip) {
    m.lock();
    if (peer_sockets.find(ip) == peer_sockets.end()) {
        m.unlock();
        return false;  // No existing connection, assume dead
    }
    
    int sockfd = peer_sockets[ip];  // Get the existing socket FD
    m.unlock();

    string ping_msg = "PING";
    send(sockfd, ping_msg.c_str(), ping_msg.length(), 0);

    char buffer[10] = {0};
    struct timeval timeout;
    timeout.tv_sec = 2;  // 2-second timeout
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);

    return (bytes_received > 0 && string(buffer) == "PONG");
}

void handle_dead_node(string& message) {
    istringstream iss(message);
    string token;
    vector<string> tokens;

    while (getline(iss, token, ':')) {
        tokens.push_back(token);
    }

    if (tokens.size() < 5) {
        cout << "Invalid dead node message format!" << endl;
        return;
    }

    string dead_ip = tokens[1];
    int dead_port = stoi(tokens[2]);

    // Check if the node exists in peer_list
    m.lock();
    if (peer_list.find(dead_ip) == peer_list.end()) {
        m.unlock();
        return;
    }
    m.unlock();

    // Verify if the node is actually dead
    if (!isalive(dead_ip) || !isalive(dead_ip)) {
        // Remove from peer_list
        m.lock();
        peer_list.erase(dead_ip);
        peer_sockets.erase(dead_ip);
        m.unlock();

        // Log the removal
        m.lock();
        ofstream fout("log.txt", ios::app);
        if (fout) {
            fout << "Dead node removed: " << dead_ip << ":" << dead_port << endl;
            fout.close();
        }
        m.unlock();
        // cout << "Confirmed dead and removed: " << dead_ip << ":" << dead_port << endl;
    } else {
        m.lock();
        ofstream fout("log.txt", ios::app);
        if(fout){
            fout<<"False alarm: "<<dead_ip<<" is still alive."<<endl;
            fout<<"False alarm given by "<<tokens[4]<<" for "<<dead_ip<<endl;
        }
        m.unlock();
        // cout << "False alarm: " << dead_ip << " is still alive." << endl;
    }
}

void handle_client(int client_socket_fd, sockaddr_in client_addr){
    // get the client ip and port number from the client_add struct
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);
    // create a peer_list to send to this peer
    string peer_list_msg = "";
    m.lock();
    for (auto& peer : peer_list) {
        peer_list_msg += peer.first + " " + to_string(peer.second) + ",";
    }
    m.unlock();
    // first send this peer list to the peer
    send(client_socket_fd, peer_list_msg.c_str(), peer_list_msg.length(), 0);
    
    // now push this clien into our own peer list.
     m.lock();
    peer_list[client_ip] = client_port;
    peer_sockets[client_ip] = client_socket_fd;
    m.unlock();
    // write in the log file that a new peer has been registered.
    m.lock();
    ofstream fout("log.txt",ios::app);
    if(!fout){
        cout<<"Error opening the file";
        m.unlock();
        return;
    }
    fout<<"New peer registered "<<client_ip<<": "<<client_port<<endl;
    fout.close();
    m.unlock();  
     
    char buffer[1024];

    while (true) {
        memset(buffer, 0, sizeof(buffer));  // Clear buffer
        int bytes_received = recv(client_socket_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received <= 0) {
            continue;
            
        }

        buffer[bytes_received] = '\0';  // Null-terminate
        string message(buffer);

        // Check if it's a "Dead Node" message
        if (message.find("Dead Node:") == 0) {
            handle_dead_node(message);
        }
    }
}
int main(int argc ,char *argv[])
{
    if(argc < 2){
        cout<<"Portnumber and Ip address required"<<endl;
        return 0;
    }
    int sockfd,port_no,n;
    sockfd = socket(AF_INET,SOCK_STREAM,0);
    if(sockfd < 0){
        cout<<"Error opening the socket"<<endl;
        return 0;
    }
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port=htons(atoi(argv[1]));
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(sockfd,(sockaddr*) &server_addr,sizeof(server_addr)) < 0){
        cout<<"Binding failed"<<endl;
        return 0;
    }
      if (listen(sockfd, SOMAXCONN) < 0) {
        cout << "Listening failed" << endl;
        return 0;
    }
    while(true){
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket_fd = accept(sockfd, (sockaddr*)&client_addr, &client_len);

        if (client_socket_fd < 0) {
            cout << "Error accepting client connection" << endl;
            continue;
        }

        thread t(handle_client, client_socket_fd, client_addr);
        t.detach();
    }

    return 0;
}