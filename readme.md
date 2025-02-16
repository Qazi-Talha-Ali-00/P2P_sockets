# P2P Network with Gossip Protocol Implementation

## Overview
This project implements a peer-to-peer (P2P) network with a gossip protocol for message broadcasting and node liveness monitoring. The network follows a power-law degree distribution and includes mechanisms for detecting and verifying dead nodes.

## Features
- Power-law degree distribution in peer connections
- Gossip protocol for message broadcasting
- Node liveness monitoring through ping mechanism
- Dead node verification system
- Multiple seed nodes for network resilience
- Secure node removal process with verification

## Components
1. **Seed Nodes (`seed.cpp`)**
   - Maintains peer list with power-law degree distribution
   - Verifies dead node reports
   - Provides peer information to new nodes
   - Handles node registration and deregistration

2. **Peer Nodes (`peer.cpp`)**
   - Connects to multiple seed nodes
   - Maintains connections following power-law distribution
   - Implements gossip protocol for message broadcasting
   - Monitors peer liveness
   - Reports potentially dead nodes to seeds

3. **Configuration File (`Config.txt`)**
   - Contains seed node information (IP:Port pairs)
   - Used by peers to discover seed nodes

## Requirements
- C++ compiler with C++11 support
- POSIX-compliant operating system (Linux/Unix)
- pthread library
- Basic network utilities (ping)

## Compilation
```bash
# Compile seed node
g++ -o seed seed.cpp -pthread

# Compile peer node
g++ -o peer peer.cpp -pthread
```

## Configuration
Create a `Config.txt` file with seed information:
```
<seed_ip1> <port1>
<seed_ip2> <port2>
...
```

## Running the Network

### 1. Start Seed Nodes
```bash
./seed <ip_address> <port_number>
```
Example:
```bash
./seed 127.0.0.1 8000
./seed 127.0.0.1 8001
```

### 2. Start Peer Nodes
```bash
./peer <ip_address> <port_number>
```
Example:
```bash
./peer 127.0.0.1 9000
./peer 127.0.0.1 9001
```

## Network Behavior

### Peer Connection Process
1. Peer reads seed information from Config.txt
2. Connects to ⌊(n/2)⌋ + 1 randomly chosen seeds
3. Receives peer list following power-law distribution
4. Establishes connections with assigned peers
5. Begins gossip protocol and liveness monitoring

### Gossip Protocol
- Each peer generates a message every 5 seconds
- Messages format: `<timestamp>:<IP>:<message_number>`
- Each peer generates 10 messages total
- Messages are forwarded to all connected peers except the sender

### Liveness Monitoring
- Peers ping connected nodes every 13 seconds
- Three consecutive failed pings trigger dead node reporting
- Seeds verify dead node reports before removal
- False reports are tracked and handled appropriately

## Output Files
- **Seed Output**: `seed_output.txt`
  - Records new peer connections
  - Dead node reports and verifications
  - Network maintenance events

- **Peer Output**: `peer_<IP>_<Port>.txt`
  - Received gossip messages
  - Connection events
  - Dead node detections
  - Ping failure notifications

## Security Considerations
- The implementation includes verification of dead node reports to prevent malicious nodes from falsely reporting active nodes as dead
- Seeds maintain connection verification to ensure accurate network state
- Power-law distribution helps maintain network resilience

## Limitations
- Network operates only on IPv4 addresses
- Requires POSIX-compliant system for ping utility
- No encryption of network traffic
- No authentication mechanism

## Common Issues and Solutions
1. **Bind Failed Error**
   - Ensure port is not in use
   - Wait a few minutes if port was recently used
   - Run with appropriate permissions

2. **Connection Refused**
   - Verify seed nodes are running
   - Check Config.txt for correct addresses
   - Ensure no firewall blocking

3. **High CPU Usage**
   - Adjust ping interval if needed
   - Reduce number of connections if necessary
   - Monitor thread creation/destruction

## Future Improvements
- Add SSL/TLS encryption
- Implement peer authentication
- Add NAT traversal capabilities
- Improve scalability for larger networks
- Add message persistence
- Implement peer discovery optimization

## Contributors
Qazi Talha Ali (B22CS087)
Yogita Yogesh Mundankar (B22CS068)


