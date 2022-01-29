#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <queue>
#define SERVER_PORT "5200"
#define MY_PORT "5101"
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
void *client_send (void *arg);
void *client_receive (void *arg);
void *timer(void *arg);
void get_message (char *text);
void error (char *msg);
void make_pkt(Packet* packet, char* data, char type);
short checksum(char* msg);
int check_packet(Packet* packet);

pthread_t client_send_thread, client_receive_thread;
pthread_t timer_thread;
int sockfd;
struct addrinfo *p,*q;
struct sockaddr *target;
size_t addr_len;
long send_seq = 0;
long recv_seq = 0;
int ack = 0;
std::queue<Packet> queue;
int drops = 0;
int main (int argc, char **argv)
{
    int rv,rv2;
    struct addrinfo hints,hints2;
    struct addrinfo *servinfo,*servinfo_bind; //res // will point to the results
    if(argc != 4){
        printf("incorrect call\n");
        return 0;
    }
    //binding to port
    memset(&hints2, 0, sizeof(hints2)); //empty the struct
    hints2.ai_family = AF_INET;
    hints2.ai_socktype = SOCK_DGRAM;
    if ((rv2 = getaddrinfo(argv[1], argv[3], &hints2, &servinfo_bind)) != 0) {
        fprintf(stderr, "getaddrinfo binding error: %s\n", gai_strerror(rv2));
        exit(1);
    }
    for(q=servinfo_bind; q!= NULL; q=q->ai_next){
        if((sockfd = socket(q->ai_family, q->ai_socktype, q->ai_protocol)) == -1){
            perror("client: socket");
            continue;
        }
        if (bind(sockfd, q->ai_addr, q->ai_addrlen) == -1) {
            perror("client: bind");
            continue;
        }
        break;       
    }
    if(!q){
        fprintf(stderr, "client: failed to bind socket\n");
        return 2;
    }
    memset(&hints, 0, sizeof(hints)); //empty the struct
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(rv));
        exit(1);
    }
    for(p=servinfo; p!= NULL; p=p->ai_next){
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            perror("client: socket");
            continue;
        }
        target = p->ai_addr;
        addr_len = p->ai_addrlen;
        break;       
    }
    if(!p){
        fprintf(stderr, "client: failed to socket\n");
        return 2;
    }

    

    //freeaddrinfo (servinfo);

    // create threads
    if (pthread_create (&client_receive_thread, NULL, client_receive, NULL) != 0)
        error ("client: pthread_create: receiver");
    if (pthread_create (&client_send_thread, NULL, client_send, NULL) != 0)
        error ("client: pthread_create: sender");

    // wait for threads
    if (pthread_join (client_receive_thread, NULL) != 0)
        error ("client: pthread_join: receiver");
    if (pthread_join (client_send_thread, NULL) != 0)
        error ("client: pthread_join: sender");

    close (sockfd);

    exit (1);

}

void *client_send (void *arg)//producer
{
    Packet sndpkt;
    char text[16];
    while (1) {
        ssize_t numbytes;
        //if queue.empty() ?
        get_message (text);
        ack=0;
        make_pkt(&sndpkt,text,MSG);
        queue.push(sndpkt);//enqueue the sent packet, as a backup
        if ((numbytes=sendto (sockfd, &sndpkt, sizeof (Packet), 0
                            ,target,addr_len)) == -1)
                error ("client: message sendto");
        //start timer
        if (pthread_create (&timer_thread, NULL, timer, NULL) != 0)
            error ("server: pthread_create: timer");
        //wait for the timer
        if (pthread_join (timer_thread, NULL) != 0)
            error ("server: pthread_join: timer");
        printf("total %d packets dropped\n",drops);
    }

    if (pthread_cancel (client_receive_thread) != 0) 
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
                    target, addr_len) == -1)
                    error ("client: resending sendto");
            t_start = t_now;
            printf("Resending the message: %s ID: %d\n",sndpkt.data,sndpkt.seq);
        }
    }
    printf("got an ack, stop the timer\n");
}

void *client_receive (void *arg)
{
    Packet recvpkt;
    Packet feedback;
    ssize_t numbytes;
    while (1) {
        if ((numbytes = recvfrom (sockfd, &recvpkt, sizeof (Packet), 0,
                        NULL, NULL)) == -1)
            error ("client: recvfrom");
        int check = check_packet(&recvpkt);
        if(recvpkt.type == MSG){//got a message packet
            if(check && recv_seq == recvpkt.seq){//not_corrupted & in sequence
                printf ("\n(Receive):  %s ID: %d\n",recvpkt.data,recvpkt.seq);//Extract and deliver
                make_pkt(&feedback, NULL, ACK);//send (ACK,recv_seq)
                if (sendto (sockfd, &feedback, sizeof (Packet), 0,
                         target, addr_len) == -1)
                        error ("client: feedback sendto");
                recv_seq = ++recv_seq % 2;//change state
            }
            else if(!check || recv_seq != recvpkt.seq){//corrupted or duplicate
                printf("Corrupted or duplicate message\n");
                make_pkt(&feedback,NULL,ACK);
                feedback.seq = (recv_seq+1)%2;//signal to request resend or to indicate duplicate.
                if (sendto (sockfd, &feedback, sizeof (Packet), 0,
                         target, addr_len) == -1)
                        error ("client: feedback sendto");
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
            //may be timeout, resend if so
            /*else if(!check || recvpkt.seq != send_seq){//resend
                Packet sndpkt;
                sndpkt = queue.front();
                if (sendto (sockfd, &sndpkt, sizeof (Packet), 0,
                        target, addr_len) == -1)
                        error ("client: resending sendto");
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