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


void listenForNeighbors();
void* announceToNeighbors(void* unusedParam);


int globalMyID = 0;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[256];
//costs read from file
int initial_costs[256];


//variables for path-vector algorithm
int vectors[256][256];		//row i, column j represents distance router i has to router j
short int paths[256][256];		

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
		for (int j = 0; j < 256; j++) {
			vectors[i][j] = -1;
			paths[i][j] = -1
		}
	}	

	//read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.
	//open and read costs file
	FILE *fp = fopen(argv[2], "r");
	if (fp == NULL) {
		perror("Opening Cost File");
		exit(1);
	}

	int nodeid;
	int costval;
	while (fscanf(fp, "%d %d\n", &nodeid, &costval) != EOF) {
		initial_costs[nodeid] = costval;
	}
	//close costs file
	fclose(fp);
	
	
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
	
	
	
	
	//good luck, have fun!
	listenForNeighbors();
	
	
	
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
		nanosleep(&sleepFor, 0);
	}
}

void listenForNeighbors()
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
		if(strstr(fromAddr, "10.1.1."))
		{
			heardFrom = atoi(
					strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);
			
			//this node can consider heardFrom to be directly connected to it
			if (vectors[globalMyID][heardFrom] < 0) {
				vectors[globalMyID][heardFrom] = initial_costs[heardFrom];
				paths[globalMyID][heardFrom] = heardFrom;
			}
			
			// TODO: send path update packet if anything was changed i.e. hackyBroadcast()
			
			//record that we heard from heardFrom just now.
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);
		}
		
		//Is it a packet from the manager? (see mp2 specification for more details)
		//send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		if(!strncmp(recvBuf, "send", 4))
		{
			//TODO send the requested message to the requested destination node
			// ...
		}
		//'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
		else if(!strncmp(recvBuf, "cost", 4))
		{
			//TODO record the cost change (remember, the link might currently be down! in that case,
			//this is the new cost you should treat it as having once it comes back up.)
			// ...
		}
		//'info'<4 ASCII bytes>, destID<1 byte unsigned> newCost<4 byte signed> pathsize<1 byte unsigned> path<up to 256 bytes unsigned>
		//info about path update from neighbor. Use info to update your own path
		else if(!strncmp(recvBuf, "info", 4))
		{
			// extract destination id
			unsigned char destId;
			memcpy(&destId, recvBuf+4, 1);
			
			//extract new cost
			int newcost;
			memcpy(&newcost, recvBuf+5, 4);
			
			//extract path size
			unsigned char pathsize;
			memcpy(&pathsize, recvBuf+9, 1);
			
			//path does not exist from <heardFrom> router to <destID>
			if (pathsize == 0) {
				vectors[heardFrom][destId] = -1;
				continue;
			}
			
			//extract path
			unsigned char *path;
			memcpy(path, recvBuf+10, pathsize);
			
			//check that path has no loops
			int loopfound = 0;
			unsigned char distinct[256];
			memset(distinct, 0, 256);
			distinct[globalMyID] = 1;
			for (int i = 0; i < pathsize; i++) {
				distinct[*(path + i)]++;
				if (distinct[*(path + i)] > 1) {
					// there is a loop, no path from here
					vectors[heardFrom][destId] = -1;
					loopfound = 1;
					break;
				}
			}
			if (loopfound) {
				continue;
			}
			
			//TODO: update path in path matrix
			
			
			//update vector: router <heardFrom> has cost <newcost> to reach destination <destId>
			vectors[heardFrom][destId] = newcost;
				
			// TODO: run the algorithm
			// check if this route to <destid> is better than current route
			// vectors[globalMyID][destId]
				
			
		}
		
		//TODO now check for the various types of packets you use in your own protocol
		//else if(!strncmp(recvBuf, "your other message types", ))
		// ... 
	
		
	}
	//(should never reach here)
	close(globalSocketUDP);
}











