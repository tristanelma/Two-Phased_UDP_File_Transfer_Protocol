Client and server socket code for fast, reliable file transfer across a network.
Note that the server should be started before the client.

server.c:
- Description: 
- Compile: gcc -o <object_file_name> server.c
- Run: ./<object_file_name> <file_to_transfer> <host_port>
- Example:
> gcc -o server server.c
> ./server sample_file.txt 7707

client.c:
- Description: 
- Compile: gcc -o <object_file_name> client.c
- Run: ./<object_file_name> <path_to_store_file> <hostname> <host_port>
- Example:
> gcc -o client client.c
> ./client ./sample_received.txt 10.0.1.45 7707