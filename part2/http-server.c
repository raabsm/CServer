

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

static void die(const char *s) { perror(s); exit(1); }

void sendError(int sock, char *message){
    char msg[500]; 
    snprintf(msg, sizeof(msg), 
              "HTTP/1.0 %s \r\n"
              "\r\n"
              "<<html><body><h1>%s</h1></body></html>", message, message);
     send(sock, msg, strlen(msg), 0);

}

int main(int argc, char **argv)
{
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        die("signal() failed");
 
    if (argc != 5) {
        fprintf(stderr, "usage: %s <server_port> <web_root> <mdb-lookup-host> <mdb-lookup-port>\n", argv[0]);
        exit(1);
    }

    unsigned short port = atoi(argv[1]);
    char *webRoot = argv[2];

    // Create a listening socket (also called server socket) 

    int servsock;
    if ((servsock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("socket failed");

    // Construct local address structure

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // any network interface
    servaddr.sin_port = htons(port);

    // Bind to the local address

    if (bind(servsock, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
        die("bind failed");

    // Start listening for incoming connections

    if (listen(servsock, 5 /* queue size for connection requests */ ) < 0)
        die("listen failed");

    int clntsock;
    socklen_t clntlen;
    struct sockaddr_in clntaddr;
    FILE *fd;
    char buf[4096];

    while(1){
        
        clntlen = sizeof(clntaddr);
        
        if((clntsock =  accept(servsock, (struct sockaddr *) &clntaddr, &clntlen)) < 0)
            die("accept failed");

        if ((fd = fdopen(clntsock, "r")) == NULL)
            die("fdopen failed");       
        
        if (fgets(buf, sizeof(buf), fd) == NULL) {
            if (ferror(fd))
                die("IO error");
            else {
                fclose(fd);
                continue;
            }
        } 
        
        char *responseCode;
        char *client_ip = inet_ntoa(clntaddr.sin_addr);
        char *token_separators = "\t \r\n"; // tab, space, new line
        char *method = strtok(buf, token_separators);
        char *requestURI = strtok(NULL, token_separators);
        char *httpVersion = strtok(NULL, token_separators);
        printf("%s, %s, %s\n", method, requestURI, httpVersion);
        
        if(strcmp("GET", method) != 0 || ((strcmp("HTTP/1.0", httpVersion) != 0) && strcmp("HTTP/1.1", httpVersion) != 0))
        {
            sendError(clntsock, "501 Not Implemented");
            responseCode = "501 Not Implemented";
        }
        else if((*requestURI) != '/' || strstr(requestURI, "..")!= NULL){
            sendError(clntsock, "400 Bad Request");
            responseCode = "400 Bad Request";
        }
        
        fclose(fd);






    }

}


