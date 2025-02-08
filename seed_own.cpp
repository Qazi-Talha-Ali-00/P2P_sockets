#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include<fstream>

using namespace std;
map<string, int> peer_list;
mutex m;
void handle_client(int client_socket_fd, sockaddr_in client_addr){
        // create a new socket for this client
     char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);
    m.lock();
    peer_list[client_ip] = client_port;
    m.unlock();

    string peer_list_msg = "";
    m.lock();
    for (auto& peer : peer_list) {
        peer_list_msg += peer.first + " " + to_string(peer.second) + ",";
    }
    m.unlock();
    send(client_socket_fd, peer_list_msg.c_str(), peer_list_msg.length(), 0);
    m.lock();
    ofstream fout("log.txt");
    if(!fout){
        cout<<"Error opening the file";
        return;
    }
    fout<<"New peer registered "<<client_ip<<": "<<client_port<<endl;
    fout.close();
    m.unlock();   
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