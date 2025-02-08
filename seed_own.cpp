#include<bits/stdc++.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
using namespace std;
void handle_client(int sockfd){
    
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
    if(bind(server_addr,(sockaddr*) &server_addr,sizeof(server_addr)) < 0){
        cout<<"Binding failed"<<endl;
        return 0;
    }
    while(true){
        int client_socket_fd = accept(sockfd,NULL,NULL);
        thread t(handle_client,client_socket_fd);
    }

    return 0;
}