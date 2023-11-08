#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <stddef.h>
#include <queue>
#include <string>
#include <iostream>
#include <fstream>
//queue type is defined in std namespace
using namespace std;


//constants would not change after initialisation
#define randomUB 50
#define K 1
#define NODES 5
#define PORT_NUM 5
#define CYCLE 1
#define TERMINATE -1
#define THRESHOLD 1
#define NDIMS 2
#define ADJ 4
#define OFFSET 20
#define REQTAG 100
#define RESTAG 90
#define ALETAG 80
#define NEATAG 70
#define TEMTAG 60
#define TEMPTTAG 50
#define SLATAG 40
#define TOKENTAG 1000
#define PARENT -25



//need to update on function prototype

/*
cd 'FIT3143/A3'
mpic++ -lpthread a3.cpp -o a3.o -lm
mpirun -oversubscribe -np 7 a3.o
*/

typedef struct {
	int id; 
	int parent;
	int ports[K]; 
	// true -> isUsing critical section
	bool isUsing;
	// true -> already ask for token
	bool asked;
	// next node to receive token
	int holder;
	// tokens queue
	pthread_t threads[K+1]; 
	queue <int> req_q; //queue to store the request
	int* shaMemo;

} node_t;

// structure for port data
typedef struct {
	int id; 
	//pointer to struct node_t
	node_t* node; // Change to pointer to node_t
	int available; //(0 or 1)
} port_t;


typedef struct {
	node_t* node; // Change to pointer to node_t
	int term; //term to terminate 
	MPI_Comm world;
	MPI_Comm comm2D;
} nodefunc_t;

//Gloable variables 
// int waitPeriod = 2;
// structure for node data

//Function Prototype
//At top level
//remember to update the signature when there is change in arguments
int master_io(MPI_Comm world_comm, MPI_Comm comm);
int slave_io(MPI_Comm world_comm, MPI_Comm comm);
void *node_func(void *args);
void *port_func(void *arg);

pthread_mutex_t g_Mutex = PTHREAD_MUTEX_INITIALIZER;
int g_nslaves = 0;


int main(int argc, char **argv)
{   
    int size, rank, provided;
    MPI_Comm new_comm;

    // initialize MPI with thread support
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	if (provided != MPI_THREAD_MULTIPLE) {
 		fprintf(stderr, "This MPI implementation does not support MPI_THREAD_MULTIPLE.\n");
 		MPI_Abort(MPI_COMM_WORLD, 1);
 	}
	
	// get the size and rank of MPI_COMM_WORLD
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);//size = -np 5
									//return true or false, two colour (0 or 1)
    g_nslaves = size - 1;

	 MPI_Comm_split(MPI_COMM_WORLD,rank == size-1, 0, &new_comm);

    if (rank == size-1)            
		master_io(MPI_COMM_WORLD, new_comm);
    else
		//slave yo control new_comm
		slave_io( MPI_COMM_WORLD, new_comm);
    MPI_Finalize();
    return 0;
}

/* This is the master */
int master_io(MPI_Comm world_comm, MPI_Comm comm)
{	
	int worldSize;
	MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
	
	int term = -100;
	MPI_Request req;

	//send termination message after 100 seconds
	for(int i=0; i<2; i++)
	{
		sleep(30);
	}

	for(int i=0; i<worldSize-1; i++)
	{
		MPI_Send(&term, 1, MPI_INT, i, TEMTAG, MPI_COMM_WORLD);

	}
	return 0;			
}


/* This is the slave */ //superset communicator passed in
int slave_io(MPI_Comm world_comm, MPI_Comm comm)
{	
	//for MPI
	//put here to be globale variables for all processes
	int ndims=2, size, my_rank, reorder, my_cart_rank, ierr, worldSize, rank;

	//random generator variables
	unsigned int randomSeed; // random number generator seed
	int randVal;

	//random seed with time for processes
	randomSeed = rank * time(NULL);
    
    MPI_Comm_size(world_comm, &worldSize); // size of the world communicator
  	MPI_Comm_size(comm, &size); // size of the slave communicator
	MPI_Comm_rank(world_comm, &rank);  // rank of the slave communicator
	MPI_Comm_rank(comm, &my_rank);
	
	// printf("\nEnter Slave IO");
	// fflush(stdout);

	MPI_Comm comm2D;
	//split_type to split the into subcommunicator that can use shared memory
	MPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, 0,MPI_INFO_NULL, &comm2D);

    MPI_Win win;
	//each process has its own shared_data pointer
	//but all points to the same location for shared memory in mpi
    int *shared_data;
	// size of the shared memory segment based on the number of slace processes
	MPI_Comm_size(comm2D, &size);
	// Allocate shared memory
    MPI_Win_allocate_shared(sizeof(int) * size, sizeof(int), MPI_INFO_NULL, comm2D, &shared_data, &win);

	// Initialize shared memory
    for (int i = 0; i < size; i++) {
        shared_data[i] = 0;
    }
	 // Synchronize all processes
    MPI_Barrier(comm2D);
	// printf("\n Finish Initialise shared memory");
	// fflush(stdout);

	//for each process assign a parent (special version that use 0 as root at the begining)
	int par = PARENT;
	node_t node;
	node.id = rank;
	node.parent = rank > 0 ? (rank - 1) / 2 : par; //-25 meaning no parent
	//initialise the status
	node.asked = 0;
	node.isUsing = 0;
	int iniHolder = 0;
	node.holder = iniHolder;
	//initially is node 0 since tree topology build upon 0
	if (node.id == 0){
		printf("\n id: %d, using: %d, parent: %d", node.id, node.isUsing, node.parent);
		fflush(stdout);
	}

	 
	node.shaMemo = shared_data;
	// printf("\n I am process %d, my parent is %d\n", rank, node.parent);
	// fflush(stdout);

	//node_func struct to pass in node_func
	//automatically assign type, no case needed
	nodefunc_t* n_func = new nodefunc_t; // Dynamically allocate memory for n_func
	n_func->node = &node;
	n_func->world = MPI_COMM_WORLD;
	n_func->comm2D = comm2D;
	n_func->term = 1; //1 for while loop to run

	//create one thread for each node
	for (int i = 0; i < (K+1); i++) 
	{	
		if(i<K){
			port_t* port = new port_t; // Dynamically allocate memory for port
			//initialise 5 ports for all the nodes
			//assign each with id
			port->id = i;
			port->node = &node; // Assign the address of node

			//available from the begining
			port->available = 1; 
			// only create 5 threads run parallely for each process
			// create a port thread with port data as argument
			pthread_create(&node.threads[i], NULL, port_func, (void *)port); 
		}

		if(i==K){//create node_func thread with last index in threads
			
			int result = pthread_create(&node.threads[i], NULL, node_func, (void *)n_func); 
			if(result == 0){
				//printf("Thread created successfully.");
			}
		}	
	}

	MPI_Status status[worldSize-1];
	MPI_Request req[worldSize-1];

	/*Meaningful region-----------------------------------*/
	//count for while loop
	int count = 0;
	int termFlag;
	int msg;
	//while loop to communicate with threads and base station
	int whileFlag = 1;
	while(1)
	{	 
		//handle the use of token                       //has pending request
		//At the begining, random node holding the token
		//In my case, node 0 holds it
		//if some node other than it make request
		//reponse request by passing down the token since itself is the toppest parent		
		if (node.holder == node.id && !node.isUsing && !node.req_q.empty()){
			node.holder = node.req_q.front(); 

			if(node.holder==node.id){
				node.isUsing = true;

				shared_data[node.id]++;
				// MPI_Win_unlock(0, win);
				printf("\n rank: %d, shared data: %d", node.id, shared_data[node.id]);
				time_t t = time(NULL);
				char* time_str = ctime(&t);
				//make it a-append so it can append not overwrite and create a logFile if is not
				FILE *logFile = fopen("log.txt", "a");  //s come with newline
			
				//Log into file
				fprintf(logFile,"\nLogged time: %s",time_str);
				fprintf(logFile, "Process: %d with Count: %d\n",node.id, shared_data[node.id]);
				fclose(logFile);

				//after usage, back to original state
				node.req_q.pop();//pop out itself, since already done the modification
				node.asked = 0;
				printf("\n id: %d pop out %d", node.id, node.holder);
				fflush(stdout);
				node.isUsing = 0;
				node.ports[0] = 1;


			}
			//pass the token to the one that request for the token
			//if the process is not the next holder
			else
			{	
				//Update parent before sending
				//use parent to send
				int ori_p = node.parent;
				node.parent = node.holder;
				printf("\n Rank: %d, original parent: %d -> new parent: %d", node.id, ori_p, node.parent);
				fflush(stdout);

				//Send token alone to let them execute for FIFO
				MPI_Send(&node.id, 1, MPI_INT, node.parent, TOKENTAG, comm2D);

				//since sending out token 
				node.req_q.pop();
				node.asked = 0;
				printf("\nid: %d pop out %d", node.id, node.holder);
				fflush(stdout);

				if (!node.req_q.empty()){
					MPI_Send(&node.id, 1, MPI_INT, node.parent, REQTAG, comm2D); // Access members via pointer
					printf("\n %d forward request to: %d \n", node.id, node.parent);
					fflush(stdout);
				}
				
			}
	
		}
		int occupied = 0;
		for (int i =0; i < K; i++)
		{
			if (node.ports[i] == 0)
			{ 
				occupied++;
			}
		}

		//Actively send request if condition fulfilled
		//count for itself or it's neighbours that whether they are vacant
		int vacancies = 0;
		
		double rate = (double) occupied / K;

		// printf("\n %d has rate: %f \n", node.id, rate);
		// fflush(stdout);
		if (rate >=  THRESHOLD)
		{	
			//making request through the parents until the holder(toppest parent node)
			
			//push to make it no empty by pushing itself into the queue
			if(!node.asked){
				node.req_q.push(node.id);
				printf("\n %d push %d into queue\n", node.id, node.req_q.front());
				fflush(stdout);
			}
			

			//make request to the holder
			//put request node itself into the queue
			//each process send request until it reach the holder node
			//stop it from repetively sending request, if already send out one
			if(node.holder != node.id && !node.req_q.empty() && !node.asked){
				
				//send request if condition in single port fulfilled
				//do following Recv for the Send if need to get immediate reponse
				if(node.parent != PARENT)
				{
					MPI_Send(&node.id, 1, MPI_INT, node.parent, REQTAG, comm2D); // Access members via pointer
					printf("\n %d send request to: %d \n", node.id, node.parent);
					fflush(stdout);
					node.asked = true; //sent request = asked
				}

				
			}
		}

		//Probe to terminate
		MPI_Iprobe(worldSize - 1, TEMTAG, MPI_COMM_WORLD, &termFlag, &status[0]);
		if(termFlag)
		{
			whileFlag=0;
			MPI_Recv(&msg, 1, MPI_INT, worldSize-1, TEMTAG, MPI_COMM_WORLD, &status[0]);

			break;
				//terminate
		}
		count++;
		sleep(1);
	}

	//Barrier to wait for all the node to break from while loop
	MPI_Barrier(comm2D);
	n_func -> term = 0;
	
	for (int i = 0; i < (K+1); i++) 
	{	// wait for all port threads to finish
		pthread_join(node.threads[i], NULL); 
	}

	free(n_func);
	int slaveTerm = -10;
	MPI_Isend(&slaveTerm, 1, MPI_INT, worldSize-1, SLATAG, MPI_COMM_WORLD, &req[0]);
	MPI_Wait(&req[0], MPI_STATUS_IGNORE);


	return 0;
}


void *node_func(void *arg){
	int flag;
	int worldSize, size;
	int coord[2];
	MPI_Comm world_comm, comm2D;
	
	MPI_Status status;
	MPI_Request req;

	nodefunc_t *node_func = (nodefunc_t *)arg;
	world_comm = node_func -> world;
	comm2D = node_func -> comm2D;
	node_t* node = node_func -> node; // Change to pointer to node_t

	//printf("\n node rank: %d \n", node->rank); // Access members via pointer
	MPI_Comm_size(world_comm, &worldSize);

	int msg;
	int whileFlag = 1;
	while(node_func->term) //handle receive by this thread alone
	{	

		//Token handler - After receive token
		MPI_Iprobe(MPI_ANY_SOURCE, TOKENTAG, comm2D, &flag, &status);
		if(flag)
		{
			int token_source;						//source from the status
			//store the request process to the source
			//receive the token from another process
			MPI_Recv(&token_source, 1, MPI_INT, status.MPI_SOURCE, TOKENTAG, comm2D, &status);
			printf("\n rank: %d receive token from %d \n", node->id, token_source);
			fflush(stdout);
			node->holder = node->id;

		}
		//After receive request
		MPI_Iprobe(MPI_ANY_SOURCE, REQTAG, comm2D, &flag, &status);
		if(flag)
		{
			int source;						//source from the status

			MPI_Recv(&source, 1, MPI_INT, status.MPI_SOURCE, REQTAG, comm2D, &status);
			printf("\n Recv %d push %d into queue\n", node->id, source);
			fflush(stdout);	
			node->req_q.push(source);

			//If the source if not the holder, continue to send to its parent
			//stop sending if node.id = node.holder
			
			if(node->id != node->holder && !node->req_q.empty() && !node->asked){
				node->asked = true;
				if(node->parent != PARENT){
					MPI_Send(&node->id, 1, MPI_INT, node->parent, REQTAG, comm2D);
					printf("Recv-Send, id: %d to parent: %d", node->id, node->parent);
					fflush(stdout);
				}
				
				
			}
			

		}
		
		
	}
	return NULL;
}


void *port_func(void *arg)
{
    // Cast the argument to a pointer to a port_t struct
    port_t *port = (port_t *)arg;

    // Use the port pointer
    srand(port->id + port->node->id);

    // Set availability to 0
    port->available = 0;

    // Update on the port array with the availability
    for(int i= 0; i < sizeof(port->node->ports)/sizeof(port->node->ports[0]); i++)
	{
		// // //Randomly set availability to 0 or 1, but mostly 0
        port->node->ports[i] = (rand() % 10 < 9) ? 0 : 1; 
		
	}
    // Sleep for 10 seconds
    sleep(10);

    return NULL;
}



	
	
