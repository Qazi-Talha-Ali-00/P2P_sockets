#include<bits/stdc++.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
using namespace std;
void err(const char *msg){
    perror(msg);
    exit(1);
}
int main(int argc,char *argv[])
{
    // struct sockaddr_in serv_addr;
    // struct hostent *server;
    // domain -> AF_INET->Ipv4
    // type SOCK_STREAM ->TCP protocol
    // type SOCK_DGRAM -> UDP
    // protocl -> 0 default for tcp
    // returns 0 for success and -1 for failure
    // int sockfd = socket(int domain,int type,int protocol);
    // int listen(int sockfd,int backlog);
    // sockfd -> socket file descriptor
    // backlog -> number of connectins a system can handle at a time
    // int bind(int sockfd,struct sockaddr *addr,socklen_t addrlen);
    // struct sockaddr{
    //     sa_family_t sa_family;
    //     char sa_data[14];
    // };
    // newsockfd = accept(sockfd,(struct sockaddr*)&addr,&addrlen);
    // 0 -> for success and -1 for failure
    // int connect (int sockfd,const struct sockaddr *addr,socklen_t addrlen);
    // buffer = string that we will send
    // int read(newsockfd, buffer,buffer_size);
    // int write(newsockfd,buffer,buffer_size);
    if(argc < 2){
        cout<<"Port number not provided "<<endl;
        exit(1);
    }
    int sockfd,newsockfd,port_no,n;
    char buffer[255];
    struct sockaddr_in serv_addr,cli_addr;
    socklen_t clilen;
    sockfd = socket(AF_INET,SOCK_STREAM,0);
    if(sockfd < 0 ){
        err("Error opening socket");
    }
    bzero((char *)&serv_addr, sizeof(serv_addr));
    port_no = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr= INADDR_ANY;
    serv_addr.sin_port = htons(port_no);
    if(bind(sockfd,(struct sockaddr*) &serv_addr,sizeof(serv_addr)) < 0){
        err("Binding failed");
        exit(1);
    }
    listen(sockfd,5);
    clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd,(struct sockaddr*)&cli_addr,&clilen);
    if(newsockfd < 0){
        err("Error on accept");
        exit(1);
    }
    while(1){
        bzero(buffer,255);
         n = read(newsockfd,buffer,255);
         if(n< 0){
            err("Error on reading");
         }
         cout<<"Client : "<<buffer<<endl;
         bzero(buffer,255);
         fgets(buffer,255,stdin);
         n = write(newsockfd,buffer,strlen(buffer));
         if(n<0){
            err("Error on writing");
         }
         int i = strncmp("Bye",buffer,3);
         if(i == 0){
            break;
         }
    }
    close(newsockfd);
    close(sockfd);
    return 0;
}