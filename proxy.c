#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";
pthread_mutex_t mutex;

void *thread(void *vargp);
void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port, char *http_header);

ssize_t Rio_readn_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);

int main(int argc,char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE],port[MAXLINE];
    pthread_t tid;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_mutex_init(&mutex, NULL);

    if(argc != 2){
        fprintf(stderr,"usage :%s <port> \n",argv[0]);
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN); // SIGPIPE 예외처리

    listenfd = Open_listenfd(argv[1]);
    while(1){
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd,(SA *)&clientaddr,&clientlen);

        /*print accepted message*/
        Getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
        printf("Accepted connection from (%s %s).\n",hostname,port);
        Pthread_create(&tid, NULL, thread, (void *)connfd);         // 다중 쓰레딩을 통한 conncurrent 접속 구현
        /*sequential handle the client transaction*/
    }
    return 0;
}

void* thread(void *vargp){
    int connfd = (int)vargp;
    Pthread_detach(pthread_self());
    doit(connfd);
    Close(connfd);
}

/*handle the client HTTP transaction*/
void doit(int connfd) {
    int end_serverfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char endserver_http_header[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    int port;
    rio_t rio, server_rio;

    Rio_readinitb(&rio, connfd);
    if (Rio_readlineb_w(&rio, buf, MAXLINE) == 0)
        return;  // EOF or error
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement the method\n");
        return;
    }

    parse_uri(uri, hostname, path, &port);
    build_http_header(endserver_http_header, hostname, path, port, &rio);

    end_serverfd = connect_endServer(hostname, port, endserver_http_header);
    if (end_serverfd < 0) {
        fprintf(stderr, "Error: Failed to connect to server %s\n", hostname);
        return;
    }

    Rio_readinitb(&server_rio, end_serverfd);
    Rio_writen_w(end_serverfd, endserver_http_header, strlen(endserver_http_header));

    size_t n;
    size_t total_size = 0;
    while ((n = Rio_readlineb_w(&server_rio, buf, MAXLINE)) != 0) {
        if (n < 0) {  // Error reading from server
            fprintf(stderr, "Error: Failed to read response from server\n");
            break;
        }
        Rio_writen_w(connfd, buf, n);
        total_size =+ n;
    }

    Close(end_serverfd);

    if(total_size > 0)
    {
        format_log_entry(hostname,uri,total_size);
    }    
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
    sprintf(request_hdr, requestlint_hdr_format, path);

    while (Rio_readlineb_w(client_rio, buf, MAXLINE) > 0) {
        if (strcmp(buf, endof_hdr) == 0) break;

        if (!strncasecmp(buf, host_key, strlen(host_key))) {
            strcpy(host_hdr, buf);
            continue;
        }

        if (!strncasecmp(buf, connection_key, strlen(connection_key)) &&
            !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) &&
            !strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
            strcat(other_hdr, buf);
        }
    }
    if (strlen(host_hdr) == 0) {
        sprintf(host_hdr, host_hdr_format, hostname);
    }
    sprintf(http_header, "%s%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, prox_hdr, user_agent_hdr, other_hdr, endof_hdr);
}

/*Connect to the end server*/
inline int connect_endServer(char *hostname,int port,char *http_header){
    char portStr[100];
    sprintf(portStr,"%d",port);
    return Open_clientfd(hostname,portStr);
}

/*parse the uri to get hostname,file path ,port*/
void parse_uri(char *uri,char *hostname,char *path,int *port)
{
    *port = 80;
    char* pos = strstr(uri,"//");

    pos = pos!=NULL? pos+2:uri;

    char*pos2 = strstr(pos,":");
    if(pos2!=NULL)
    {
        *pos2 = '\0';
        sscanf(pos,"%s",hostname);
        sscanf(pos2+1,"%d%s",port,path);
    }
    else
    {
        pos2 = strstr(pos,"/");
        if(pos2!=NULL)
        {
            *pos2 = '\0';
            sscanf(pos,"%s",hostname);
            *pos2 = '/';
            sscanf(pos2,"%s",path);
        }
        else
        {
            sscanf(pos,"%s",hostname);
        }
    }
    return;
}

void format_log_entry(char *browser_ip, char *url, size_t size)
{
    time_t now;
    char time_str[MAXLINE];

    // Get the current time
    time(&now);

    // Format the time string
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    // Log entry in the required format
    char log_entry[MAXLINE];
    sprintf(log_entry, "%s: %s %s %zu\n", time_str, browser_ip, url, size);

    // Open the log file in append mode and write the log entry
    pthread_mutex_lock(&mutex);
    FILE *log_file = fopen("proxy.log", "a");
    if (log_file != NULL)
    {
        fputs(log_entry, log_file);
        fclose(log_file);
    }
    pthread_mutex_unlock(&mutex);
}

ssize_t Rio_readn_w(int fd, void *usrbuf, size_t n) {
    ssize_t rc;

    if ((rc = rio_readn(fd, usrbuf, n)) < 0) {
        fprintf(stderr, "Rio_readn error: %s\n", strerror(errno));
        return 0;
    }
    return rc;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) {
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        fprintf(stderr, "Rio_readlineb error: %s\n", strerror(errno));
        return 0;
    }
    return rc;
}

ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n) {
    ssize_t rc;

    if ((rc = rio_writen(fd, usrbuf, n)) < 0)
        fprintf(stderr, "Rio_writen error: %s\n", strerror(errno));

    return rc;
}
