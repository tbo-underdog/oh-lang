#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
int main(void){
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0) return 1;
    int one=1;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    struct sockaddr_in sa={0};
    sa.sin_family=AF_INET;
    sa.sin_port=htons(8080);
    sa.sin_addr.s_addr=INADDR_ANY;
    if(bind(fd,(struct sockaddr*)&sa,sizeof(sa))<0) return 2;
    listen(fd,16);
    const char*resp="HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 15\r\nConnection: close\r\n\r\nHello from Oh!\n";
    size_t n=strlen(resp);
    for(;;){
        int c=accept(fd,0,0);
        if(c<0) return 3;
        write(c,resp,n);
        close(c);
    }
    return 0;
}
