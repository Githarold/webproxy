#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
int connect_endServer(char *hostname,int port,char *http_header);
void *thread(void *vargs);

pthread_mutex_t mutex;

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  pthread_mutex_init(&mutex, NULL);

  if (argc != 2) { 
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  signal(SIGPIPE, SIG_IGN);

  listenfd = Open_listenfd(argv[1]);

  while (1) {
    clientlen = sizeof (clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, (void *)connfd);
  }

  pthread_mutex_destroy(&mutex);

  return 0;
}

void *thread(void *vargs) {
  int connfd = (int)vargs;
  Pthread_detach(pthread_self());
  doit(connfd);
  close(connfd);
}

void doit(int connfd) {
  int end_serverfd;

  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char endserver_http_header[MAXLINE];

  char hostname[MAXLINE], path[MAXLINE];
  int port;

  rio_t rio, server_rio;

  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET")) {
    printf("Proxy does not implement this method");
    return;
  }

  parse_uri(uri, hostname, path, &port);
  build_http_header(endserver_http_header, hostname, path, port, &rio);
  end_serverfd = connect_endServer(hostname, port, endserver_http_header); 
  if (end_serverfd < 0) {
    printf("connection failed\n");
    return;
  }

  Rio_readinitb(&server_rio, end_serverfd); 
  Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));
  
  size_t n;
  while((n = Rio_readlineb(&server_rio,buf, MAXLINE)) != 0) { 
    printf("proxy received %d bytes,then send\n",n);
    Rio_writen(connfd, buf, n);
  }
  Close(end_serverfd);
}

void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio) {
  char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];

  sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);

  while(Rio_readlineb(client_rio, buf, MAXLINE)>0) {  

    if (strcmp(buf, "\r\n") == 0) 
      break; 


    if (!strncasecmp(buf, "Host", strlen("Host"))) { 
      strcpy(host_hdr, buf);
      continue;
    }

    if (strncasecmp(buf, "Connection", strlen("Connection"))
        && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection"))
        && strncasecmp(buf, "User-Agent", strlen("User-Agent"))) {
      strcat(other_hdr,buf);  
    }
  }

  if (strlen(host_hdr) == 0) {
      sprintf(host_hdr,"Host: %s\r\n",hostname);
  }

  sprintf(http_header,"%s%s%s%s%s%s%s",
          request_hdr,
          host_hdr
          "Connection: close\r\n",
          "Proxy-Connection: close\r\n",
          user_agent_hdr,
          other_hdr,
          "\r\n");
  return ;
}

inline int connect_endServer(char *hostname,int port,char *http_header) {
  char portStr[100];
  sprintf(portStr,"%d",port);
  return Open_clientfd(hostname,portStr);
}

void parse_uri(char *uri,char *hostname,char *path,int *port) {
  *port = 80;

  char* pos = strstr(uri,"//");
  pos = pos!=NULL? pos+2:uri;

  char *pos2 = strstr(pos,":"); 

  if (pos2!=NULL) { 
    *pos2 = '\0';
    sscanf(pos,"%s",hostname);
    sscanf(pos2+1,"%d%s",port,path);
  }
  
  else {
    pos2 = strstr(pos,"/");
    if (pos2!=NULL) {
      *pos2 = '\0';
      sscanf(pos,"%s",hostname);
      *pos2 = '/';
      sscanf(pos2,"%s",path);
    } else {
      sscanf(pos,"%s",hostname);
    }
  }
  return;
}
