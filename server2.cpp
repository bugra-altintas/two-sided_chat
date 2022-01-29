#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <chrono>
#include <mutex>
#include <pthread.h>
#include <queue>

#define MYPORT "5202"
#define MSG 0
#define ACK 1

typedef struct Packet {
    char type;
    short checksum;
    long seq;
    char data[16];
    struct Packet& operator=(const struct Packet& rhs){
    if(this == &rhs)
        return *this;
    this->type = rhs.type;
    this->checksum = rhs.checksum;
    this->seq = rhs.seq;
    strcpy(this->data,rhs.data);
    return *this;
}
} Packet;

/*
std::mutex g_mutex;
bool g_ready = false;
int g_data = 0;
*/
int get_port(struct sockaddr *sa){
    return ntohs(((struct sockaddr_in*)sa)->sin_port);
}
void *server_send (void *arg);
void *server_receive (void *arg);
void *timer(void *arg);
void get_message (char *text);
void error (char *msg);
void make_pkt(Packet* packet, char* data, char type);
short checksum(char* msg);
int check_packet(Packet* packet);

pthread_t server_send_thread, server_receive_thread;
pthread_t timer_thread;
Packet recv_message, send_message;
int sockfd;
socklen_t client_addrlen;
struct sockaddr_storage client_addr;
struct addrinfo *p;
long send_seq = 0;
long recv_seq = 0;
int ack=0;
int drops=0;
std::queue<Packet> queue;
int main(int argc, char *argv[]){
    int rv;
    struct addrinfo hints;
    struct addrinfo *servinfo; //res // will point to the results
    //binding to port
    memset(&hints, 0, sizeof(hints)); //empty the struct
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if(argc != 2){
        printf("incorrect call\n");
        return 0;
    }
    if ((rv = getaddrinfo("127.0.0.1", argv[1], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(rv));
        exit(1);
    }
    for(p=servinfo; p!= NULL; p=p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            perror("server: socket");
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;       
    }
    if(!p){
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }
    //freeaddrinfo (servinfo);
    fprintf(stderr,"server: waiting to recv...\n");
    
    //create threads
    if (pthread_create (&server_receive_thread, NULL, server_receive, NULL) != 0)
        error ("server: pthread_create: receiver");
    if (pthread_create (&server_send_thread, NULL, server_send, NULL) != 0)
        error ("server: pthread_create: sender");

    // wait for threads
    if (pthread_join (server_receive_thread, NULL) != 0)
        error ("server: pthread_join: receiver");
    if (pthread_join (server_send_thread, NULL) != 0)
        error ("server: pthread_join: sender");


    close (sockfd);
    exit (1);
}
void *server_send (void *arg)//producer
{
    Packet sndpkt;
    char text[16];
    while (1) {
        get_message (text);
        make_pkt(&sndpkt,text,MSG);
        queue.push(sndpkt);
        if(sendto (sockfd, &sndpkt, sizeof (Packet), 0,
                         (struct sockaddr *) &client_addr, client_addrlen) == -1)
                 error ("server: sendto");
        //start_timer
        if (pthread_create (&timer_thread, NULL, timer, NULL) != 0)
            error ("server: pthread_create: timer");
        if (pthread_join (timer_thread, NULL) != 0)
            error ("server: pthread_join: timer");
    }
    if (pthread_cancel (server_receive_thread) != 0) 
        error ("pthread_cancel");
    return NULL;
}
void make_pkt(Packet* packet, char *data, char type){
    if(!data){//ACK packet
        strcpy(packet->data,"ACK");
        packet->seq = recv_seq;
    }
    else{//Message packet
        strcpy(packet->data,data);
        packet->seq = send_seq;
    }
    packet->type = type;
    packet->checksum = checksum(packet->data);
}
void get_message (char *text)
{
    memset(text,0,sizeof(text));
    while (1) {
        if (fgets (text, 16, stdin) == NULL) 
            error ("fgets");
        int len = strlen (text);
        if (text[len-1] == '\n')
            text[len-1] = '\0';
        return;
    }
}
short checksum(char* msg){
    short sum=0;
    for(int i=0;i<8;i++){
        sum+=msg[i];
    }
    return (~sum);
}
void *timer(void *arg){
    //start = now
    auto t_start = std::chrono::high_resolution_clock::now();
    std::chrono::milliseconds delay(100);
    while(!ack){
        usleep(1000);
        auto t_now = std::chrono::high_resolution_clock::now();
        std::chrono::milliseconds elapsed = 
                std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_start);
        if(delay <= elapsed){//delayed ack or loss packet
            printf("timeout\n");
            drops++;
            Packet sndpkt;
            sndpkt = queue.front();
            if (sendto (sockfd, &sndpkt, sizeof (Packet), 0,
                     (struct sockaddr *) &client_addr, client_addrlen) == -1)
                    error ("client: resending sendto");
            t_start = t_now;
            printf("Resending the message: %s ID: %d\n",sndpkt.data,sndpkt.seq);
        }
    }
    printf("got an ack, stop the timer\n");
}
void *server_receive (void *arg)//receiver
{
    Packet recvpkt;
    Packet feedback;
    ssize_t numbytes;
    while (1) {
        client_addrlen = sizeof (struct sockaddr_storage);
        if ((numbytes = recvfrom (sockfd, &recvpkt, sizeof (Packet), 0,
                        (struct sockaddr *) &client_addr, &client_addrlen)) == -1)
            error ("server: recvfrom");
        printf("port: %d\n",get_port((struct sockaddr*)&client_addr));
        int check = check_packet(&recvpkt);
        //receive message thread
        if(recvpkt.type == MSG){//got a message
            if(check && recv_seq == recvpkt.seq){//not_corrupted & in sequence
                printf ("\n(Receive):  %s ID: %d\n",recvpkt.data,recvpkt.seq);//extract and deliver
                make_pkt(&feedback, NULL, ACK);//send (ACK,recv_seq)
                if(sendto (sockfd, &feedback, sizeof (Packet), 0,
                         (struct sockaddr *) &client_addr, client_addrlen) == -1)
                        error ("server: feedback sendto");
                recv_seq = ++recv_seq % 2;//change state
            }
            else if(!check || recv_seq != recvpkt.seq){//corrupted or duplicate
                printf("Corrupted or duplicate message\n");
                make_pkt(&feedback,NULL,ACK);
                feedback.seq = (recv_seq+1)%2;//signal to request resend or to indicate duplicate.
                if(sendto (sockfd, &feedback, sizeof (Packet), 0,
                         (struct sockaddr *) &client_addr, client_addrlen) == -1)
                        error ("server: feedback sendto");
            }
        }
        else if(recvpkt.type == ACK){//got a feedback packet
            if(check && recvpkt.seq == send_seq){
                printf("I got an ACK %d, changing state to %d\n",send_seq,(send_seq+1)%2);
                send_seq = ++send_seq % 2;//change state
                queue.pop();//dequeue the backup
                ack=1;
                //stop_timer
            }
            /*else if(!check || recvpkt.seq != send_seq){//resend
                Packet sndpkt;
                sndpkt = queue.front();
                if(sendto (sockfd, &sndpkt, sizeof (Packet), 0,
                         (struct sockaddr *) &client_addr, client_addrlen) == -1)
                        error ("server: resending sendto");
                printf("Resending the message: %s ID: %d\n",sndpkt.data,sndpkt.seq);
            }*/
        }
        /*else{//can not recognize the packet, will be fixed, just send the packet in queue
            printf("Can not recognize the packet!\n");
        }*/
        
    }
}
int check_packet(Packet* packet){
    short sum=0;
    for(int i=0;i<8;i++){
        sum+=packet->data[i];
    }
    short result = sum+packet->checksum;
    if(result&-1 == -1)
        return 1;
    else return 0;
}
void error (char *msg)
{
    perror (msg);
    exit (1);
}





    

