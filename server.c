#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>

//Arbitrary constants to tweak to improve performance
#define PACKET_DATA_SIZE 1400
#define SIGNAL_PACKET_SPACING_MIRCO 75
#define SIGNAL_REDUNDANCY 20
#define BEST_EFFORT_PACKET_SPACING_MIRCO 50
#define BEST_EFFORT_PHASES 1
#define BEST_EFFORT_REDUNDANCY 1
#define RESEND_PACKET_SPACING_MIRCO 75
#define RESEND_LOOP_DELAY_MICRO 1000
#define RESEND_REDUNDANCY 2
#define END_RESEND_PHASE_REDUNDANCY 10000

struct packet{
    int id;
    char data[PACKET_DATA_SIZE];
};

struct data_info{
    int num_packets;
    int file_size;
};

int main(int argc, char **argv){
    //Input: ./<executable> <file to send> <host port>
    FILE *read_file;
    if((read_file = fopen(argv[1], "rb")) == NULL){
        printf("Error opening specified file.\n");
        return -1;
    }
    struct data_info my_data_info;
    fseek(read_file, 0, SEEK_END);
    my_data_info.file_size = ftell(read_file);
    fseek(read_file, 0, SEEK_SET);
    my_data_info.num_packets = my_data_info.file_size / PACKET_DATA_SIZE;
    if((my_data_info.file_size % PACKET_DATA_SIZE) != 0) my_data_info.num_packets++;
    //Read data into buffer so it can be transmitted quickly
    char *buff = malloc(my_data_info.file_size + 1);
    memset(buff, 0, my_data_info.file_size+1);
    fread(buff, 1, my_data_info.file_size, read_file);
    fclose(read_file);

    int my_socket;
    if((my_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1){
        printf("Error creating socket.\n");
        return -1;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    //Accept connections from any client ip
    server_address.sin_addr.s_addr = INADDR_ANY;
	server_address.sin_port = htons(atoi(argv[2]));
    
    if (bind(my_socket, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
        printf("Error binding to socket.\n");
        return -1;
    }

    //Receive request from client
    char req[8];
    struct sockaddr_in client_address;
    ssize_t client_address_size = sizeof(client_address);
    recvfrom(my_socket, req, 8, 0, (struct sockaddr *) &client_address, (socklen_t *) &client_address_size);

    // struct timeval tv;
    // tv.tv_sec = 0;
    // tv.tv_usec = 10;
    // //Set timeout
    // setsockopt(my_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int value_received = -1;
    while(value_received != my_data_info.num_packets){
        //Send size of file to client
        sendto(my_socket, &my_data_info, sizeof(my_data_info), 0, (struct sockaddr *) &client_address, sizeof(client_address));
        
        //Receive the number of packets that the client sends in response
        recvfrom(my_socket, &value_received, sizeof(value_received), 0, NULL, NULL);
    }




    //Signal beginning of best-effort phase
    int good = -2;
    for(int i = 0; i < SIGNAL_REDUNDANCY; i++){
        sendto(my_socket, &good, sizeof(good), 0, (struct sockaddr *) &client_address, sizeof(client_address));
        usleep(SIGNAL_PACKET_SPACING_MIRCO);
    }

    void *global_counter = mmap(NULL, 4, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(global_counter, 0, 4);

    //Enable multithreading if desired
    //int pid = fork();
    int pid = 1;

    struct packet my_packet;
    
    int bytes_sent = 0;

    //Begin timer
    struct timeval tv1;
    gettimeofday(&tv1, NULL);
    double start = (tv1.tv_sec) + (tv1.tv_usec) / 1000000.0;

    //Best-effort phase
    for(int phase = 0; phase < BEST_EFFORT_PHASES; phase++){
        bytes_sent = 0;
        for(int i = 1; i <= my_data_info.num_packets; i++){
            //Clear memory of packet struct
            memset(&my_packet, 0, sizeof(my_packet));
            memcpy(my_packet.data, buff+bytes_sent, PACKET_DATA_SIZE);
            my_packet.id = i;
            
            //Send packet to client
            for(int j = 0; j < BEST_EFFORT_REDUNDANCY; j++){
                sendto(my_socket, &my_packet, sizeof(my_packet), 0, (struct sockaddr *) &client_address, sizeof(client_address));
                usleep(BEST_EFFORT_PACKET_SPACING_MIRCO);
            }
            bytes_sent += PACKET_DATA_SIZE;
        }
    }

    if(pid == 0){
        exit(0);
    }
    //End multithreading ^^^
    wait(NULL);
    
    gettimeofday(&tv1, NULL);
    double end = (tv1.tv_sec) + (tv1.tv_usec) / 1000000.0 ;
    double transfer_time = end - start;
    printf("Best-effort phase transfer time: %f seconds\n", transfer_time);
    long bytes_transferred = my_data_info.file_size;
    long bits_transferred = bytes_transferred * 8;
    printf("Best-effort data transfered: %ld bits (%.2f Mbits)\n", bits_transferred, bits_transferred/1000000.0);
    printf("Best-effort phase throughput: %.2f bits/sec (%.2f Mbits/sec)\n\n", bits_transferred/transfer_time, bits_transferred/transfer_time/1000000);
    
    //Signal end of best-effort phase
    memset(&my_packet, 0, sizeof(my_packet));
    my_packet.id = -5;
    for(int j = 0; j < SIGNAL_REDUNDANCY; j++){
        sendto(my_socket, &my_packet, sizeof(my_packet), 0, (struct sockaddr *) &client_address, sizeof(client_address));
        usleep(SIGNAL_PACKET_SPACING_MIRCO);
    }


    
    //Resend lost packets phase

    //Global array of id's to send
    void *ids_to_send = mmap(NULL, PACKET_DATA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(ids_to_send, 0, PACKET_DATA_SIZE);

    void *global_end_signal = mmap(NULL, 4, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(global_end_signal, 0, 4);

    gettimeofday(&tv1, NULL);
    double start_resend = (tv1.tv_sec) + (tv1.tv_usec) / 1000000.0;

    pid = fork();

    if(pid == 0){
        //Receive requested packet id's from client
        int requested_packet_id = 0;
        while(1){
            memset(&my_packet, 0, sizeof(my_packet));
            recvfrom(my_socket, &my_packet, sizeof(my_packet), 0, NULL, NULL);
            if(my_packet.id == -8){
                memset(global_end_signal, 1, 4);
                break;
            }
            memcpy(ids_to_send, my_packet.data, PACKET_DATA_SIZE);
        }
        exit(0);
    }
    else if(pid > 0){
        //Resend requested packets
        int end_signal = 0;
        int id_to_send;
        while(end_signal == 0){
            for(int i = 0; i < PACKET_DATA_SIZE; i++){
                memcpy(&id_to_send, ids_to_send+i, 4);
                if((id_to_send > 0) && (id_to_send <= my_data_info.num_packets)){
                    memset(&my_packet, 0, sizeof(my_packet));
                    memcpy(my_packet.data, buff+((id_to_send-1)*PACKET_DATA_SIZE), PACKET_DATA_SIZE);
                    my_packet.id = id_to_send;
                    for(int j = 0; j < RESEND_REDUNDANCY; j++){
                        sendto(my_socket, &my_packet, sizeof(my_packet), 0, (struct sockaddr *) &client_address, sizeof(client_address));
                        usleep(RESEND_PACKET_SPACING_MIRCO);
                    }
                }
            }
            memcpy(&end_signal, global_end_signal, 4);
        }
    }

    //Display performance results
    gettimeofday(&tv1, NULL);
    end = (tv1.tv_sec)+(tv1.tv_usec)/1000000.0;
    transfer_time = end-start_resend;
    printf("Resend phase transfer time: %.2f seconds\n", transfer_time);
    printf("Resend throughput: %.2f bits/sec (%.2f Mbits/sec)\n\n", bits_transferred/transfer_time, bits_transferred/transfer_time/1000000);

    transfer_time = end-start;
    printf("Overall transfer time: %.2f seconds\n", transfer_time);
    printf("Overall data transfered: %ld bits (%.2f Mbits)\n", bits_transferred, bits_transferred/1000000.0);
    printf("Overall throughput: %.2f bits/sec (%.2f Mbits/sec)\n", bits_transferred/transfer_time, bits_transferred/transfer_time/1000000);

    free(buff);
    close(my_socket);
    return 0;
}