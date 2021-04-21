/*********************************************************
* Author: Philip Wright                                  *
* Project 5                                              *
* Date: April 20, 2021
**********************************************************/
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/msg.h>
#include <getopt.h>
#include <string.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define SHMCLOCKKEY	86868             /* Parent and child agree on common key for clock.*/
#define MSGQUEUEKEY	68686            /* Parent and child agree on common key for msgqueue.*/
#define MAXRESOURCEKEY	71657            /* Parent and child agree on common key for resources.*/

#define TERMCONSTANT 2 				// Percent chance that a child process will terminate instead of requesting/releasing a resource
#define REQUESTCONSTANT 50			// Percent chance that a child process will request a new resource

#define PERMS (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define FLAGS (O_CREAT | O_EXCL)

static volatile sig_atomic_t childDoneFlag = 0;

typedef struct {
	int seconds;
	int nanosecs;
} clockStruct;

typedef struct {
	long mtype;
	pid_t pid;
	int msg;
} mymsg_t;

typedef struct {
	int resourcesUsed[20];
} resourceStruct;

//globals
static int queueid;
static resourceStruct *maxResources;
static resourceStruct *allocatedResources;
static clockStruct *sharedClock;
static mymsg_t *toParentMsg;


int lenOfMessage;
int shmclock;
int maxResourceSegment;

int sigHandling();
int initPCBStructures();
static void endChild(int signo);
void tearDown();

int main (int argc, char *argv[]){

	srand(time(NULL) + getpid());

	pid_t pid = getpid();
	int i;
	int resIndex;
	int randomResourceChoice;
	int randomActionTaken;
	int resCount = 0;


	sigHandling();
	initPCBStructures();

	while(!childDoneFlag){
			randomActionTaken = rand() % 100;
			// child terminates
			if(randomActionTaken < TERMCONSTANT){
				childDoneFlag = 1;
				//send termination status to parent to deallocated resources
				toParentMsg->mtype = 1;
				toParentMsg->pid = pid;
				toParentMsg->msg = 0;
				msgsnd(queueid, toParentMsg, lenOfMessage, 1);
				if (msgrcv(queueid, toParentMsg, lenOfMessage, pid, 0) != -1){
					allocatedResources->resourcesUsed[randomResourceChoice] += 1;
				}

			//request resource
			} else if (randomActionTaken >= TERMCONSTANT && randomActionTaken < REQUESTCONSTANT){
				randomResourceChoice = rand() % 20;
				if ((allocatedResources->resourcesUsed[randomResourceChoice]) < (maxResources->resourcesUsed[randomResourceChoice]) && allocatedResources->resourcesUsed[randomResourceChoice] < 1){
					toParentMsg->mtype = 3;
					toParentMsg->pid = pid;
					toParentMsg->msg = randomResourceChoice;
					msgsnd(queueid, toParentMsg, lenOfMessage, 3);
					if (msgrcv(queueid, toParentMsg, lenOfMessage, pid, 0) != -1){
						allocatedResources->resourcesUsed[randomResourceChoice] += 1;
						resCount += 1;
					}

				}
			// release randomly selected resource
			} else {
				if(resCount > 0){
					resIndex = 0;
					randomResourceChoice = rand() % resCount;
					for(i = 0; i < 20; i++){
						if(allocatedResources->resourcesUsed[i] > 0){
							if (resIndex == randomResourceChoice){
								toParentMsg->mtype = 2;
								toParentMsg->pid = pid;
								toParentMsg->msg = i;
								msgsnd(queueid, toParentMsg, lenOfMessage, 2);
								if (msgrcv(queueid, toParentMsg, lenOfMessage, pid, 0) != -1){
									allocatedResources->resourcesUsed[i] -= 1;
									resCount -= 1;
									i = 20;
								}
							} else {
								resIndex += 1;
							}
						}
					}
				}	
			}
			
			if(sharedClock->seconds >= 1000){
				childDoneFlag = 1;
			}
	}
	
	printf("End of child %d: [", pid);
	for (i = 0; i < 20; i++){
		printf("%d,", allocatedResources->resourcesUsed[i]);
	}
	printf("]\n");
	exit(1);
	
	return 1;
}

int initPCBStructures(){
	// init clock
	shmclock = shmget(SHMCLOCKKEY, sizeof(clockStruct), 0666 | IPC_CREAT);
	sharedClock = (clockStruct *)shmat(shmclock, NULL, 0);

	//init resources
	maxResourceSegment = shmget(MAXRESOURCEKEY, (sizeof(resourceStruct) + 1), 0666 | IPC_CREAT);
	maxResources = (resourceStruct *)shmat(maxResourceSegment, NULL, 0);
	if (maxResourceSegment == -1){
		return -1;
	}

	// init allocated resources
	allocatedResources = malloc(sizeof(resourceStruct));
	int r;
	for (r = 0; r < 20; r++){
		allocatedResources->resourcesUsed[r] = 0;
	}

	//queues
	queueid = msgget(MSGQUEUEKEY, PERMS | IPC_CREAT);
	if (queueid == -1){
		return -1;
	} 

	// init to message struct 
	toParentMsg = malloc(sizeof(mymsg_t));
	lenOfMessage = sizeof(mymsg_t) - sizeof(long);
	
	return 0;
}

void tearDown(){
	shmdt(clock);
	shmdt(maxResources);

 	msgctl(queueid, IPC_RMID, NULL);
}

int sigHandling(){

	//set up handler for ctrl-C
	struct sigaction controlc;

	controlc.sa_handler = endChild;
	controlc.sa_flags = 0;

	if ((sigemptyset(&controlc.sa_mask) == -1) || (sigaction(SIGINT, &controlc, NULL) == -1)) {
		perror("Failed to set SIGINT to handle control-c");
		return -1;
	}

	//set up handler for when child terminates
	struct sigaction sigParent;

	sigParent.sa_handler = endChild;
	sigParent.sa_flags = 0;

	if ((sigemptyset(&sigParent.sa_mask) == -1) || (sigaction(SIGCHLD, &sigParent, NULL) == -1)) {
		perror("Failed to set SIGCHLD to handle signal from child process");
		return -1;
	}

	return 1;
}

static void endChild(int signo){
		childDoneFlag = 1;
}
