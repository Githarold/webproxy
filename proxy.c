#include <stdio.h>
#include "csapp.h"

/* Predefined HTTP header components for the proxy */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

/* Key strings used in HTTP headers */
static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";
pthread_mutex_t mutex;

void *thread(void *vargp);
void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
void format_log_entry(char *browser_ip, char *url, size_t size);
int connect_endServer(char *hostname, int port, char *http_header);

ssize_t Rio_readn_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);

int main(int argc, char **argv) {
    /* Main function: sets up a server listening for connections */
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    pthread_t tid;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    // Initialize mutex for thread synchronization
    pthread_mutex_init(&mutex, NULL);

    // Check if port number is provided as argument
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1); // Exit if port number is not provided
    }

    signal(SIGPIPE, SIG_IGN);

    // Open a listening socket on the provided port
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // Retrieve and print the client's hostname and port number
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        // Create a new thread to handle the client connection
        Pthread_create(&tid, NULL, thread, connfdp);
    }

    // Destroy the mutex before the program terminates
    pthread_mutex_destroy(&mutex);

    return 0;
}

void *thread(void *vargp){
    /* Thread function to handle each client connection */
    int connfd = *(int *)vargp;
    free(vargp);
    Pthread_detach(pthread_self());

    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int connfd) {
    /* Handles the HTTP transaction for a client */
    int port, end_serverfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char endserver_http_header[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    rio_t rio, server_rio;

    Rio_readinitb(&rio, connfd);
    if (Rio_readlineb_w(&rio, buf, MAXLINE) == 0)
        return;  // EOF or error

    sscanf(buf, "%s %s %s", method, uri, version); // Parse the request line

    // Check if the method is GET, the only method implemented by this proxy
    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement the method\n");
        return;
    }

    // Parse the URI to extract hostname, path, and port
    parse_uri(uri, hostname, path, &port);

    // Build the HTTP header to be sent to the end server
    build_http_header(endserver_http_header, hostname, path, port, &rio);

    // Connect to the end server
    end_serverfd = connect_endServer(hostname, port, endserver_http_header);
    if (end_serverfd < 0) {
        fprintf(stderr, "Error: Failed to connect to server %s\n", hostname);
        return;
    }

    // Initialize robust I/O for the connection with the end server
    Rio_readinitb(&server_rio, end_serverfd);

    // Write the built HTTP header to the end server
    Rio_writen_w(end_serverfd, endserver_http_header, strlen(endserver_http_header));

    size_t n;
    size_t total_size = 0;

    // Read the response from the end server and forward it to the client
    while ((n = Rio_readlineb_w(&server_rio, buf, MAXLINE)) != 0) {
        if (n < 0) {  // Error reading from server
            fprintf(stderr, "Error: Failed to read response from server\n");
            break;
        }
        // Write the server's response to the client
        Rio_writen_w(connfd, buf, n);
        total_size += n;
    }

    Close(end_serverfd); // Close the connection to the end server

    // Log the request if any data was transferred
    if(total_size > 0)
    {
        format_log_entry(hostname, uri, total_size);
    }    
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {
    /* Constructs the HTTP header for forwarding the request to the end server */
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
    sprintf(request_hdr, requestlint_hdr_format, path);

    // Read and process each line from the client's HTTP header
    while (Rio_readlineb_w(client_rio, buf, MAXLINE) > 0) {
        if (strcmp(buf, endof_hdr) == 0) break;

        // Check for host key in the header and copy it to host_hdr
        if (!strncasecmp(buf, host_key, strlen(host_key))) {
            strcpy(host_hdr, buf);
            continue;
        }

        // Concatenate other relevant headers to other_hdr
        if (!strncasecmp(buf, connection_key, strlen(connection_key)) &&
            !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) &&
            !strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
            strcat(other_hdr, buf);
        }
    }
    // If no host header was provided, use the hostname from the URI
    if (strlen(host_hdr) == 0) {
        sprintf(host_hdr, host_hdr_format, hostname);
    }
    // Construct the complete HTTP header
    sprintf(http_header, "%s%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, prox_hdr, user_agent_hdr, other_hdr, endof_hdr);
}

inline int connect_endServer(char *hostname,int port,char *http_header) {
    /* Function to establish a connection with the end server */
    char portStr[100];
    sprintf(portStr,"%d",port);
    return Open_clientfd(hostname, portStr);
}

void parse_uri(char *uri, char *hostname, char *path, int *port) {
    /* Parses the URI to obtain hostname, path, and port number */
    *port = 80; // Default port number is 80
    char *pos = strstr(uri, "//");

    pos = pos != NULL ? pos + 2 : uri;

    char *pos2 = strstr(pos, ":");
    if (pos2 != NULL) {
        // If a port is specified in the URI
        *pos2 = '\0';
        sscanf(pos, "%s", hostname);
        sscanf(pos2 + 1, "%d%s", port, path);
    } else {
        // If no port is specified in the URI
        pos2 = strstr(pos, "/");
        if (pos2 != NULL) {
            *pos2 = '\0';
            sscanf(pos, "%s", hostname);
            *pos2 = '/';
            sscanf(pos2, "%s", path);
        } else {
            sscanf(pos, "%s", hostname);
        }
    }
}

void format_log_entry(char *browser_ip, char *url, size_t size) {
    /* Formats and logs each HTTP request */
    time_t now;
    char time_str[MAXLINE];

    // Get the current time
    time(&now);

    // Format the time into a readable string
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    // Prepare the log entry
    char log_entry[MAXLINE];
    sprintf(log_entry, "%s: %s %s %zu\n", time_str, browser_ip, url, size);

    // Write the log entry to a file
    pthread_mutex_lock(&mutex); // Lock mutex for thread-safe file access
    FILE *log_file = fopen("proxy.log", "a");
    if (log_file != NULL) {
        fputs(log_entry, log_file);
        fclose(log_file);
    }
    pthread_mutex_unlock(&mutex); // Unlock mutex
}

/* Wrapper functions for robust I/O operations */
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
