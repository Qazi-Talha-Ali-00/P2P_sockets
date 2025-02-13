#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include<fstream>

using namespace std;
unordered_map<string,int>seed_nodes; // maps ip of seed node to it's port number
int main(){
    ifstream fin;
    fin.open("Config.txt");
    if (!fin) {
        cout << "Error opening the file " << endl;
        return 0;
    }

    string line;
    // Read IP addresses and ports from config file
    while (getline(fin, line)) {
        stringstream ss(line);
        string ip;
        int port;
        ss >> ip >> port;
        seed_nodes[ip] = port;
    }
    fin.close();
     
    return 0;
}