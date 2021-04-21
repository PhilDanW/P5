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

#include "oss.h"

#define SHMCLOCKKEY	86868            /* Parent and child agree on common key for clock.*/
#define MSGQUEUEKEY	68686            /* Parent and child agree on common key for msgqueue.*/
#define MAXRESOURCEKEY	71657            /* Parent and child agree on common key for resources.*/

#define PERMS (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define FLAGS (O_CREAT | O_EXCL)

Queue *firstInBlockedQueue;
Queue *lastInBlockedQueue; 
Queue *firstInProcessList;
Queue *lastInProcessList;

//globals
static volatile sig_atomic_t doneflag = 0;
static clockStruct *sharedClock;
static clockStruct *forkTime;
static resourceStruct *maxResources;
static resourceStruct *allocatedResources;
static resourceStruct *availableResources;
static mymsg_t *toParentMsg;
static int queueid;

//other necessary variables
int randForkTime;
int maxResourceSegment;
int childCounter;
int shmclock;
int lenOfMessage;

int requestsGranted;
int deadlockFunctionCount;

int main (int argc, char *argv[]){

	//setup output log file
	FILE *logfile;
	logfile = fopen("logfile", "w");

	srand(time(NULL) + getpid());

	Queue * tmpPtr;
	Queue * prevTmpPtr;

	//initialize all variables
	tmpPtr = NULL;
	prevTmpPtr = NULL;

	firstInBlockedQueue = NULL;
	lastInBlockedQueue = NULL; 

	firstInProcessList = NULL;
	lastInProcessList = NULL;

	int childPid;
	int timeLimit = 2;
	int tmpRes;
	int verbose = 1; //1: verbose on
	int childLimit = 18;
    int totalChildren = 0;
    int totalLinecount = 0;

	childCounter = 0;
    requestsGranted = 0;
	deadlockFunctionCount = 0;

	//initialize signal handling, process control blocks and the signal for if the time limit is reached
	sigHandling();
	initPCBStructures();
	alarm(timeLimit);

	// initialize the message struct 
	toParentMsg = malloc(sizeof(mymsg_t));
	lenOfMessage = sizeof(mymsg_t) - sizeof(long);

	//initialize allocated resources
	allocatedResources = malloc(sizeof(resourceStruct));
	availableResources = malloc(sizeof(resourceStruct));

	// initialize both resource structs' resource elements
	int i;
	for (i = 0; i < 20; i++){
		maxResources->resourcesUsed[i] = (rand() % 9) + 1;
		availableResources->resourcesUsed[i] = maxResources->resourcesUsed[i];
		allocatedResources->resourcesUsed[i] = 0;
		printf("Available resources [%d]: %d\n", i, availableResources->resourcesUsed[i]);
	}
	
	//set the timer
	setForkTimer();
	
	//the main loop of the oss, which will go exec the user.c executable for the user process portion
	while(!doneflag){
		if(childCounter < childLimit && checkIfTimeToFork() == 1){

			//execlp ./user
			if ((childPid = fork()) == 0){
				execlp("./user", "./user", (char*)NULL);
				fprintf(stderr, "%sFailed to exec user process!\n", argv[0]);
				_exit(1);
			}	

			//if the processing list is empty add the pid to the queue
			if (firstInProcessList == NULL){
				firstInProcessList = newProcessMember(childPid);
				lastInProcessList = firstInProcessList;
			//else add it to the end of the queue	
			} else {
				lastInProcessList = lastInProcessList->next = newProcessMember(childPid);
			}

			totalChildren  += 1;
			childCounter += 1;
			printf("Process %d was created at %d:%d\n", childPid, sharedClock->seconds, sharedClock->nanosecs);
			setForkTimer();
		}	

		//if a child process is terminating
		if(msgrcv(queueid, toParentMsg, lenOfMessage, 1, IPC_NOWAIT) != -1){
			deleteFromProcessList(toParentMsg->pid, firstInProcessList);

			toParentMsg->mtype = toParentMsg->pid;
			msgsnd(queueid, toParentMsg, lenOfMessage, 0);
		}

		//if a child process requests resources
		if(msgrcv(queueid, toParentMsg, lenOfMessage, 3, IPC_NOWAIT) != -1){
			if(verbose == 1 && totalLinecount <= 100000){
				fprintf(logfile, "Master has detected Process P%d requesting R%d at time %d:%d\n", toParentMsg->pid, toParentMsg->res, sharedClock->seconds, sharedClock->nanosecs);
				totalLinecount += 1;
			}

			deadlockFunctionCount += 1;
			if (bankersAlgorithm(toParentMsg->res, findPCB(toParentMsg->pid, firstInProcessList)) == 1){
				if(verbose == 1 && totalLinecount <= 100000){
					fprintf(logfile, "Master granting Process P%d request R%d at time %d:%d\n", toParentMsg->pid, toParentMsg->res, sharedClock->seconds, sharedClock->nanosecs);
					if(deadlockFunctionCount % 20 == 0){
						int o; 
						int n = 0;
						tmpPtr = firstInProcessList;
						fprintf(logfile, "     ");
						for(o = 0; o < 20; o++){
							fprintf(logfile, "R%d ", o);
						}
						totalLinecount += 1;
						fprintf(logfile, "\n");
						totalLinecount += 1;
						while(tmpPtr != NULL){
							fprintf(logfile, "P%d", n);
							for(o = 0; o < 20; o++){
								fprintf(logfile, "   %d", tmpPtr->head->resUsed->resourcesUsed[o]);
							}
							fprintf(logfile, "\n");
							tmpPtr = tmpPtr->next;
							n += 1;
							totalLinecount += 1;
						}
					}
					totalLinecount += 1;
				}

				requestsGranted += 1;
				findPCB(toParentMsg->pid, firstInProcessList)->resUsed->resourcesUsed[toParentMsg->res] += 1;
				toParentMsg->mtype = toParentMsg->pid;
				// printf("\nRESOURCE GRANTED! %d for %d\n", toParentMsg->res, toParentMsg->pid);
				msgsnd(queueid, toParentMsg, lenOfMessage, 0);
			} else {
				fprintf(logfile, "Process P%d has been blocked!\n", toParentMsg->pid);
				totalLinecount += 1;
				
				// put in blocked queue
				if (firstInBlockedQueue == NULL){
					firstInBlockedQueue = newBlockedQueueMember(findPCB(toParentMsg->pid, firstInProcessList));
					lastInBlockedQueue = firstInBlockedQueue;
				} else {
					lastInBlockedQueue = lastInBlockedQueue->next = newBlockedQueueMember(findPCB(toParentMsg->pid, firstInProcessList));
				}
				lastInBlockedQueue->head->blockedBurstSecond = sharedClock->seconds;
				lastInBlockedQueue->head->blockedBurstNano = sharedClock->nanosecs;
				lastInBlockedQueue->head->requestedResource = toParentMsg->res;
			}

		}
		// child releasing resource
		if(msgrcv(queueid, toParentMsg, lenOfMessage, 2, IPC_NOWAIT) != -1){
				if(verbose == 1 && totalLinecount <= 100000){
					fprintf(logfile, "Master has acknowledged Process P%d releasing R%d at time %d:%d", toParentMsg->pid, toParentMsg->res, sharedClock->seconds, sharedClock->nanosecs);
					totalLinecount += 1;
				}			
			allocatedResources->resourcesUsed[toParentMsg->res] -= 1;
			availableResources->resourcesUsed[toParentMsg->res] += 1;
			findPCB(toParentMsg->pid, firstInProcessList)->resUsed->resourcesUsed[toParentMsg->res] -= 1;

			toParentMsg->mtype = toParentMsg->pid;
			msgsnd(queueid, toParentMsg, lenOfMessage, 0);

		}

		// try to remove element from blocked queue
		tmpPtr = firstInBlockedQueue;
		prevTmpPtr = firstInBlockedQueue;

		while(tmpPtr != NULL){
			tmpRes = tmpPtr->head->requestedResource;
			
			if ((allocatedResources->resourcesUsed[tmpRes]) < (maxResources->resourcesUsed[tmpRes])){
				if(tmpPtr == prevTmpPtr){
					firstInBlockedQueue = tmpPtr->next;
				} else {
					if (tmpPtr->next == NULL){
						lastInBlockedQueue = prevTmpPtr;
					}

						prevTmpPtr->next = tmpPtr->next;	
				}
				fprintf(logfile, "Process P%d has been unblocked!\n", tmpPtr->head->pid);
				totalLinecount += 1;
				allocatedResources->resourcesUsed[tmpRes] += 1;
				availableResources->resourcesUsed[tmpRes] -= 1;
				findPCB(tmpPtr->head->pid, firstInProcessList)->resUsed->resourcesUsed[tmpRes] += 1;

				toParentMsg->mtype = tmpPtr->head->pid;
				msgsnd(queueid, toParentMsg, lenOfMessage, 0);
				tmpPtr = NULL;
				prevTmpPtr = NULL;
			} else {
				if (prevTmpPtr != tmpPtr){
					prevTmpPtr = tmpPtr;
				}

				tmpPtr = tmpPtr->next;
			}
		}

         sharedClock->nanosecs += 1000000;
            if (sharedClock->nanosecs >= 1000000000){
                sharedClock->seconds += 1;
                sharedClock->nanosecs = sharedClock->nanosecs % 1000000000;
            }
            if (sharedClock->seconds >= 1000){
                sharedClock->nanosecs = 0;
                doneflag = 1;
            }

            if(totalChildren >= 99){
            	doneflag = 1;
            }

    }

    while(childCounter > 0){
    				// totalChildren  += 1;

    	printf("Child count: %d\n", childCounter);
	//sleep for 2 seconds
    	sleep(5);

    }

    	printf("Final Clock time is at %d:%d\n", sharedClock->seconds, sharedClock->nanosecs);
	printf("Final total process count: %d\n", totalChildren);
	printf("Total Requests: %d\n", deadlockFunctionCount);
	printf("Requests granted: %d\n", requestsGranted);
	printf("Percentage of requests granted: %f\n", (float)requestsGranted/(float)deadlockFunctionCount*100);

	tearDown();

	fclose(logfile);
	return 0;
}

int sigHandling(){

	//set up alarm after some time limit
	struct sigaction timerAlarm;

	timerAlarm.sa_handler = endAllProcesses;
	timerAlarm.sa_flags = 0;

	if ((sigemptyset(&timerAlarm.sa_mask) == -1) || (sigaction(SIGALRM, &timerAlarm, NULL) == -1)) {
		perror("Failed to set SIGALRM to handle timer alarm");
		return -1;
	}

	//set up handler for SIGINT
	struct sigaction controlc;

	controlc.sa_handler = endAllProcesses;
	controlc.sa_flags = 0;

	if ((sigemptyset(&controlc.sa_mask) == -1) || (sigaction(SIGINT, &controlc, NULL) == -1)) {
		perror("Failed to set SIGINT to handle control-c");
		return -1;
	}

	//set up handler for when child terminates
	struct sigaction workerFinished;

	workerFinished.sa_handler = childFinished;
	workerFinished.sa_flags = 0;

	if ((sigemptyset(&workerFinished.sa_mask) == -1) || (sigaction(SIGCHLD, &workerFinished, NULL) == -1)) {
		perror("Failed to set SIGCHLD to handle signal from child process");
		return -1;
	}


	return 1;
}

//end all processes
static void endAllProcesses(int signo){
	doneflag = 1;
	if(signo == SIGALRM){
		printf("\nKILLING ALL PROCESSES\n");
		killpg(getpgid(getpid()), SIGINT);
	}
}

//determine if a child is finished processing
static void childFinished(int signo){
	pid_t finishedpid;
	while((finishedpid = waitpid(-1, NULL, WNOHANG))){
		if((finishedpid == -1) && (errno != EINTR)){
			break;
		} else {
			printf("Child %d is done!\n", finishedpid);
			childCounter -= 1;
		}
	}
}

//set up the structs of the process control block
int initPCBStructures(){
	// initialize clock
	shmclock = shmget(SHMCLOCKKEY, sizeof(clockStruct), 0666 | IPC_CREAT);
	sharedClock = (clockStruct *)shmat(shmclock, NULL, 0);
	if (shmclock == -1){
		return -1;
	}

	sharedClock -> seconds = 0;
	sharedClock -> nanosecs = 0;

	//determine when to fork new child process
	forkTime = malloc(sizeof(clockStruct));

	//intialize resources
	maxResourceSegment = shmget(MAXRESOURCEKEY, (sizeof(resourceStruct) + 1), 0666 | IPC_CREAT);
	maxResources = (resourceStruct *)shmat(maxResourceSegment, NULL, 0);
	if (maxResourceSegment == -1){
		return -1;
	}

	//queues
	queueid = msgget(MSGQUEUEKEY, PERMS | IPC_CREAT);
	if (queueid == -1){
		return -1;
	} 

	return 0;
}

//tear down and clean up the shared clock, resources, and queue IDs
void tearDown(){
	shmdt(sharedClock);
	shmctl(shmclock, IPC_RMID, NULL);
	shmdt(maxResources);
	shmctl(maxResourceSegment, IPC_RMID, NULL);
	msgctl(queueid, IPC_RMID, NULL);
}

//initialize the attributes of a new member added to process queue
Queue *newProcessMember(int pid)
{
    Queue *newQ;
    newQ = malloc(sizeof(Queue));
    newQ->next = NULL;
    newQ->head = malloc(sizeof(PCB));
    newQ->head = newPCB(pid);
    
    return newQ;
}

//initialize new member of the blocked queue
Queue *newBlockedQueueMember(PCB *pcb)
{
    Queue *newQ;
    newQ = malloc(sizeof(Queue));
    newQ->next = NULL;
    newQ->head = malloc(sizeof(PCB));
    newQ->head = pcb;
    
    return newQ;
}

//remove a process from the list
void deleteFromProcessList(int pidToDelete, Queue *ptr){
	//if the process to be deleted if the first in the queue
	if (ptr->head->pid == pidToDelete){
		releaseAllResources(ptr->head->resUsed);
		//assign next in line to the head of the queue
		firstInProcessList = ptr->next;
		return;
	} else {
		while(ptr != NULL){
			if (ptr->next->head->pid == pidToDelete){
				releaseAllResources(ptr->next->head->resUsed);
				ptr->next = ptr->next->next;
				if(ptr->next == NULL){
					lastInProcessList = ptr;
				}
				return;
			} else {
				ptr = ptr->next;
			}
		}
	}
}

//print the queue to the screen
void printQueue(Queue * ptr){
	while(ptr != NULL){
		printf("Pid: %d\n", ptr->head->pid);
		printf("Total Blocked Time: %d\n", ptr->head->totalBlockedTime);
		printf("Blocked Burst Nano: %d\n", ptr->head->blockedBurstNano);
		printf("Blocked Burst Second: %d\n", ptr->head->blockedBurstSecond);
		printf("Requested Resource: %d\n", ptr->head->requestedResource);
		int n;
		printf("Resources: [");
		for(n = 0; n < 20; n++){
			printf("%d,", ptr->head->resUsed->resourcesUsed[n]);
		}
		printf("]\n");
		ptr = ptr->next;
	}
}

//initialize process control block
PCB *newPCB(int pid){
	PCB *newP;
	newP = malloc(sizeof(PCB));
	newP->pid = pid;
	newP->totalBlockedTime = 0;
	newP->blockedBurstSecond = 0;
	newP->blockedBurstNano = 0;
	newP->requestedResource = 0;
	newP->resUsed = malloc(sizeof(resourceStruct));
	int n;
	for(n = 0; n < 20; n++){
		newP->resUsed->resourcesUsed[n] = 0;
	}

	return newP;
}

//locate a specific process control block by the pointer head
PCB *findPCB(int pid, Queue * ptrHead){
	while(ptrHead != NULL){
		if(ptrHead->head->pid == pid){
			return ptrHead->head;
		} else {
			ptrHead = ptrHead->next;
		}
	}
	return NULL;
}

//checks to see if a process is allowed to fork
//if the shared clock nano/seconds is greater than or equal to fork time, then its time to fork!
int checkIfTimeToFork(){
	if ((sharedClock->nanosecs >= forkTime->nanosecs) && (sharedClock->seconds >= forkTime->seconds)){
		return 1;
	} else {
		if(sharedClock->seconds < 2 && sharedClock->nanosecs%100000000 == 0){

	}
		return 0;
	}
}

//sets up the fork timer
void setForkTimer(){
	randForkTime = (rand() % 500) * 1000000;

	forkTime->nanosecs = sharedClock->nanosecs + randForkTime;
	forkTime->seconds = sharedClock->seconds;
	if(forkTime->nanosecs >= 1000000000){
		forkTime->seconds += 1;
		forkTime->nanosecs = forkTime->nanosecs%1000000000;
	}
}

int deadlockAvoidance(int res){
	if((allocatedResources->resourcesUsed[res]) < (maxResources->resourcesUsed[res])){
		allocatedResources->resourcesUsed[res] += 1;
		availableResources->resourcesUsed[res] -= 1;
		return 1;
	} else {
		return 0;
	}
}

int bankersAlgorithm(int res, PCB * proc){
	int r;
	int s;

	if(availableResources->resourcesUsed[res] > 1){
		allocatedResources->resourcesUsed[res] += 1;
		availableResources->resourcesUsed[res] -= 1;
		return 1;
	} else if (availableResources->resourcesUsed[res] == 0){
		return 0;
	} else {
		for(r = 0; r < 20; r++){
			s = r;
			if(r == res){
				s = res + 1;
			}
			if(availableResources->resourcesUsed[s] + proc->resUsed->resourcesUsed[s] < 1){
				return 0;
			}
		}
		allocatedResources->resourcesUsed[res] += 1;
		availableResources->resourcesUsed[res] -= 1;
		return 1;
	}

}

//clean up all available and allocated resources
void releaseAllResources(resourceStruct * res){

	int r;

	for (r = 0; r < 20; r++){
		if(res->resourcesUsed[r] > 0){

			availableResources->resourcesUsed[r] += res->resourcesUsed[r];
			allocatedResources->resourcesUsed[r] -= res->resourcesUsed[r];
			
		}
	}
}
