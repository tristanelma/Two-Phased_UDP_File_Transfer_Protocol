# Two-Phased UDP File Transfer Protocol

UDP client and server socket code for fast, reliable file transfer across a network. Protocol designed by Tristan Elma in Fall 2020 for EE 542 at USC (Internet and Cloud Computing), instructed by Prof. Young Cho.

---

## Protocol

1. Client requests service from the server
2. Server responds with size of file to be transmitted
i. Client sends acknowledgement
ii. When done, server signals beginning of next phase
3. Single-threaded “best-effort phase”
i. When done, server signals beginning of next phase
4. Multi-threaded “resend phase”
i. When done, client signals that transfer is complete

### Best-effort phase
The general idea is to send packets from the server to the client as fast as possible, with no acknowledgements from client. In the code, there are parameters in `#define` constants to configure redundancy and also for performing the best-effort phase multiple times in a row before moving to the resend phase, but the best performance is typically attained with no redundancy (`BEST_EFFORT_REDUNDANCY = 1`). Throughout the best-effort phase, the client updates a char array of 0’s and 1’s, signifying which packets it has successfully received. The client loops constantly and receives packets as they arrive. When a packet arrives, it updates the char array index for that packet to 1 based on its ID. When a packet arrives, it stores the incoming data in a buffer, which will eventually comprise the entire file.

### Resend phase
This phase is designed to ensure that any data which is lost during the best-effort phase is communicated reliably, though it is slower than the best-effort phase. The client and server are both multithreaded, with one thread constantly receiving and processing incoming packets and the other thread constantly sending packets. In both the client and server programs, there is a global ID buffer allocated with `mmap()` that allows status information about which packets still need to be communicated, which is shared among the threads.

Client:
- Global ID buffer stores the packet ID’s which it never received
- The receiving thread receives packets from the server. When it receives a packet, it adds its data to the data buffer and updates the global ID buffer
- The sending thread reads the global ID buffer, packages the ID’s of as many missing packets as it can into a single packet payload, and sends them to the server in a single packet

Server:
- Global ID buffer stores the packet ID’s which need to be resent
- The receiving thread receives packets from the client, which are parsed into a list of packet ID’s that the client still needs. From this, it updates the global ID buffer.
- The sending thread reads the global ID buffer, retrieves the data associated with those ID’s, and resends the packets to the client

---

## Instructions
Note that the server should be started before the client.

server.c:
- Description: 
- Compile: `gcc -o <object_file_name> server.c`
- Run: `./<object_file_name> <file_to_transfer> <host_port>`
- Example:

`gcc -o server server.c`

`./server sample_file.txt 7707`

client.c:
- Description: 
- Compile: `gcc -o <object_file_name> client.c`
- Run: `./<object_file_name> <path_to_store_file> <hostname> <host_port>`
- Example:

`gcc -o client client.c`

`./client ./sample_received.txt 10.0.1.45 7707`
