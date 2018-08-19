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

#include "helper.h"
#include "reduction.h"

#define SERVERPORT_D_TCP "25843" // aws <-> client
#define MAXINPUTLEN 1000 // max num of csv input file


int get_reducefn(char *input) {
    int i = -1;
    for(i = 0; i < num_reducefn; i++) {
        if(strcmp(list_reducefn[i], input) == 0) {
            return i;
        }
    }

    if(i == -1) {
        fprintf(stderr, "Invalid Argument!\n");
    }
    return i;
}

uint32_t * rand_generator(int size, int max_num) {
    int max = max_num; // rand num from 0 to max - 1;

    uint32_t *arr = malloc(size * sizeof(uint32_t));
    if(arr == NULL) {
    	fprintf(stderr, "malloc failed!\n");
    	return NULL;
    }
    
    int i;
    srand((unsigned) time(NULL));
    
    for(i = 0; i < size; i++) {
        arr[i] = (uint32_t) (rand() % max);
    }
    return arr;
}

int connect_aws(char *ipaddr, char *port) {
	// Create TCP stream socket to connect to AWS (TCP)
    int sockfd, retno;
    struct addrinfo *servinfo;
    if((retno = set_addrinfo(ipaddr, port, SOCK_STREAM, &servinfo)) == -1) {
    	return -1;
    }

    if((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1) {
    	perror("client: socket");
    	freeaddrinfo(servinfo);
    	return -1;
    }

	// Connecting (TCP) to AWS server#d to establish connection
    if(connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
        perror("connect");
        freeaddrinfo(servinfo);
        close(sockfd);
        return -1;
    }

    freeaddrinfo(servinfo);
    return sockfd;
}

int send_job(int sockfd, int reduce_fn, int reduce_size, uint32_t *data) {
	int send_bytes;

	// Send reduce function and reduce size to AWS
    uint32_t sbuf[2] = {reduce_fn, reduce_size};
    if((send_bytes = send_handler(2, sbuf, sockfd, NULL, 0, 1)) == -1) {
    	printf("ERROR: send_handler!\n");
    	return -1;
    }

    // Send data to AWS
    if((send_bytes = send_handler(reduce_size, data, sockfd, NULL, 0, 1)) == -1) {
    	printf("ERROR: send_handler!\n");
    	return -1;
    }

    int num_vals = send_bytes / sizeof(uint32_t);

    return num_vals;
}

uint32_t *recv_result(int sockfd, int final_len) {
	uint32_t *final;
	int recv_bytes = 0;
	
	if((final = recv_handler(final_len, &recv_bytes, sockfd, NULL, NULL, 1)) == NULL) {
		fprintf(stderr, "ERROR: recv_handler!\n");
	}
	
	return final;
}

int main(int argc, char *argv[]) {
	// Check for reduction type input argument
    if(argc < 2) {
        printf("ERROR: missing <function input>");
        exit(1);
    }


    uint32_t *data;
    int reduce_fn = (int) get_reducefn(argv[1]);
    int reduce_size;

    if(argc == 2) {
        // Read in input data (nums.csv)
        int input_size = 0;
        uint32_t *input_arr = malloc(MAXINPUTLEN * sizeof(int));
        char input_buf[MAXINPUTLEN];

        FILE *fstream = fopen("nums.csv","r");
        if(fstream == NULL) { 
            printf("Unable to find file!\n");
        }
        while(fgets(input_buf, sizeof(input_buf), fstream)) {
            input_arr[input_size] = (uint32_t) atoi(input_buf); // char to int
            input_size++;
        }
        data = input_arr;
        reduce_size = input_size;

        printf("read %d\n", input_size);

        if(input_size == 0) {
            fprintf(stderr, "ERROR: read 0 numbers from file!\n");
            return -1;
        }
    } else if(argc == 3) {
        reduce_size = atoi(argv[2]);
        if((data = rand_generator(reduce_size, 10000)) == NULL) {
            return -1;
        }
    } 

    /* EVENT Client connects to AWS via TCP */
    int sockfd;
    if((sockfd = connect_aws("127.0.0.1", SERVERPORT_D_TCP)) == -1) {
    	printf("ERROR: connect_aws!\n");
    	return -1;
    }
    printf("The client is up and running.\n");

    int num_vals;
    if((num_vals = send_job(sockfd, reduce_fn, reduce_size, data)) == -1) {
    	printf("ERROR: send_job!\n");
    	return -1;
    }
    printf("The client has sent the reducetion type [%s] to AWS.\n", argv[1]);
    printf("The client has sent %d numbers to AWS.\n", num_vals);

    uint32_t *final = NULL;
    int final_len = 1;
	if(reduce_fn == 4) {
		final_len = reduce_size;	
	}

	if((final = recv_result(sockfd, final_len)) == NULL) {
		fprintf(stderr, "ERROR: recv_result!\n");
		return -1;
	}

	if(reduce_fn != 4) {
    	printf("The client has received reduction [%s]: %d\n", argv[1], *final);
    } else {
    	printf("The client has successfully finished the reduction [%s]: ... \n", argv[1]);
    	print_arr(final_len, final);
    }

    free(data);
    free(final);
    close(sockfd);

	return 0;
}