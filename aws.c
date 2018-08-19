/*

Reference Material 
- Beej's Guide to Network Programming

********************************************************************************
Description:
	- This program will perform a reduction by evenly distributing an array of 
	numbers to three back-end servers.

	- Front-end will handle the following:
		1. Receive an user-input in the form of (function size). 
		2. User-input is parsed and sent to the back-end servers for setup.
		3. Receive reduction value from back-end and perform last reduction.
		4. Send final reduction to client.

		"Function" is the type of reduction you want to perform.
		"Size" is the total amount of numbers the reduction is performed upon.

	- Back-end will handle the following:
		1. Receive reduction type and size of reduction to peform.
		2. Create a buffer with appropriate size noted in (1)
		3. Perform reduction and send reduced value to front-end.
********************************************************************************


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
#include <sys/time.h> 
#include <signal.h>
#include <time.h>

#include "reduction.h"
#include "helper.h"

#define MYIPADDR "127.0.0.1"

#define SERVERIP_A "127.0.0.1"
#define SERVERIP_B "127.0.0.1"
#define SERVERIP_C "127.0.0.1"

#define SERVERPORT_A "21843" // backend servers
#define SERVERPORT_B "22843"
#define SERVERPORT_C "23843"

#define SERVERPORT_D_UDP "24843" // aws <-> backend
#define SERVERPORT_D_TCP "25843" // aws <-> client
#define SERVERNAME "AWS"

#define NUMSERVERS 3
#define MAXSENDLEN 65000 // bytes
#define MAXINPUTLEN 100 // bytes

char *backend_name[3] = {"A", "B", "C"};

/*
	Split the src array evenly and copy into dest and return int array of their len
	Param dest stores uint32_t pointers to evenly spliited arrays
	POST: free each array that is pointed in dest call free_split_array()
	POST: free the return val of split_array()
*/
int *split_array(int split, int size, uint32_t *src, uint32_t **dest) {
    int len = size / split;
    int rem = size % split;
    
    int i;
    for(i = 0; i < split; i++) {
        if(i == 0) {
            (dest)[i] = malloc((len + rem) * sizeof(uint32_t));
            memcpy((dest)[i], src, (len + rem) * sizeof(uint32_t));
        } else {
            (dest)[i] = malloc(len * sizeof(uint32_t));
            memcpy((dest)[i], src + (i*len + rem), len * sizeof(uint32_t));
        }
    }

    int *ret = malloc(split * sizeof(int));
    if(!ret) {
        return NULL;
    } else {
        for(i = 0; i < split; i++) {
            if(i == 0) {
                ret[i] = len + rem;
            } else {
                ret[i] = len;
            }
        }
        return ret;
    }
}

/* 
	Used to free each array pointed by uint32_t* in dest from split_array()
*/
void free_split_array(int split, uint32_t **arr) {
    int i;
    for(i = 0; i < split; i++) {
        free((arr)[i]);
    }
}

/*
	pos is loc in dest arry to start merging of values of src with size of len
    pos is updated to the size of dest
 */
int merge_array(int pos, uint32_t *src, uint32_t *dest, int len) {
    memcpy(dest + (uint32_t) pos, src, len * sizeof(uint32_t));
    return pos + len;
}

void get_split_size(int size, int *min, int *max, int split) {
    int val = size / split;
    int rem = size % split;
        
    *min = val;
    *max = val + rem;
}

int send_function(int sockfd, int *pair, struct addrinfo **serv[], int num_servers) {
	int total_bytes_sent = 0;
	// if input doesnt split evenly, first backend server recv remainder (high)
	// if even split then low == high
	int low, high;
	get_split_size(pair[1], &low, &high, num_servers);
	// printf("DEBUG: get_split_size -> min: %d, max: %d\n", low, high);
	
	/* EVENT AWS sends reduce function ID and buffer size to backend servers */
	// printf("STATUS: sending reduction func and size to back-end servers.\n");
	int i;
	for(i = 0; i < num_servers; i++) {
		/* Initialize the buf to send */
		uint32_t sbuf[2] = {pair[0], high};
		if((low != high) && (i != 0)) {
			sbuf[1] = low;
		}

    	int sent; //num of bytes sent
    	if((sent = send_handler(2, sbuf, sockfd, (*serv[i])->ai_addr, (*serv[i])->ai_addrlen, 2)) == -1) {
    		fprintf(stderr, "ERROR: send_handler failed!\n");
    		return -1;
    	}
    	total_bytes_sent += sent;
	}
	return total_bytes_sent;
}

/*
	Return 0 if receive OK status from backend severs after sending reduction function type and size
	else -1 to prevent sending more data to backend servers. 
	PRE: single status code value from each backend servers.
*/
int recv_status(int sockfd, int num_servers) {
	struct sockaddr_storage from_addr; // connector's address information
	socklen_t fromlen = sizeof(from_addr);

	/* EVENT AWS receive status fronm servers */
	// fprintf(stdout, "STATUS: recv READY status from back-end servers.\n");
	int i;
	for(i = 0; i < num_servers; i++) {
		uint32_t *rbuf;
		int recv_bytes;
		if((rbuf = recv_handler(1, &recv_bytes, sockfd, (struct sockaddr *) &from_addr, &fromlen, 2)) == NULL) {
			fprintf(stderr, "ERROR: recv_handler failed!\n");
			return -1;
		}

		int num_vals = recv_bytes / sizeof(uint32_t);
		int status = rbuf[num_vals - 1];

		/*
		uint16_t r_port;
		char r_ip4str[INET_ADDRSTRLEN];
		(void) get_sockinfo((struct sockaddr_in *) &from_addr, &r_port, r_ip4str);

		fprintf(stdout, "\tinput received: %d (%d bytes) from %s on %d\n", 
			status, recv_bytes, r_ip4str, r_port);
		*/

		free(rbuf);

		if(status != 0) {
			return -1;
		}
	}
	return 0;
}

/*
	Return total num of bytes sent to all servers else -1 on error
	PRE: size = total input size of input array src
*/
int send_data(int size, uint32_t *data, int num_servers, int sockfd, struct addrinfo **serv[]) {
	int total_bytes_sent = 0;
	/* Split input data evenly according to num_servers */
	uint32_t *sbuf[num_servers];
	int *slen = split_array(num_servers, size, data, sbuf); // remember to free sbuf[i] and slen!!

	// printf("STATUS: send reduction data array to back-end servers.\n");
	int i;
	for(i = 0; i < num_servers; i++) {
		int sent; //num of bytes sent
    	if((sent = send_handler(slen[i], sbuf[i], sockfd, (*serv[i])->ai_addr, (*serv[i])->ai_addrlen, 2)) == -1) {
    		fprintf(stderr, "ERROR: send_handler failed!\n");
    		return -1;
    	}
    	printf("The %s has sent %d numbers to Backend-Sever %s.\n", SERVERNAME, slen[i], backend_name[i]);

    	/*
    	uint16_t port;
		char ip4str[INET_ADDRSTRLEN];
		(void) get_sockinfo((struct sockaddr_in *) (*serv[i])->ai_addr, &port, ip4str);

		printf("\tsending %d numbers (%lu bytes) to %s on %d\n", 
    		slen[i], slen[i] * sizeof(uint32_t), ip4str, port);

		ntohl_arr(slen[i], sbuf[i]);
		print_arr(slen[i], sbuf[i]);
		*/
		total_bytes_sent += sent;
	}

	free(slen);
	free_split_array(num_servers, sbuf);
	return total_bytes_sent;
}

uint32_t *recv_reduction(int sockfd, int reduce_fn, int input_size, int max_reduce_size, int num_servers, int *reduced_len) {
	struct sockaddr_storage from_addr; // connector's address information
	socklen_t fromlen = sizeof(from_addr);

	int curr_len = 0; // len of reduced array
	uint32_t *reduced; // arr of received values from backend servers
	if(reduce_fn != 4) {
		reduced = malloc(num_servers * sizeof(uint32_t));
	} else {
		reduced = malloc(input_size * sizeof(uint32_t));
	}
	if(reduced == NULL) {
		fprintf(stderr, "ERROR: malloc failed at recv_reduction!\n");
		return NULL;
	}

	// printf("STATUS: waiting to recv REDUCTION from servers ...\n");
	int i;
	for(i = 0; i < num_servers; i++) {
		int recv_bytes; // num bytes received
		uint32_t *rbuf;
		if((rbuf = recv_handler(max_reduce_size, &recv_bytes, sockfd, (struct sockaddr *) &from_addr, &fromlen, 2)) == NULL) {
			fprintf(stderr, "ERROR: recv_handler failed!\n");
			return NULL;
		}

		int rlen = recv_bytes / sizeof(uint32_t);
		curr_len = merge_array(curr_len, rbuf, reduced, rlen);

		// Obtain port number of UDP datagram socket using getsockname
        struct sockaddr_in udp_sin;
        socklen_t udp_sin_len = sizeof(udp_sin);
        if (getsockname(sockfd, (struct sockaddr *)&udp_sin, &udp_sin_len) == -1) {
            perror("getsockname");
            return NULL;
        }
        short udp_port = ntohs(udp_sin.sin_port); // aws port
        
        uint16_t r_port;
		char r_ip4str[INET_ADDRSTRLEN];
		(void) get_sockinfo((struct sockaddr_in *) &from_addr, &r_port, r_ip4str);
		// printf("\tback-end server is from %s on %d\n", r_ip4str, r_port);

		char *backend = "";
        if(atoi(SERVERPORT_A) == r_port) {
        	backend = backend_name[0];
        } else if(atoi(SERVERPORT_B) == r_port) {
        	backend = backend_name[1];
        } else if(atoi(SERVERPORT_C) == r_port) {
        	backend = backend_name[2];
        } else {
        	// error
        }

		if(reduce_fn != 4) {
			printf("The %s received reduction result of [%s] from Backend-Sever %s using UDP over port %d and it is %d.\n", 
				SERVERNAME, list_reducefn[reduce_fn], backend, udp_port, *rbuf);
		} else {
			printf("The %s received reduction result of [%s] from Backend-Sever %s using UDP over port %d and it is ... \n", 
			SERVERNAME, list_reducefn[reduce_fn], backend, udp_port);
			print_arr(rlen, rbuf);
		}
		
		free(rbuf);
	}

	*reduced_len = curr_len;
	return reduced;
}

uint32_t *recv_job(int sockfd, int *reduce_fn, int *reduce_len, int *client) {
	int client_sockfd, recv_bytes;

	// Listening ... to accept TCP connection from Client
    if(listen(sockfd, 10) == -1) {
    	perror("listen");
    	return NULL;
    }

    // Waiting to accept TCP connection from Client
    struct sockaddr_storage from_addr;
    socklen_t fromlen = sizeof(from_addr);
    if((client_sockfd = accept(sockfd, (struct sockaddr *)&from_addr, &fromlen)) == -1) {
    	perror("accept");
    	return NULL;
    }
    *client = client_sockfd; // return client information

    /* 
    uint16_t r_port;
	char r_ip4str[INET_ADDRSTRLEN];
	(void) get_sockinfo((struct sockaddr_in *) &from_addr, &r_port, r_ip4str);
	printf("STATUS: received reduce job from client\n");
	printf("\tclient is from %s on %d\n", r_ip4str, r_port);
	printf("\treduce job = %d of len = %d\n", *reduce_fn, *reduce_len);
	*/

	uint32_t *input;
    if((input = recv_handler(2, &recv_bytes, client_sockfd, NULL, 0, 1)) == NULL) {
    	fprintf(stderr, "ERROR: recv_handler!\n");
    	return NULL;
    }

    *reduce_fn = (int) input[0];
    *reduce_len = (int) input[1];

    uint32_t *data;
    if((data = recv_handler(input[1], &recv_bytes, client_sockfd, NULL, 0, 1)) == NULL) {
    	fprintf(stderr, "ERROR: recv_handler!\n");
    	return NULL;
    }

    int num_vals = recv_bytes / sizeof(uint32_t);
    // print_arr(num_vals, data);

    if(num_vals != input[1]) {
    	fprintf(stderr, "ERROR: did not receive all data!\n");
    	return NULL;
    }

    // Obtain port number of TCP datagram socket using getsockname
    struct sockaddr_in tcp_sin;
    socklen_t tcp_sin_len = sizeof(tcp_sin);
    if (getsockname(sockfd, (struct sockaddr *)&tcp_sin, &tcp_sin_len) == -1) {
        perror("getsockname");
        return NULL;
    }
    short tcp_port = ntohs(tcp_sin.sin_port); // aws tcp port

    printf("The %s has received %d numbers from the client using TCP ovrt port %d.\n",
    	SERVERNAME, num_vals, tcp_port);


    free(input);
    
    return data; // remember to free(data) !!!
}

int send_final(int sockfd, int final_len, uint32_t *final) {
	int send_bytes;
	if((send_bytes = send_handler(final_len, final, sockfd, NULL, 0, 1)) == -1) {
    	fprintf(stderr, "ERROR: send_handler!\n");
    	return -1;
    }

    printf("The %s has successfully finished sending the reduction value to client.\n", SERVERNAME);
    return 0;
}



int main(int argc, char *argv[]) {
	struct addrinfo *servinfo_a, *servinfo_b, *servinfo_c; // info about backend servers
	int retno; // return error number


	struct addrinfo *servinfo_d_udp, *servinfo_d_tcp; // info about AWS
	int sockfd_udp, sockfd_tcp; // udp -> backend and tcp -> client

	struct addrinfo **servinfo[3] = {&servinfo_a, &servinfo_b, &servinfo_c};
	char *servport[3] = {SERVERPORT_A, SERVERPORT_B, SERVERPORT_C};
	char *servaddr[3] = {SERVERIP_A, SERVERIP_B, SERVERIP_C};

	if((retno = set_servinfo(servport, servaddr, servinfo)) != 0) {
		return retno;
	}

	if((retno = set_addrinfo(MYIPADDR, SERVERPORT_D_UDP, SOCK_DGRAM, &servinfo_d_udp)) != 0) {
		return retno;
	}

	if((retno = set_addrinfo(MYIPADDR, SERVERPORT_D_TCP, SOCK_STREAM, &servinfo_d_tcp)) != 0) {
		return retno;
	}

	if((retno = set_socketfd(&sockfd_udp, servinfo_d_udp)) != 0) {
		// servinfo_d_udp is freed in set_sockfd()
		return retno;
	}

	if((retno = set_socketfd(&sockfd_tcp, servinfo_d_tcp)) != 0) {
		// servinfo_d_tcp is freed in set_sockfd()
		return retno;
	}

	printf("\n");

    while(1) {
    	printf("The %s is up and running.\n", SERVERNAME);

    	/* EVENT AWS receive job from client */
    	int client_sockfd; // client socket used for sending back reduction
    	int reduce_fn, data_size;
    	uint32_t *data; // remember to free!!!
    	if((data = recv_job(sockfd_tcp, &reduce_fn, &data_size, &client_sockfd)) == NULL) {
    		printf("ERROR: recv_job!\n");
    		return -1;
    	}
        
        int min_reduce_size, max_reduce_size;
    	get_split_size(data_size, &min_reduce_size, &max_reduce_size, NUMSERVERS);

        /* start reduction process */
        struct timeval t1, t2;
        double elapsedTime = 0;
        gettimeofday(&t1, NULL); // start timer


        int send_bytes = 0; // num of bytes sent

        /* EVENT AWS send reduce function and reduce size to backend servers */
        int parsed[2] = {reduce_fn, data_size};
        if((send_bytes = send_function(sockfd_udp, parsed, servinfo, NUMSERVERS)) == -1) {
        	printf("ERROR: send_function!\n");
        	return -1;
        }

        /* EVENT AWS receive status from backend servers = ready to send data */
        int ready;
        if((ready = recv_status(sockfd_udp, NUMSERVERS)) == -1) {
        	printf("ERROR: recv_status!\n");
        	return -1;
        }

        /* EVENT AWS send data to backend servers */
        if((send_bytes = send_data(data_size, data, NUMSERVERS, sockfd_udp, servinfo)) == -1) {
        	printf("ERROR: send_data!\n");
        	return -1;
        }

        /* EVENT AWS receives reduced data from backend servers */
        int reduced_len;
        uint32_t *reduced;
        if((reduced = recv_reduction(sockfd_udp, reduce_fn, data_size, max_reduce_size, NUMSERVERS, &reduced_len)) == NULL) {
        	printf("ERROR: recv_reduction!\n");
        	return -1;
        }

        /* EVENT AWS performs final reduction on reduced data */
    	uint32_t *final;
    	int final_len;
    	if(reduce_fn != 4) {
    		uint32_t temp = reduction_handler(reduce_fn, reduced_len, reduced, 1);
    		final = &temp;
    		final_len = 1;
    	} else {
    		if((final = reduction_sort_handler(reduced_len, reduced)) == NULL) {
    			fprintf(stderr, "ERROR: reduction_sort_handler!\n");
    			return -1;
    		}
    		final_len = reduced_len;
    	}

        if(reduce_fn != 4) {
        	printf("The %s has successfully finished the reduction [%s]: %d.\n", SERVERNAME, list_reducefn[reduce_fn], *final);
        } else {
        	printf("The %s has successfully finished the reduction [%s]: ... \n", SERVERNAME, list_reducefn[reduce_fn]);
        	print_arr(final_len, final);
        }

        gettimeofday(&t2, NULL); // end timer
        long delta = (t2.tv_sec*1e6 + t2.tv_usec) - (t1.tv_sec*1e6 + t1.tv_usec);
        elapsedTime += delta / 1000.0;   // us to ms

        /* EVENT AWS sends final reduction back to client via TCP */
        if((retno = send_final(client_sockfd, final_len, final)) == -1) {
        	fprintf(stderr, "ERROR: send_final!\n");
        	return -1;
        }

        printf("\nSTATUS: elapsedTime (w/ reduction): %f ms\n\n", elapsedTime);


        // Clean Up Here 
        close(client_sockfd);

    	if(reduce_fn == 4) {
    		free(final);
    	}

        free(data);
    	free(reduced);
    }

	freeaddrinfo(*servinfo[0]);
	freeaddrinfo(*servinfo[1]);
	freeaddrinfo(*servinfo[2]);
	close(sockfd_udp);
	close(sockfd_tcp);

	return 0;
}
