#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/mman.h>

//Arbitrary constants to tweak to improve performance
#define PACKET_DATA_SIZE 8888
#define SIGNAL_REDUNDANCY 10
#define SIGNAL_PACKET_SPACING_MIRCO 75
#define RESEND_PACKET_SPACING_MIRCO 75
#define RESEND_LOOP_DELAY_MICRO 1
#define RESEND_REDUNDANCY 5
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
    //Input: ./<executable> <location to store file> <hostname or IP> <host port>
    FILE *write_file;
    if((write_file = fopen(argv[1], "wb")) == NULL){
        printf("Error opening specified path for writing.\n");
        return -1;
    }
    int my_socket;
    if((my_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1){
        printf("Error creating socket.\n");
        return -1;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr(argv[2]);
	server_address.sin_port = htons(atoi(argv[3]));

    //Request service from server
    char req[8] = "request";
    for(int i = 0; i < SIGNAL_REDUNDANCY; i++){
        sendto(my_socket, req, sizeof(req), 0, (struct sockaddr *) &server_address, sizeof(server_address));
    }
    
    struct data_info my_data_info;
    struct data_info my_data_info_temp;
    while(my_data_info_temp.num_packets != -2){
        //Receive size of file from server
        recvfrom(my_socket, &my_data_info_temp, sizeof(my_data_info_temp), 0, NULL, NULL);
        if(my_data_info_temp.num_packets >= 0){
            my_data_info.num_packets = my_data_info_temp.num_packets;
            my_data_info.file_size = my_data_info_temp.file_size;
            //Respond with the value received
            sendto(my_socket, &(my_data_info.num_packets), sizeof(my_data_info.num_packets), 0, (struct sockaddr *) &server_address, sizeof(server_address));
        }
    }
    //Allocate a buffer to write to, rather than directly to the disk
    void *global_buff = mmap(NULL, my_data_info.num_packets*PACKET_DATA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(global_buff, 0, my_data_info.num_packets*PACKET_DATA_SIZE);
    void *global_buff_flags = mmap(NULL, my_data_info.num_packets+1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(global_buff_flags, 0, my_data_info.num_packets+1);

    //Begin multithreading
    //int pid = fork();
    int pid = 1;

    struct packet my_packet;
    memset(&my_packet, 0, sizeof(my_packet));

    printf("\nEntering best-effort phase...\n\n");

    //Best-effort phase
    while(my_packet.id != -5){
        //Clear memory of packet struct
        memset(&my_packet, 0, sizeof(my_packet));
        //Receive packet from server and write data to packet struct
        recvfrom(my_socket, &my_packet, sizeof(my_packet), 0, NULL, NULL);
        printf("Received packet ID %d of %d\n", my_packet.id, my_data_info.num_packets);
        if((my_packet.id > 0) && (my_packet.id <= my_data_info.num_packets)){
            memcpy(global_buff+((my_packet.id-1)*PACKET_DATA_SIZE), &my_packet.data, sizeof(my_packet.data));
            memset(global_buff_flags+my_packet.id, 1, 1);
        }
    }

    if(pid == 0)
        exit(0);

    void *global_end_signal = mmap(NULL, 4, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(global_end_signal, 0, 4);



    //Resend phase:
    printf("\nEntering resend phase...\n\n");
    char flag = 0;
    int end_signal = 0;

    pid = fork();

    if(pid > 0){
        int num_lost_packets_detected = 0;
        int packet_index = 0;
        while(1){
            //Send all id's of lost packets to server (send the id's as the data/payload of the packets)
            num_lost_packets_detected = 0;
            packet_index = 0;
            memset(&my_packet, 0, sizeof(my_packet));
            for(int i = 1; i <= my_data_info.num_packets; i++){
                memcpy(&flag, global_buff_flags+i, 1);
                if(flag == 0){
                    memcpy(my_packet.data+(packet_index*4), &i, 4);
                    packet_index++;
                    num_lost_packets_detected++;
                    if((num_lost_packets_detected%(PACKET_DATA_SIZE/4)) == 0){
                        //Send packet containing lost id's to server
                        my_packet.id = 0;
                        for(int j = 0; j < RESEND_REDUNDANCY; j++){
                            sendto(my_socket, &my_packet, sizeof(my_packet), 0, (struct sockaddr *) &server_address, sizeof(server_address));
                            usleep(RESEND_PACKET_SPACING_MIRCO);
                        }
                        memset(&my_packet, 0, sizeof(my_packet));
                        packet_index = 0;
                    }
                }
            }
            printf("Number of missing packets: %d\n", num_lost_packets_detected);
            if(num_lost_packets_detected == 0){
                memset(global_end_signal, 1, 4);
                break;
            }

            //Fill out remaining packet with 0's
            if((PACKET_DATA_SIZE - (num_lost_packets_detected*4)) > 0){
                memset(my_packet.data+(num_lost_packets_detected*4), 0, (PACKET_DATA_SIZE - (num_lost_packets_detected*4)));
            }

            //Send the last batch of id's
            for(int j = 0; j < RESEND_REDUNDANCY; j++){
                sendto(my_socket, &my_packet, sizeof(my_packet), 0, (struct sockaddr *) &server_address, sizeof(server_address));
                usleep(RESEND_PACKET_SPACING_MIRCO);
            }

            //Delay
            usleep(RESEND_LOOP_DELAY_MICRO);
        }
    }
    else if(pid == 0){
        //Receive the packets the server resends in a similar way to best-effort phase
        while(end_signal == 0){
            //Clear memory of packet struct
            memset(&my_packet, 0, sizeof(my_packet));
            //Receive packet from server and write data to packet struct
            recvfrom(my_socket, &my_packet, sizeof(my_packet), 0, NULL, NULL);
            if(my_packet.id > 0){
                memcpy(global_buff+((my_packet.id-1)*PACKET_DATA_SIZE), &my_packet.data, sizeof(my_packet.data));
                memset(global_buff_flags+my_packet.id, 1, 1);
            }
            memcpy(&end_signal, global_end_signal, 4);
        }
        exit(0);
    }

    //Signal end of this phase by sending packets with id -8 to server
    my_packet.id = -8;
    for(int j = 0; j < END_RESEND_PHASE_REDUNDANCY; j++){
        sendto(my_socket, &my_packet, sizeof(my_packet), 0, (struct sockaddr *) &server_address, sizeof(server_address));
        usleep(SIGNAL_PACKET_SPACING_MIRCO);
    }

    //Write contents of buffer to output file
    fwrite(global_buff, 1, my_data_info.file_size, write_file);

    fclose(write_file);
    close(my_socket);
    printf("Done.\n");
}