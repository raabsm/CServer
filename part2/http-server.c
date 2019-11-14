
#include <netdb.h>
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
              "<html><body><h1>%s</h1></body></html>", message, message);
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

    const char *submit_form =
        "HTTP/1.0 200 OK\r\n"
        "\r\n"
        "<html><body>\n"
        "<h1>mdb-lookup</h1>\n"
        "<p>\n"
        "<form method=GET action=/mdb-lookup>\n"
        "lookup: <input type=text name=key>\n"
        "<input type=submit>\n"
        "</form>\n"
        "<p>\n";

    unsigned short servport = atoi(argv[1]);
    char *webRoot = argv[2];
    char *dbHost = argv[3];
    unsigned short dbPort = atoi(argv[4]);
    char *db_ip;

    struct hostent *db_he;
    if((db_he = gethostbyname(dbHost)) == NULL)
        die("gethostbyname failed");
    db_ip = inet_ntoa(*(struct in_addr *)db_he->h_addr);    

    //Create 

    // Create a listening socket (also called server socket) 

    int servsock;
    if ((servsock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("socket failed");

    int dbsock;
    if ((dbsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        die("socket failed");

    
    struct sockaddr_in dbaddr;

    memset(&dbaddr, 0, sizeof(dbaddr));
    dbaddr.sin_family = AF_INET;
    dbaddr.sin_addr.s_addr = inet_addr(db_ip); // any network interface
    dbaddr.sin_port = htons(dbPort);

     if (connect(dbsock, (struct sockaddr *)&dbaddr, sizeof(dbaddr)) < 0) {
         die("connect failed");
     }

    // Construct server address structure

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // any network interface
    servaddr.sin_port = htons(servport);

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
    char requestLine[4096];

    while(1){
        
        clntlen = sizeof(clntaddr);
        
        if((clntsock =  accept(servsock, (struct sockaddr *) &clntaddr, &clntlen)) < 0)
            die("accept failed");

        if ((fd = fdopen(clntsock, "r")) == NULL)
            continue;       
        
        if (fgets(requestLine, sizeof(requestLine), fd) == NULL) {
            if (ferror(fd))
                die("IO error");
            else {
                fclose(fd);
                continue;
            }
        } 
        
        char hostname[500];
        gethostname(hostname, 500);
        struct hostent* host;
        host = gethostbyname(hostname);
        strcpy(hostname,host->h_name);

        char *responseCode;
        char *client_ip = inet_ntoa(clntaddr.sin_addr);
        char *token_separators = "\t \r\n"; // tab, space, new line
        char *method = strtok(requestLine, token_separators);
        char *requestURI = strtok(NULL, token_separators); 
        char *httpVersion = strtok(NULL, token_separators);
        char *extra = strtok(NULL, token_separators);
       
        if((extra || !method || !requestURI || !httpVersion) 
                    || strcmp("GET", method) != 0 
                    || ((strcmp("HTTP/1.0", httpVersion) != 0) 
                        && strcmp("HTTP/1.1", httpVersion) != 0))
        {
            sendError(clntsock, "501 Not Implemented");
            responseCode = "501 Not Implemented";
        }
        else if((*requestURI) != '/' || (strstr(requestURI, "..")!= NULL)){
            sendError(clntsock, "400 Bad Request");
            responseCode = "400 Bad Request";
        }
        
        else{
            char file_path[1000];
            strcpy(file_path, requestURI);

            while(1){
                if (fgets(buf, sizeof(buf), fd)== NULL){
                    if (ferror(fd))
                        die("IO error");
                    else{
                        fclose(fd);
                        continue;
                    }
                }
                if(strcmp("\r\n", buf) == 0)
                    break;
            }
             
            if(*(file_path + strlen(file_path) - 1) == '/'){
                strcat(file_path, "index.html");
            }
            char *mdb_lookup = "/mdb-lookup";
            if (strncmp(mdb_lookup, file_path, strlen(mdb_lookup)) == 0){
                send(clntsock, submit_form, strlen(submit_form), 0);
                char key[4096] = "?key=";
                if((strlen(file_path) >= strlen(key) + strlen(mdb_lookup)) 
                        && strncmp(key, file_path + strlen(mdb_lookup),strlen(key))==0){
                    strcpy(key, file_path + strlen(mdb_lookup) + strlen(key));
                    key[strlen(key)+1] = 0;
                    key[strlen(key)] = '\n';
                    send(dbsock, key, strlen(key), 0);
                    FILE *dbInfo;
                    dbInfo  = fdopen(dbsock, "r");
                    char *table = "<p><table border>\n";
                    send(clntsock, table, strlen(table), 0);
                    while(fgets(key, sizeof(key), dbInfo) != NULL){
                        if(key[0] == '\n')
                            break;
                        char tableEntry[1000];
                        snprintf(tableEntry, sizeof(tableEntry), "<tr><td>%s\n", key);
                        send(clntsock, tableEntry, strlen(tableEntry), 0);
                    }
                    table = "</table>\n"
                            "</body></html>\n";
                    send(clntsock, table, strlen(table), 0);
                }
                responseCode = "200 OK";               

            }
            else{
            
                strcpy(buf, webRoot);
                strcat(buf, file_path);
                struct stat path;
                int if_exists = stat(buf, &path);
                if(if_exists != 0){
                    sendError(clntsock, "404 Not Found");
                    responseCode = "404 Not Found";
                }
                else if (S_ISDIR(path.st_mode)){
                    responseCode = "301 Moved Permanently";
                    char msg[2000];
                    snprintf(msg, sizeof(msg), 
                            "HTTP/1.0 %s \r\n"
                            "Location: http://%s:%d%s/\r\n"
                            "\r\n"
                            "<<html><body>"
                            "<h1>%s</h1>"
                            "<p>The document has moved"
                            "<a href=\"http://%s:%d%s/\">here</a>."
                            "</p>"
                            "</body></html>", responseCode, hostname, servport, 
                                              file_path, responseCode, hostname,
                                              servport, file_path);
                    send(clntsock, msg, strlen(msg), 0);
                }
                    
                else{
                    FILE *fp = fopen(buf, "r");
                    responseCode = "200 OK";
                    unsigned int n; 
                    snprintf(buf, sizeof(buf), 
                            "HTTP/1.0 %s \r\n"
                            "\r\n", responseCode);
                    send(clntsock, buf, strlen(buf), 0);
                    while((n = fread(buf, 1, sizeof(buf), fp)) > 0 ){
                        send(clntsock, buf, n, 0);
                    }
                    fclose(fp);
                }
            }
        }
        fclose(fd);
        if(!method)
            method = "(null)"; 
        if(!requestURI)
            requestURI = "(null)";
        if(!httpVersion)
            httpVersion = "(null)";
        
        fprintf(stderr, "%s \"%s %s %s\" %s\n", client_ip, method, requestURI, httpVersion, responseCode);

    }

}


