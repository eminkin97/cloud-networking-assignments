/*
** client.c -- a stream socket client demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#define MAXDATASIZE 1024 // max number of bytes we can get at once 

struct address {
	char *hostname;	// web address without header or port
	char *port;
	char *pathtofile;
};

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

//This function writes parameter message to an output file
void write_output(char *message) {
	FILE *f = fopen("output", "w");
	if (f == NULL) {
		perror("ERROR opening file to write");
		exit(1);
	}

	fprintf(f, "%s", message);
	fclose(f);
}

void write_output_in_bytes(char *bytes, int size) {
	FILE *f = fopen("output", "w");
	if (f == NULL) {
		perror("ERROR opening file to write");
		exit(1);
	}

	fwrite(bytes, 1, size, f);
	fclose(f);

}

struct address* validateaddress(int argc, char *argv[]) {
	// validate that cmd line inputted parameter is in valid format
	if (argc != 2) {
	    write_output("ERROR - usage: client hostname\n");
	    exit(1);
	}

	// check if http header is there
	if (strncmp(argv[1], "http://", 7) != 0) {
	    write_output("INVALIDPROTOCOL\n");
	    exit(1);
	}

	// extract hostname, port number, and path to file
	struct address *addrobj = (struct address *)malloc(sizeof(struct address));
	char *webaddress = (char *) malloc(strlen(argv[1]));
	strcpy(webaddress, argv[1] + 7);

	// determine if port number included in webaddress
	char *hostname = strtok(webaddress, ":");
	char *port = strtok(NULL, "/");
	if (port == NULL) {
	    // no port specified in address
	    addrobj -> hostname = strtok(webaddress, "/");
	    addrobj -> port = "http";
	    addrobj -> pathtofile = strtok(NULL, " ");
	} else {
	    // port specified in address
	    addrobj -> hostname = hostname;
	    addrobj -> port = port;
	    addrobj -> pathtofile = strtok(NULL, " ");	//rest of web address is the path to file
	}

	return addrobj;
}

// function handles http response
// if response code 200, it writes message response from http to output file
// if response code 404, it writes FILENOTFOUND to output file
void handleresponse(char *buf, int byteswritten) {
	// get first line of http response which contains code
	char responsecode[16];
	memcpy(responsecode, buf, 15);
	responsecode[15] = '\0';

	if (strstr(responsecode, "404")) {
		// file not found, write FILENOTFOUND
		write_output("FILENOTFOUND\n");
	} else if (strstr(responsecode, "200")) {
		// success, write message to output file
		// extract entire header
		char *ptr = buf;
		int i = 0;
		while (i < byteswritten) {
			if (memcmp(ptr, "\r\n\r\n", 4) == 0) {
				break;	
			}
			ptr += 1;
			i += 1;
		}

		char *header = (char *)malloc(i+10);
		memcpy(header, buf, i);

		// extract content length from header
		long contentlength;
		sscanf(strstr(header, "Content-Length"), "Content-Length: %ld", &contentlength);
		char *message = ptr + 4;

		//write message to output file
		write_output_in_bytes(message, contentlength);
	}

}


int main(int argc, char *argv[])
{
	int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;

	// validate webaddress and form http get request
	struct address *addrobj = validateaddress(argc, argv);
	char *request = (char *) malloc(strlen(addrobj -> pathtofile) + strlen(addrobj -> hostname) + 1000);
	snprintf(request, strlen(addrobj -> pathtofile) + strlen(addrobj -> hostname) + 1000, 
	"GET /%s HTTP/1.0\r\n"
	"Host: %s\r\n\r\n", addrobj -> pathtofile, addrobj -> hostname);


	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	// get info about the server we are connecting to
	if ((rv = getaddrinfo(addrobj -> hostname, addrobj -> port, &hints, &servinfo)) != 0) {
		write_output("NOCONNECTION\n");
		exit(1);
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			continue;
		}

		break;
	}

	if (p == NULL) {
		write_output("NOCONNECTION\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	// send get request to server
	if (send(sockfd, request, strlen(request)+1, 0) == -1) {
	    write_output("ERROR on send\n");
	    exit(1);
	}

	// free allocated data for request parameter
	free(request);
	free(addrobj -> hostname);
	free(addrobj);

	char *response_msg = (char *) malloc(MAXDATASIZE);
	int bytes_allocated = MAXDATASIZE;
	int bytes_received = 0;
	while ((numbytes = recv(sockfd, buf, MAXDATASIZE, 0)) > 0) {
		bytes_received += numbytes;
		if (bytes_allocated < bytes_received) {
			response_msg = (char *) realloc(response_msg, bytes_allocated + MAXDATASIZE);
			bytes_allocated += MAXDATASIZE;
		}
		memcpy(response_msg + (bytes_received - numbytes), buf, numbytes);
		memset(buf, 0, MAXDATASIZE);
	}


	close(sockfd);

	// process response
	handleresponse(response_msg, bytes_received);

	free(response_msg);

	return 0;
}

