#include<bits/stdc++.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<netdb.h>
using namespace std;
void error(const char *msg){
    perror(msg);
    exit(0);
}

int main(int argc,char *argv[])
{
    int sockfd,port_no;
int n;
struct sockaddr_in serv_addr;
struct hostent *server;
char buffer [255];
if(argc < 3){
    cout<<"please provide hostname , port and ip "<<endl;
    exit(0);
}
port_no = atoi(argv[2]);
sockfd = socket(AF_INET,SOCK_STREAM,0);
if(sockfd < 0){
    error("ERRor opening socket");
}
server = gethostbyname(argv[1]);
if(server == NULL){
    cout<<"Error no such host"<<endl;
    exit(0);
}
bzero((char *)&serv_addr,sizeof(serv_addr));
serv_addr.sin_family = AF_INET;
bcopy((char *)server->h_addr , (char *)&serv_addr.sin_addr.s_addr,server->h_length);
serv_addr.sin_port= htons(port_no);
if(connect(sockfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr)) < 0){
    error("Connnection failed");
}
while(1){
    bzero(buffer,255);
    fgets(buffer,255,stdin);
    n= write(sockfd,buffer,strlen(buffer));
    if(n<0){
        error("Error on writing");
    }
    bzero(buffer,255);
    n =read(sockfd,buffer,255);
    if(n<0){
        error("Error on reading");
    }
    cout<<"Server: "<<buffer;
    int i = strncmp("Bye",buffer,3);
    if(i == 0){
        break;
    }
}
close(sockfd);

    return 0;
}