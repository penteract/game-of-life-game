#include<arpa/inet.h>
#include<netinet/in.h>
#include<signal.h>
#include<stdbool.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<unistd.h>

#define WIDTH (1<<10)
#define HEIGHT (1<<10)

#define MAXPLAYERS (1<<12)


/// RESPONSES

char errResp[] = "HTTP/1.1 404 Not Found\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Connection: close\r\nCache-Control: no-cache\r\n"
"Content-Length: 94\r\n"
"\r\n"
"404 Not Found (or something else for which I can't be bothered to make a proper error message)";

char chunkedHeaders[] = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Connection: close\r\n"
"X-Content-Type-Options: nosniff\r\n"
"Cache-Control: no-cache\r\n"
"Transfer-Encoding: Chunked\r\n\r\n";

char startResp[] = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Content-Length: 161\r\n"
"\r\n"
"<html><body><script>"
"form=document.createElement('form');"
"form.method='POST';"
"form.action='/';"
"document.body.appendChild(form);"
"form.submit()"
"</script></body></html>";

char initChunk[10009];


///UTILS

int snd(int sock, char* msg){
    printf("\nsending message to socket %d\n",sock);
    puts(msg);
    int c = send(sock,msg,strlen(msg),0);
    printf("sent: %d\n",c);
    if(c<0){perror("send failed"); close(sock); return -1;}
    return 0;
}

char b64[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "abcdefghijklmnopqrstuvwxyz"
               "0123456789+/";

unsigned char b64inv[256];

void b64setup(){
    for(int i=0;i<256;i++)
        b64inv[i]=0xFF;
    for(char i=0;i<64;i++)
        b64inv[b64[i]]=i;
}

int b64e(char* in, char* out, int len){
    int c=0;
    int tmp=0;
    while(len--){
        tmp |= *(in++);
        if(++c%3==0){
            for(int i=0;i<3;i++){
                *(out++)=b64[(tmp>>18)&0x3F];
                tmp<<=8;
            }
            tmp=0;
        }
        else tmp<<=8;
    }
    tmp<<=(c%3)*8;
    for(int i=0;i<c%3;i++){
        *(out++)=b64[(tmp>>18)&0x3F];
        tmp<<=8;
    }
    return c;
}

int b64d(char* in, char* out, int len){
    int lenin = (len/3)*4 + ((len%3)*3+1)/2;
    int tmp=0;
    int c=0;
    while(b64inv[*in]!=0xFF && c<lenin){
        tmp |= b64inv[*(in++)];
        if(++c%4==0){
            for(int i=0;i<3;i++){
                *(out++)=(tmp>>16)&0xFF;
                tmp<<=8;
            }
            tmp=0;
        }
        else tmp<<=6;
    }
    tmp<<=(c%4)*6;
    for(int i=0;i<c%4;i++){
        *(out++)=(tmp>>16)&0xFF;
        tmp<<=8;
    }
    return c;
}

///STRUCTS


typedef unsigned short cell;

cell grid[WIDTH][HEIGHT];

struct player{
    int conn;
    unsigned long token;
    int lastTurn;
    // If the game lasts a unix epoch, some players may be
    // unfairly prevented from moving during one second.
};

typedef struct player player;
player players[1<<16];

int playing;

int width;
int height;
int turn;


int new_player(int sock){
    if(playing>=MAXPLAYERS)
        return -1;
    cell idx = sock|0x8000; //TODO: do something random
    
    return;
}

#define CHECK(cond)\
    if(!(cond)){\
        snd(sock,errResp);\
        close(sock);\
        return;\
    }

void handle(int sock, char* request){
    if(memcmp(request,"GET ",4)==0){
        request+=4;
        if (memcmp(request,"/ ",2)==0)
            snd(sock,startResp);
        else snd(sock,errResp);
    }
    else if(memcmp(request,"POST ",5)==0){
        request+=5;
        CHECK(*request++ =='/')
        if (*request==' '){
            new_player(sock);
            return;// don't close socket
        }
        // Playing a move
        snd(sock, errResp);
    }
    else CHECK(false)
    close(sock);
}




/// Initialization
char tohexdig(int n){
  if(n<10) return 0x30+n;
  else return 0x61-10+n;
}
int mkChunk(char* str){
    int l = strlen(str) - 6; // xx/r/nbody/r/n
    if (l>=256 || l<0){
        return -1;
    }
    str[0] = tohexdig(l/16);
    str[1] = tohexdig(l%16);
    puts(str);
    return 0;
}
int setup(){
    b64setup();
    FILE * file = fopen("init.html","r");
    if (file==0){return -2;}
    int k = fread(initChunk+6,1,10000,file);
    if(ferror(file)) return -3;
    if(!feof(file)) return -4;
    sprintf(initChunk,"%04x\r",k);
    initChunk[5]='\n';
    sprintf(initChunk+k+6,"\r\n");
    fclose(file);
    return 0;
}

int main(){
    puts("let's begin");
    if (setup()){
        puts("setup failed");
        return 1;
    }
    int socket_desc;
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc <0){
        puts("Could not create socket");
        return 1;
    }

    int enable = 1;
    //Really helps debugging
    setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    struct sockaddr_in server, client;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    if ( bind(socket_desc, (struct sockaddr *) &server, sizeof(server)) <0){
        perror("bind failed");
        return 1;
    }

    listen(socket_desc, 3);
    puts("Listening");
    while(true){
        int k;
        int new_socket = accept(socket_desc, (struct sockaddr*) &client,
                                (socklen_t*) &k);
        if(new_socket<0) break;
        char request[101];
        int c = recv(new_socket, request,100, 0);
        if(c>0){
            request[c]=0;
            printf("count: %d\n",c);
            puts(request);
            handle(new_socket,request);
        }
        else{
            perror("recv failed");
            close(new_socket);
        }
    }
    return 0;
}
