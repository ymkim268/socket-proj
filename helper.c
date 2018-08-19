#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "helper.h"

#define MAXSENDLEN 65000 // bytes

void ntohl_arr(int len, uint32_t *buf) {
	int i;
	for(i = 0; i < len; i++) {
		buf[i] = ntohl(buf[i]);
	}
}

void htonl_arr(int len, uint32_t *buf) {
	int i;
	for(i = 0; i < len; i++) {
		buf[i] = htonl(buf[i]);
	}
}

void print_arr(int len, uint32_t *buf) {
	int i;
	printf("[ ");
	for(i = 0; i < len; i++) {
		printf("%u ", buf[i]);
	}
	printf("]\n");
}

void get_sockinfo(struct sockaddr_in *sin, uint16_t *port, char *ip4str) {
	*port = ntohs(sin->sin_port);
	inet_ntop(sin->sin_family, &(sin->sin_addr.s_addr), ip4str, INET_ADDRSTRLEN);
}


int set_addrinfo(const char *addr, const char *port, int sock_type, struct addrinfo **info) {
	int errno;

	// Prepare socket
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = sock_type;
	hints.ai_flags = AI_PASSIVE; // use my IP

	// Call getaddrinfo to pack information about hostname/server and load struct sockaddr
	if((errno = getaddrinfo(addr, port, &hints, info)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(errno));
		freeaddrinfo(*info);
	}

	return errno;
}


int set_servinfo(char *port[], char *addr[], struct addrinfo **servinfo[]) {
	int i, retno;

	for(i = 0; i < 3; i++) {
		if((retno = set_addrinfo(addr[i], port[i], SOCK_DGRAM, servinfo[i])) != 0) {
			return retno;
		}
	}

	/*
	printf("DEBUG: set_servinfo() status:\n");
	printf("\tset servinfo for %s, %s, %s\n", port[0], port[1], port[2]);
	printf("\tset servinfo for %s, %s, %s\n", addr[0], addr[1], addr[2]);
	
	 
	printf("\tretno = %d\n", retno);

	printf("\tadd of servinfo[0], *servinfo[0]: %p, %p\n", servinfo[0], *servinfo[0]);
	printf("\tadd of servinfo[1], *servinfo[1]: %p, %p\n", servinfo[1], *servinfo[1]);
	printf("\tadd of servinfo[2], *servinfo[2]: %p, %p\n", servinfo[2], *servinfo[2]);
	*/
	return retno;
}


int set_socketfd(int *sockfd, struct addrinfo *info) {
	int bindst, sockoptst;
	int optval = 1; // used for setsockopt

	int i = 0;
	struct addrinfo *p;
	for(p = info; p != NULL; p = p->ai_next) {
		// create socket
		if(((*sockfd) = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1) {
			perror("socket");
			continue;
		}
		// in case socket is haning onto the port; reuse port
		if((sockoptst = setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR,  &optval, sizeof(optval))) == -1) {
			perror("setsockopt");
			return sockoptst; // kill program
		}
		// bind socket
		if((bindst = bind(*sockfd, info->ai_addr, info->ai_addrlen)) == -1) {
			close(*sockfd);
			perror("bind");
			continue;
		}

		i++;
		break; // successful bind then break out of loop
	}

	uint16_t port;
	char *ip4str = malloc(INET_ADDRSTRLEN * sizeof(char));
	get_sockinfo((struct sockaddr_in *) info->ai_addr, &port, ip4str);

	char *protocol = "";
	if(info->ai_socktype == 1) {
		protocol = "TCP";
	} else if(info->ai_socktype == 2) {
		protocol = "UDP";
	}

	printf("SETUP: information of socket\n");
	printf("\tIP: %s on PORT: %d of TYPE: %s\n", ip4str, port, protocol);

	int sendbuff, recvbuff, res;
	socklen_t optlen;

	optlen = sizeof(sendbuff);
	res = getsockopt(*sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuff, &optlen);
	res = getsockopt(*sockfd, SOL_SOCKET, SO_RCVBUF, &recvbuff, &optlen);

	if(res == -1) {
     	fprintf(stderr, "ERROR getsockopt: find buffer size!\n");
	} else {
		printf("\tcurrent send buf size = %d bytes.\n", sendbuff);
		printf("\tcurrent recv buf size = %d bytes.\n", recvbuff);
 	}

/* 	// trying to increase send buf size ** Removed!
 	sendbuff = MAXSENDLEN; // bytes
 	printf("\tset sendbuff size to %d bytes.\n", sendbuff);
 	res = setsockopt(*sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
 	if(res == -1) {
 		fprintf(stderr, "ERROR setsockopt: set buffer size!\n");
 	}

 	res = getsockopt(*sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuff, &optlen);
 	if(res == -1) {
     	fprintf(stderr, "ERROR getsockopt: find buffer size!\n");
	} else {
		printf("\tnew buffer size = %d\n", sendbuff);
 	}
 */

/*
	printf("DEBUG: set_socketfd() status:\n");
	printf("\tnum of entries before bind = %d\n", i);
	printf("\tsetsockopt = %d\n", sockoptst);
	printf("\tbind = %d\n", bindst);
*/

	if(p == NULL) {
		fprintf(stderr, "socket failed to bind\n");
		return -1;
	}

	freeaddrinfo(info);
	return bindst;
}


/* 
	Return buf received from sender and recv_bytes = num of bytes received
	Need to free buf!
	Param: len = set size of receive buffer
	POST: buf is modified from network to host bytes
	PRE: flag = 1 TCP and flag = 2 UDP
		 if TCP, to = null and tolen = 0
*/
uint32_t *recv_handler(int len, int *recv_bytes, int sockfd, struct sockaddr *from, socklen_t *fromlen, int flag) {
	/* expected to receive an array of size len */
	uint32_t *buf = malloc(len * sizeof(uint32_t));
	if(buf == NULL) {
		fprintf(stderr, "ERROR: malloc failed at recv_handler!\n");
		return NULL;
	}

	/* Receive data from sender */
	int num_bytes = 0;
	if(flag == 2) { // UDP
		if((num_bytes = recvfrom(sockfd, buf, len * sizeof(uint32_t), 0, from, fromlen)) == -1) {
			perror("recvfrom");
			return NULL;
		}
	} else if(flag == 1) { // TCP
		if((num_bytes = recv(sockfd, buf, len * sizeof(uint32_t), 0)) == -1) {
            perror("recv");
            return NULL;
        }
	} else {
		return NULL;
	}
	*recv_bytes = num_bytes; // set recv mem loc to num of bytes received

	/* Process data using ntohl (network to host long) */
	int num_vals = num_bytes / sizeof(uint32_t);
	(void) ntohl_arr(num_vals, buf); // buf gets modified

	return buf;
}

/*
	Send buf with size of len
	Return num of bytes sent else on error -1
	PRE: values in buf are NOT in network bytes
		 flag = 1 TCP and flag = 2 UDP
		 if TCP, to = null and tolen = 0
	POST: buf is modified from host to network bytes

*/
int send_handler(int len, uint32_t *buf, int sockfd, struct sockaddr *to, socklen_t tolen, int flag) {
	/* Process data using htonl (host to network long) */
	(void) htonl_arr(len, buf); 

	/* Send buf to receiver */
	int num_bytes = 0;
	if(flag == 2) {
		if((num_bytes = sendto(sockfd, buf, len * sizeof(uint32_t), 0, to, tolen)) == -1) {
			perror("sendto");
			return -1;
		}	
	} else if(flag == 1) {
		if((num_bytes = send(sockfd, buf, len * sizeof(uint32_t), 0)) == -1) {
            perror("send");
            return -1;
        }
	} else {
		return -1;
	}

	return num_bytes; //bytes sent
}





