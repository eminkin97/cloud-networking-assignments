#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <limits.h>


void listenForNeighbors();
void* announceToNeighbors(void* unusedParam);
void *sendlsa(void *unusedParam);


int globalMyID = 0;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[256];
//costs read from file
short int initial_costs[256];


//variables for link-state algorithm
short int vectors[256][256];		//undirected graph representing the routing system. value i,j is >= 0 if router i and j are neighbors
short int seqnums[256];			//sequence numbers of LSAs
short int shortestpathspredecessors[256];

//last time my distance vector was updated ... used for sending lsas
struct timeval mynodelastupdated;


struct pqnode {
	unsigned char id;
	struct pqnode *next;
};


int main(int argc, char** argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s mynodeid initialcostsfile logfile\n\n", argv[0]);
		exit(1);
	}
	
	
	//initialization: get this process's node ID, record what time it is, 
	//and set up our sockaddr_in's for sending to the other nodes.
	globalMyID = atoi(argv[1]);
	int i;
	for(i=0;i<256;i++)
	{
		gettimeofday(&globalLastHeartbeat[i], 0);
		
		char tempaddr[100];
		sprintf(tempaddr, "10.1.1.%d", i);
		memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
		globalNodeAddrs[i].sin_family = AF_INET;
		globalNodeAddrs[i].sin_port = htons(7777);
		inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
	}
	
	//initialize initial costs, distance, and vector arrays
	for (int i = 0; i < 256; i++) {
		initial_costs[i] = 1;
		seqnums[i] = -1;
		shortestpathspredecessors[i] = -1;
		for (int j = 0; j < 256; j++) {
			vectors[i][j] = -1;
		}
	}	

	//read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.
	//open and read costs file
	FILE *fp = fopen(argv[2], "r");
	if (fp == NULL) {
		perror("Opening Cost File");
		exit(1);
	}

	short int nodeid;
	short int costval;
	while (fscanf(fp, "%hd %hd\n", &nodeid, &costval) != EOF) {
		initial_costs[nodeid] = costval;
	}
	//close costs file
	fclose(fp);
	
	//create log file
	FILE *logfile = fopen(argv[3], "a");
	if (logfile == NULL) {
		perror("Opening Log File");
		exit(1);
	}
	fclose(logfile);
	
	//socket() and bind() our socket. We will do all sendto()ing and recvfrom()ing on this one.
	if((globalSocketUDP=socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	char myAddr[100];
	struct sockaddr_in bindAddr;
	sprintf(myAddr, "10.1.1.%d", globalMyID);	
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(7777);
	inet_pton(AF_INET, myAddr, &bindAddr.sin_addr);
	if(bind(globalSocketUDP, (struct sockaddr*)&bindAddr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind");
		close(globalSocketUDP);
		exit(1);
	}
	
	
	//start threads... feel free to add your own, and to remove the provided ones.
	pthread_t announcerThread;
	pthread_create(&announcerThread, 0, announceToNeighbors, (void*)0);
	
	pthread_t lsaThread;
	pthread_create(&lsaThread, 0, sendlsa, (void*)0);
	
	//good luck, have fun!
	listenForNeighbors(argv[3]);
	
	
	
}

void *sendlsa(void *unusedParam) {
	//create and send out LSA
	while(1) {
		struct timespec sleepFor;
		sleepFor.tv_sec = 1;  //1 second
		sleepFor.tv_nsec = 0; 
		
		nanosleep(&sleepFor, 0);
		
		//how long since node been updated
		struct timeval currentTime;
		gettimeofday(&currentTime, 0);
 
		long elapsed = (currentTime.tv_sec-mynodelastupdated.tv_sec)*1000000 + currentTime.tv_usec-mynodelastupdated.tv_usec;
				
		if (elapsed <= 3000000) {		//if time since last heartbeat is <= 3 seconds send lsa's every second
			
			
			unsigned char sendBuf[1000];
			memcpy(sendBuf, "info", 4);
					
			unsigned char myrouterid = (unsigned char) globalMyID;
			memcpy(sendBuf + 4, &myrouterid, 1);
				
			seqnums[globalMyID]++;
			memcpy(sendBuf + 5, &seqnums[globalMyID], 2);
				
			memcpy(sendBuf + 7, vectors[globalMyID], 512);
				
			// send LSA to all neighbors
			for(int i = 0; i < 256; i++)
				if(i != globalMyID)
					sendto(globalSocketUDP, sendBuf, 530, 0,
						  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
		}
	}
}


void monitorneighbors() {
	
	struct timeval currentTime;
	gettimeofday(&currentTime, 0);

	for (int i = 0; i < 256; i++) {
		if (vectors[globalMyID][i] >= 0) {
			// get elapsed time in microseconds 
			long elapsed = (currentTime.tv_sec-globalLastHeartbeat[i].tv_sec)*1000000 + currentTime.tv_usec-globalLastHeartbeat[i].tv_usec;
			
			if (elapsed >= 1000000) {		//if time since last heartbeat is >= 1 second
				// cannot reach neighbor set cost of link to -1
				vectors[globalMyID][i] = -1;
			}
		}
	}

}

//Run djikstras algorithm to calculate the shortest paths through the network
void calculateshortestpaths() {
	int distance[256];
	unsigned char finished[256];
	short int predecessor[256];

	for (int i = 0; i < 256; i++) {
		distance[i] = -1;
		finished[i] = 0;
		predecessor[i] = -1;
	}
	
	distance[globalMyID] = 0;
	
	// create priority queue to deal with tiebreaking
	struct pqnode *pqhead = (struct pqnode *)malloc(sizeof(struct pqnode));
	struct pqnode *pqtail = pqhead;
	pqhead -> id = globalMyID;
	pqhead -> next = NULL;
	
	while (pqhead != NULL) {
		// get node with minimum distance
		int min_distance = INT_MAX, min_distance_index = -1;
		struct pqnode *prev = NULL, *ptr = pqhead;
		while (ptr != NULL) {
			if (distance[ptr -> id] < min_distance) {
				min_distance = distance[ptr -> id];
				min_distance_index = ptr -> id;
			}
			ptr = ptr -> next;
		}
		
		// mark min distance node as finished
		finished[min_distance_index] = 1;
		
		//add neighbors of min distance node
		for (int j = 0; j < 256; j++) {
			if (!finished[j] && vectors[min_distance_index][j] >= 0) {
				if (distance[j] == -1 || distance[min_distance_index] + vectors[min_distance_index][j] < distance[j]) {
					if (distance[j] >= 0) {
						// node already in priority queue
						// need to move to end
						prev = NULL;
						ptr = pqhead;
						while (ptr != NULL) {
							if (ptr -> id == j) {
								if (prev == NULL) {
									pqhead = pqhead -> next;
								} else {
									prev -> next = ptr -> next;
								}
								ptr -> next = NULL;
								pqtail -> next = ptr;
								pqtail = ptr;
								break;
							}
							prev = ptr;
							ptr = ptr -> next;
						}
					} else {
						//add to end of priority queue
						struct pqnode *newnode = (struct pqnode *)malloc(sizeof(struct pqnode));
						newnode -> id = j;
						newnode -> next = NULL;
						pqtail -> next = newnode;
						pqtail = newnode;
					}		
			
					distance[j] = distance[min_distance_index] + vectors[min_distance_index][j];
					predecessor[j] = min_distance_index;
				}
			}
		}
		
		// remove min distance node
		prev = NULL;
		ptr = pqhead;
		while (ptr != NULL) {
			if (ptr -> id == min_distance_index) {
				if (prev == NULL) {
					pqhead = pqhead -> next;
				} else {
					prev -> next = ptr -> next;
				}
				free(ptr);
				break;
			}
			prev = ptr;
			ptr = ptr -> next;
		}
	}
	
	//save shortest paths
	memcpy(shortestpathspredecessors, predecessor, 512);
}




//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
void hackyBroadcast(const char* buf, int length)
{
	int i;
	for(i=0;i<256;i++)
		if(i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
			sendto(globalSocketUDP, buf, length, 0,
				  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void* announceToNeighbors(void* unusedParam)
{
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 300 * 1000 * 1000; //300 ms
	while(1)
	{
		hackyBroadcast("HEREIAM", 7);
		monitorneighbors();
		nanosleep(&sleepFor, 0);
	}
}

void listenForNeighbors(char *logfilename)
{
	char fromAddr[100];
	struct sockaddr_in theirAddr;
	socklen_t theirAddrLen;
	unsigned char recvBuf[1000];

	int bytesRecvd;
	while(1)
	{
		theirAddrLen = sizeof(theirAddr);
		if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000 , 0, 
					(struct sockaddr*)&theirAddr, &theirAddrLen)) == -1)
		{
			perror("connectivity listener: recvfrom failed");
			exit(1);
		}
		
		inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);
		
		short int heardFrom = -1;
		unsigned char graphupdated = 0;		//whether graph was updated or not
		if(strstr(fromAddr, "10.1.1."))
		{
			heardFrom = atoi(
					strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);
			
			//this node can consider heardFrom to be directly connected to it
			//initialize vector representing link if not initialized
			if (vectors[globalMyID][heardFrom] < 0) {
				vectors[globalMyID][heardFrom] = initial_costs[heardFrom];
				gettimeofday(&mynodelastupdated, 0);
				graphupdated = 1;
				
				if (vectors[globalMyID][heardFrom] > 1) {
					//send out costupdate message to neighbor
					unsigned char sendBuf[10];
					unsigned char myrouterid = (unsigned char) globalMyID;
					
					memcpy(sendBuf, "cupd", 4);
					memcpy(sendBuf + 4, &vectors[globalMyID][heardFrom], 2);
					
					sendto(globalSocketUDP, sendBuf, 8, 0, (struct sockaddr*)&globalNodeAddrs[heardFrom], sizeof(globalNodeAddrs[heardFrom]));
				}
			}
						
			//record that we heard from heardFrom just now.
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
		}
		
		//'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
		if(!strncmp(recvBuf, "cost", 4))
		{
			//TODO record the cost change (remember, the link might currently be down! in that case,
			//this is the new cost you should treat it as having once it comes back up.)
			// ...
		}
		//'cupd'<4 ASCII bytes> newCost to neighbor <2 byte signed>
		// determines link cost with your neighbor
		else if (!strncmp(recvBuf, "cupd", 4)) {
			short int newcost;
			memcpy(&newcost, recvBuf + 4, 2);
			
			vectors[globalMyID][heardFrom] = newcost;
			gettimeofday(&mynodelastupdated, 0);
			graphupdated = 1;
		}
		//'info'<4 ASCII bytes>, routerID<1 byte unsigned> seqNum<2 byte signed> vector of costs for this router <256 2 bytes signed>
		//info about path update from neighbor. Use info to update your own path
		else if(!strncmp(recvBuf, "info", 4))
		{
			// extract router id message is coming from
			unsigned char routerId;
			memcpy(&routerId, recvBuf+4, 1);
			
			//extract sequence number
			short int seqnum;
			memcpy(&seqnum, recvBuf+5, 2);
			
			//check if seqnum is greater than previously received seqnums
			if (seqnum > seqnums[routerId]) {
				seqnums[routerId] = seqnum;

				//extract vector for this router
				short int vector[256];
				memcpy(vector, recvBuf + 7, 512);
				
				//update global vectors row
				for (int i = 0; i < 256; i++) {
					if (vectors[routerId][i] != vector[i]) {
						graphupdated = 1;
					}
					vectors[routerId][i] = vector[i];
				}
				
				
				
				// send LSA to all neighbors except one it just came from
				for(int i = 0; i < 256; i++)
					if((i != routerId) && (i != globalMyID) && (i != heardFrom))
						sendto(globalSocketUDP, recvBuf, bytesRecvd, 0,
							  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
			}
			
		}
		
		
		if (graphupdated) {
			// run djikstra's algorithm to recalculate best paths
			calculateshortestpaths();
		}
		
		//Is it a packet from the manager? (see mp2 specification for more details)
		//send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		//forw format: 'forw'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		if(!strncmp(recvBuf, "send", 4) || (!strncmp(recvBuf, "forw", 4)))
		{
			//send the requested message to the requested destination node
			//output to log file
			char logline[200];
			// Open logfile for writing
			FILE *logfile = fopen(logfilename, "a");
			if (logfile == NULL) {
				perror("Opening Log File");
				exit(1);
			}
			
			short int destID;
			memcpy(&destID, recvBuf + 4, 2);
			destID = ntohs(destID);
			
			recvBuf[bytesRecvd] = '\0';
			char message[100];
			memcpy(message, recvBuf + 6, 100);
			
			if (destID == globalMyID) {
				//log message to log file
				sprintf(logline, "receive packet message %s\n", message);
				fwrite(logline, 1, strlen(logline), logfile);
				fclose(logfile);
				continue;
			}
			
			// determine next hop
			short int nexthop;
			if (shortestpathspredecessors[destID] == -1) {
				//destination unreachable drop packet
				sprintf(logline, "unreachable dest %hd\n", destID);
				fwrite(logline, 1, strlen(logline), logfile);
				fclose(logfile);
				continue;

			} else {
				short int pred = shortestpathspredecessors[destID], current = destID;
				while (pred != globalMyID) {
					current = pred;
					pred = shortestpathspredecessors[current];
				}
				nexthop = current;
			}
			
			// send packet to nexthop
			//log in log file
			if(!strncmp(recvBuf, "send", 4)) {
				memcpy(recvBuf, "forw", 4);
				sendto(globalSocketUDP, recvBuf, bytesRecvd, 0, (struct sockaddr*)&globalNodeAddrs[nexthop], sizeof(globalNodeAddrs[nexthop]));
				sprintf(logline, "sending packet dest %hd nexthop %hd message %s\n", destID, nexthop, message);
			} else {
				sendto(globalSocketUDP, recvBuf, bytesRecvd, 0, (struct sockaddr*)&globalNodeAddrs[nexthop], sizeof(globalNodeAddrs[nexthop]));
				sprintf(logline, "forward packet dest %hd nexthop %hd message %s\n", destID, nexthop, message);
			}
			fwrite(logline, 1, strlen(logline), logfile);

			//close logfile
			fclose(logfile);

		}
		
	}
	//(should never reach here)
	close(globalSocketUDP);
}











