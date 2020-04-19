#include<arpa/inet.h>
#include<netinet/in.h>
#include<signal.h>
#include<stdbool.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<unistd.h>
#include<sys/random.h>
#include<time.h>

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

char errFullResp[] = "HTTP/1.1 503 Server Full\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Connection: close\r\nCache-Control: no-cache\r\n"
"Content-Length: 35\r\n"
"\r\n"
"Sorry, The server is full right now";

char chunkedHeaders[] = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Connection: close\r\n"
"X-Content-Type-Options: nosniff\r\n"
"Cache-Control: no-cache\r\n"
"Transfer-Encoding: Chunked\r\n\r\n";

char startResp[] = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Connection: close\r\n"
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

// WIDTH=1024, 4*(1026/3)= 1368
#define DATASIZE (2*HEIGHT)
char dataChunk[4*(DATASIZE/3)+31] = "56C\r\n"
"\n<script>"
"d(\"X";
char* dataStart;

char startChunk[] = "__\r\n"
"<script>"
"tok=\"TTTTTTTTTTTT\";"
"plnum=NNNNNN;"
"start(XXXX ,YYYY ,WWWW ,HHHH );"
"</script>"
"\r\n";

char* starttok;
char* startplnum;
char* startp[2];
char* startsz[2];


///UTILS

int snd(int sock, char* msg){
    printf("\nsending message to socket %d\n",sock);
    puts(msg);
    int l = strlen(msg);
    while (l>0){
        int c = send(sock,msg,l,0);
        printf("sent: %d\n",c);
        if(c<0){perror("send failed"); close(sock); return -1;}
        l-=c;
    }
    return 0;
}
char tohexdig(int n){
  if(n<10) return 0x30+n;
  else return 0x61-10+n;
}

char b64[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "abcdefghijklmnopqrstuvwxyz"
               "0123456789+/";

unsigned char b64inv[256];

char* b64e(unsigned char* in, unsigned char* out, int len){
    int c=0;
    int tmp=0;
    while(len--){
        tmp |= *(in++);
        if((++c)%3==0){
            for(int i=0;i<4;i++){
                *(out++)=b64[(tmp>>18)&0x3F];
                tmp<<=6;
            }
            tmp=0;
        }
        else tmp<<=8;
    }
    int k = c%3;
    if(k){
        tmp<<=(2-k)*8;
        for(int i=0;i<4;i++){
            *(out++) = i>k?'=':b64[(tmp>>18)&0x3F];
            tmp<<=6;
        }
    }
    return out;
}

int b64d(unsigned char* in, unsigned char* out, int len){
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
    int k = c%4;
    tmp<<=(3-k)*6;
    for(int i=0;i<k-1;i++){
        *(out++)=(tmp>>16)&0xFF;
        tmp<<=8;
    }
    return c;
}

void b64setup(){
    for(int i=0;i<256;i++)
        b64inv[i]=0xFF;
    for(char i=0;i<64;i++)
        b64inv[b64[i]]=i;
        /*TESTING
    char s[100] = "XXXXXXXXXX";
    int x = b64d("UVdFUlQ=",s,5);
//    s[x]='\0';
    printf("%d %s\n",x, s);
    exit(0);*/
}

///STRUCTS
typedef unsigned short cell; // 0 means off, 1 means on but unowned

cell base[WIDTH][HEIGHT];
cell backup[WIDTH][HEIGHT];

cell (*grid)[HEIGHT] = base;

struct player{
    int conn;
    unsigned long token;
    short move[2];
    int ref;
};

typedef struct player player;
player players[1<<16];

int ixs[MAXPLAYERS];
int plays[MAXPLAYERS+1];
int playing;

int width; //for dynamic resizing
int height;

cell (*next)[HEIGHT] = backup;

//GoL
cell get(int x, int y){
    return grid[(width+x%width)%width][(height+y%height)%height];
}

void step(){
    for(int x=0;x<width;x++) for(int y=0;y<width;y++){
      cell v=grid[x][y];
      if(v){
          int numset=0;
          for(int i=-1;i<2;i++)
            for(int j=-1;j<2;j++)
              if(i||j)
              {if(get(x+i,y+j)) numset+=1;}
          next[x][y]=((numset==2 || numset==3)* v);
      }else{
          cell fst = 0;
          cell snd = 0;
          cell res = 0;
          for(int i=-1;i<2;i++) for(int j=-1;j<2;j++)
            if(i||j){
              cell k=get(x+i,y+j);
              if(k){
                if(snd && !fst) res=0;
                else if (snd){
                  if(fst==snd || fst==k) res=fst;
                  else if (snd==k) res=snd;
                  else res=1;
                  fst=0;
                }
                else {
                  snd=fst;
                  fst=k;
                }
              }
          }
          next[x][y]=res;
        /*
        for(int i=-1;i<2;i++) for(int j=-1;j<2;j++) if (i||j){
            cell k = get(x+i,y+j);
        }*/
      }
    }
    void* tmp=grid;
    grid=next;
    next=tmp;
    int i=0;
    while(players[plays[i]].move[0]==-1){
        short* x = players[plays[i]].move;
        if(grid[x[0]][x[1]]==0){
            //Add move to script
            grid[x[0]][x[1]]=1;
        }
        players[plays[i]].move[0]=-1;
    }
    // Consider changing width/height
}


/// SENDING DATA

int sendDataChunk(int sock, char* dat,int len){
    char* nxt = b64e(dat, dataStart, len);
    strcpy(nxt, "\")</script>\r\n");
    int x = strlen(dataChunk)-7;
    dataChunk[0]=tohexdig(x>>8);
    dataChunk[1]=tohexdig((x>>4)&0xF);
    dataChunk[2]=tohexdig(x&0xF);
    return snd(sock, dataChunk);
}
int sendData(int sock){
    int i=0;
    while(i<width){
        if(sendDataChunk(sock, ((char*)grid[i]), height*2))
            return -1;
        i++;
    }
    //return sendDataChunk(sock, ((char*)grid)+i*DATASIZE, 2*width*height - i*);
    return 0;
}

int sendStart(int idx, short* startpos ){
    sprintf(startp[0],"%4d ,%4d ,%4d ,%4d",
        startpos[0],startpos[1],width,height);
    startsz[1][4]=' ';
    sprintf(startplnum,"%6d",idx);
    startplnum[6]=';';
    b64e((char*) &players[idx].token, starttok, sizeof(long));
    return snd(players[idx].conn,startChunk);
}


/// REQUEST HANDLERS

int remove_player(int idx){
    if(!players[idx].conn) return 1;
    //close socket? - No becuase snd already does that
    players[idx].conn=0;
    int r = players[idx].ref;
    ixs[r] = ixs[--playing];
    players[ixs[r]].ref=r;
    return 0;
    // Consider decolouring cells belonging to the player
    // Performance considerations apply if many disconnect simultaneously
}

int new_player(int sock){
    if(playing>=MAXPLAYERS){
        if(!snd(sock,errFullResp))
          close(sock);
        return -1;
    }
    // Setup players and ixs
    cell idx = sock|0x8000; //TODO: do something random
    while(players[idx].conn)idx=((idx+1)&0x7FFF)|0x8000;
    getrandom(&players[idx].token,sizeof(long) ,0);
    players[idx].conn=sock;
    players[idx].move[0]=-1;
    players[idx].ref=playing;
    ixs[playing ++]=idx;
    
    // Setup start location
    short startpos[2];
    getrandom(startpos, 2* sizeof(short) ,0);
    startpos[0]=(width+startpos[0]%width)%width;
    startpos[1]=(height+startpos[1]%height)%height;
    for(int i=width-1;i<width+3;i++)
      for(int j=height-1;j<height+3;j++){
        grid[(i+startpos[0])%width][(j+startpos[1])%height]=((i&2)||(j&2))?0:idx;
        //printf("%d,%d:%d\n", (i+startpos[0])%width, (j+startpos[1])%height,grid[(i+startpos[0])%width][(j+startpos[1])%height]);
    }
    if(snd(sock,chunkedHeaders)
      || snd(sock,initChunk)
      || sendStart(idx,startpos)
      || sendData(sock)
    ){
        remove_player(idx);
    }
    return 0;
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
        CHECK(*(request++) =='/')
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
    if (mkChunk(startChunk)) return -1;
    starttok = strchr(startChunk,'T');
    startplnum = strchr(startChunk,'N');
    startp[0] = strchr(startChunk,'X');
    startp[1] = strchr(startChunk,'Y');
    startsz[0] = strchr(startChunk,'W');
    startsz[1] = strchr(startChunk,'H');
    dataStart = strchr(dataChunk,'X');
    FILE * file = fopen("init.html","r");
    if (file==0){return -2;}
    int k = fread(initChunk+6,1,10000,file);
    if(ferror(file)) return -3;
    if(!feof(file)) return -4;
    sprintf(initChunk,"%04x\r",k);
    initChunk[5]='\n';
    sprintf(initChunk+k+6,"\r\n");
    fclose(file);
    width=32;
    height=32;
    return 0;
}

time_t prev;

int main(){
    puts("let's begin");
    if (setup()){
        puts("setup failed");
        return 1;
    }
    int sock_listen;
    sock_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_listen <0){
        puts("Could not create socket");
        return 1;
    }

    //Really helps debugging
    int enable = 1;
    setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    struct timeval timeout;
    timeout.tv_sec=1;
    timeout.tv_usec=0;
    if(setsockopt(sock_listen, SOL_SOCKET, SO_RCVTIMEO,
       &timeout, sizeof(timeout))<0){
        puts("setting timeout failed");
        return 1;
    }
    struct sockaddr_in server, client;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    if ( bind(sock_listen, (struct sockaddr *) &server, sizeof(server)) <0){
        perror("bind failed");
        return 1;
    }

    listen(sock_listen, 10);
    puts("Listening");
    while(true){
        int k;
        int new_socket = accept(sock_listen, (struct sockaddr*) &client,
                                (socklen_t*) &k);
        if(new_socket>0){
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
        time_t new = time(0);
        if(new!=prev){
            step();
            new=prev;
        }
    }
    return 0;
}
