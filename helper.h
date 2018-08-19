
#ifndef HELPER_H
#define HELPER_H

int set_servinfo(char *port[], char *addr[], struct addrinfo **servinfo[]);

int set_addrinfo(const char *addr, const char *port, int sock_type, struct addrinfo **info);

int set_socketfd(int *sockfd, struct addrinfo *info);

void get_sockinfo(struct sockaddr_in *sin, uint16_t *port, char *ip4str);

void ntohl_arr(int len, uint32_t *buf);

void htonl_arr(int len, uint32_t *buf);

void print_arr(int len, uint32_t *buf);

uint32_t *recv_handler(int len, int *recv, int sockfd, struct sockaddr *from, socklen_t *fromlen, int flag);

int send_handler(int len, uint32_t *buf, int sockfd, struct sockaddr *to, socklen_t tolen, int flag);


#endif