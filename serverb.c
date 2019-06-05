/*

Reference Material 
- Beej's Guide to Network Programming

*/

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

#include "reduction.h"
#include "helper.h"

#define MYIPADDR "127.0.0.1"
#define MYPORT "22843" /* 21000+xxx (last three digits of USC ID) */
#define SERVERNAME "Server B"

/* 
	Return an array containing reduction function and size of incoming data
	Need to free buf returned by recv_hanlder()! 
	Pre: len = 2
*/
uint32_t *recv_function(int *recv, int sockfd, struct sockaddr_storage *from_addr, socklen_t *fromlen) {
	/* EVENT Server received packet of data */
	// printf("STATUS: waiting to recv FUNCTION from front-end server ...\n");
	uint32_t *rbuf;
	if((rbuf = recv_handler(2, recv, sockfd, (struct sockaddr *) from_addr, fromlen, 2)) == NULL) {
		fprintf(stderr, "ERROR: recv_handler failed!\n");
		return NULL;
	}

	// int num_vals = (*recv) / sizeof(uint32_t);
	// printf("\treceived [%d bytes]: %d numbers.\n", (*recv), num_vals);
	// print_arr(num_vals, rbuf);

	// printf("\treduction function type: %s\n", cmdList[rbuf[0]]);
	// printf("\tset buffer size: %d\n", rbuf[1]);

	/* Print Information about their socket */
	// uint16_t port;
	// char ip4str[INET_ADDRSTRLEN];
	// (void) get_sockinfo((struct sockaddr_in *) from_addr, &port, ip4str);

	// printf("DEBUG: printing front-end information\n");
	// printf("\tfront-end server is on IPv4 %s at port %d \n", ip4str, port);
	return rbuf;
}

/* 
	Return an array containing incoming data to perform reduction
	Need to free buf returned by recv_hanlder()! 
	Pre: len = size of incoming data
*/
uint32_t *recv_data(int len, int *recv, int sockfd, struct sockaddr_storage *from_addr, socklen_t *fromlen) {
	/* EVENT Server received data */
	// printf("STATUS: waiting to recv DATA from front-end server ...\n");
	uint32_t *rbuf;
	if((rbuf = recv_handler(len, recv, sockfd, (struct sockaddr *) from_addr, fromlen, 2)) == NULL) {
		fprintf(stderr, "ERROR: recv_handler failed!\n");
		return NULL;
	}

	int num_vals = (*recv) / sizeof(uint32_t);
	printf("The %s has received %d numbers.\n", SERVERNAME, num_vals);
	// printf("\treceived [%d bytes]: %d numbers.\n", (*recv), num_vals);
	// print_arr(num_vals, rbuf);

	/*
	uint16_t port;
	char ip4str[INET_ADDRSTRLEN];
	get_sockinfo((struct sockaddr_in *) from_addr, &port, ip4str);
	printf("\tAWS is on IPv4 %s at port %d \n", ip4str, port);
	*/
	
	return rbuf;
}


int send_status(int sockfd, int status, struct sockaddr_storage *to_addr, socklen_t tolen) {
	// Pack status value as an uint32_t array
	uint32_t *buf = malloc(1 * sizeof(uint32_t));
	if(buf == NULL) {
		fprintf(stderr, "ERROR: malloc failed at send_status!\n");
		return -1;
	}
	buf[0] = status;

	// printf("STATUS: send READY (recv buffer ready) to front-end server.\n");
	int sent;
	if((sent = send_handler(1, buf, sockfd, (struct sockaddr *) to_addr, tolen, 2)) == -1) {
		perror("sendto");
		return -1;
	}
	
	// printf("\tsent ready status as: %d\n", status);
	free(buf);
	return sent;
}

int send_reduction(int sockfd, int len, uint32_t *buf, struct sockaddr_storage *to_addr, socklen_t tolen) {
	// printf("STATUS: send final reduction value(s) to front-end server.\n");
	
	int sent; //bytes sent
	if((sent = send_handler(len, buf, sockfd, (struct sockaddr *) to_addr, tolen, 2)) == -1) {
		perror("sendto");
		return -1;
	}

	/*
	if(len == 1) {
		printf("\treduced and sent: %u\n", ntohl(buf[0]));
	} else {
		printf("\treduced and sent:\n");
		ntohl_arr(len, buf);
		print_arr(len, buf);
	}
	*/

	return sent;
}



int main() {
	struct addrinfo *servinfo;
	int retno; // return errorno

	if((retno = set_addrinfo(MYIPADDR, MYPORT, SOCK_DGRAM, &servinfo)) != 0) {
		return retno;
	}

	int sockfd;
	if((retno = set_socketfd(&sockfd, servinfo)) != 0) {
		freeaddrinfo(servinfo);
		return retno;
	}

	/* EVENT Server Booting Up: setup socket */ 
	struct sockaddr_in sin;
	socklen_t addrlen = sizeof(sin);
	
	if((retno = getsockname(sockfd, (struct sockaddr *)&sin, &addrlen)) == -1) {
		perror("getsockname");
		return retno;
	}

	uint16_t s_port;
	char s_ip4str[INET_ADDRSTRLEN];
	(void) get_sockinfo(&sin, &s_port, s_ip4str);

	printf("\n");
	
	while(1) {
		printf("The %s is up and running using UDP at IP %s on port %d\n", SERVERNAME, s_ip4str, s_port);

		/* EVENT Server expect to receive a string command */
		struct sockaddr_storage from_addr; // connector's address information
		socklen_t fromlen = sizeof(from_addr);

		int recv_bytes = 0; // num of bytes received
		int send_bytes = 0; // num of bytes sent

		uint32_t *fn;
		if((fn = recv_function(&recv_bytes, sockfd, &from_addr, &fromlen)) == NULL) {
			fprintf(stderr, "ERROR: recv_function!\n");
			return -1;
		}

		// since connection to aws is over UDP, data can come in any order
		// want to make sure receive reduce fn and reduce size first
		// so send status = 0 to sync with aws, ready to receive incoming data 

		int reduce_fn = fn[0]; // reduce function 
		int reduce_size = fn[1]; // buffer size for receiving data

		/* EVENT Server send ready status to start receiving data */
		if((send_bytes = send_status(sockfd, 0, &from_addr, fromlen)) == -1) {
			fprintf(stderr, "ERROR: send_status!\n");
			return -1;
		}

		uint32_t *data;
		if((data = recv_data(reduce_size, &recv_bytes, sockfd, &from_addr, &fromlen)) == NULL) {
			fprintf(stderr, "ERROR: recv_data!\n");
			return -1;
		}

		/* EVENT Server performs reduction function on received data */
		uint32_t *reduced;
		int reduced_len;
		if(reduce_fn != 4) {
			uint32_t temp = reduction_handler(reduce_fn, reduce_size, data, 0);
			reduced = &temp;
			reduced_len = 1;
		} else {
			if((reduced = reduction_sort_handler(reduce_size, data)) == NULL) {
				fprintf(stderr, "ERROR: reduction_sort_handler!\n");
				return -1;
			}
			reduced_len = reduce_size;
		}

		if(reduce_fn != 4) {
			printf("The %s has successfully finished the reduction [%s]: %d\n", SERVERNAME, list_reducefn[reduce_fn], *reduced);
		} else {
			printf("The %s has successfully finished the reduction [%s]: ... \n", SERVERNAME, list_reducefn[reduce_fn]);
			print_arr(reduced_len, reduced);
		}
		

		/* EVENT Server sends reduced value back */
		if((send_bytes = send_reduction(sockfd, reduced_len, reduced, &from_addr, fromlen)) == -1) {
			fprintf(stderr, "ERROR: send_reduction!\n");
			return -1;
		}
		printf("The %s has successfully finished sending the reduction value to AWS server.\n\n", SERVERNAME);
		

		if(reduce_fn == 4) {
			free(reduced);
		}
		free(fn);
		free(data);
	}

	close(sockfd);

	return 0;
}