/************************************************************************************
 * Name: Jackson Hoenig
 * Class: CMPSCI 4760
 * Project 4
 * Description:
 * This Project is an operating system scheduler simulator.  The OSS will periodically generate a launch
 * time that it will then fork off a child process exececuting the USER binary. The time is kept track of
 * in the shared memory clock that is incremented in the OSS after each iteration of the loop.
 * The Oss can be called with option t to change the timeout from the default of 3 real-time seconds.
 * The oss communicates with the forked children by going through a round robin queue of three priorities.
 * then the oss dispatches the process by updating shared memory that the children are looking at.
 * oss waits for message received back then either executes the next available process or loops back to the start.
 * See readme for more information.
 * *************************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <errno.h>
#include <sys/msg.h>
#include "customStructs.h"
#include "semaphoreFunc.h"
#include "bitMap.h"
#define PERMS (IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
#define ALPHA 1
#define BETA 1

//global var.
int processes = 0;

//prototype for exit safe
void exitSafe(int);
bool isSafe(int);
//Global option values
int maxChildren = 10;
char* logFile = "log.txt";
int timeout = 3;
long double passedTime = 0.0;

//get shared memory
static int shmid;
static int shmidPID;
static int semid;
static int shmidPCB;
static int shmidRD;
static struct Clock *shmclock;
static struct PCB *shmpcb;
static struct Dispatch *shmpid;       
static struct RD *shmrd;
//get message queue id
int msgid;
                       
//array to hold the pid's of all the child processes
int pidArray[20];
char unsigned bitMap[3];
//function is called when the alarm signal comes back.
void timerHandler(int sig){
	long double througput = passedTime / processes;
	printf("Process has ended due to timeout, Processses finished: %d, time passed: %Lf\n",processes,passedTime);
	printf("OSS: it takes about %Lf seconds to generate and finish one process \n",througput);
	printf("OSS: throughput = %Lf\n",processes / passedTime);
	fflush(stdout);
	//get the pids so we can kill the children
	int i = 0;
	shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
        for(i = 0; i < 18; i++) {
	       if(((bitMap[i/8] & (1 << (i % 8 ))) == 0) && shmpcb[i].simPID != getpid())
		pidArray[i] = shmpcb[i].simPID;
        }
	shmdt(shmpcb);
	//printf("Killed process \n");
	exitSafe(1);
}
//function to exit safely if there is an error or anything
//it removes the IPC data.
void exitSafe(int id){
	//destroy the Semaphore memory
	if(removesem(semid) == -1) {
               perror("Error: oss: Failed to clean up semaphore");
       }
	//shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
	//fclose(fp);
        shmdt(shmpcb);
	
	//destroy msg queue
	msgctl(msgid,IPC_RMID,NULL);
	//detatch dispatch shm
	shmdt(shmpid);
	//destroy shared memory 
        shmctl(shmid,IPC_RMID,NULL);
        shmctl(shmidPCB,IPC_RMID,NULL);
        shmctl(shmidRD,IPC_RMID,NULL);
	shmctl(shmidPID,IPC_RMID,NULL);
	int i;
	//kill the children.
	for(i = 0; i < 18; i++){
		if((bitMap[i/8] & (1 << (i % 8 ))) != 0){	
			//if(pidArray[i] == getpid()){
				kill(pidArray[i],SIGKILL);
			//}		
		}	
	}
	exit(0);
}

unsigned long long calcWait(const struct Queue * queue);
void incrementClock();
//Global for increment clock
struct sembuf semwait[1];
struct sembuf semsignal[1];


int main(int argc, char **argv){
	//const int ALPHA = 1;
	//const int BETA = 1;
	int opt;
	//get the options from the user.
	while((opt = getopt (argc, argv, "ht:")) != -1){
		switch(opt){
			case 'h':
				printf("Usage: ./oss [-t [timeout in seconds]]\n");
				exit(1);
			case 't':
				timeout = atoi(optarg);
				break;
			
			default:
				printf("Wrong Option used.\nUsage: ./oss [-t [timeout in seconds]]\n");
				exit(1);
		}
	}
	//set the countdown until the timeout right away.
	alarm(timeout);
        signal(SIGALRM,timerHandler);
	
	//get shared memory for clock
	key_t key = ftok("./oss",45);
	if(errno){
		perror("Error: oss: Shared Memory key not obtained: ");
		exitSafe(1);
	}	
	shmid = shmget(key,1024,0666|IPC_CREAT);
	if (shmid == -1 || errno){
		perror("Error: oss: Failed to get shared memory. ");
		exitSafe(1);
	}
	shmclock = (struct Clock*) shmat(shmid,(void*)0,0);
	shmclock->second = 0;
	shmclock->nano = 0;
	shmdt(shmclock);
 
	//get shared memory for the Process Control Table
	key_t pcbkey = ftok("./oss",'v');
	if(errno){
		perror("Error: oss: Shared Memory key not obtained: ");
                exitSafe(1);
	}
	size_t PCBSize = sizeof(struct PCB) * 18;
	shmidPCB = shmget(pcbkey,PCBSize,0666|IPC_CREAT);
        if (shmidPCB == -1 || errno){
                perror("Error: oss: Failed to get shared memory. ");
                exitSafe(1);
        }
        shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
        int x;
	for(x = 0; x < 18; x++){
		shmpcb[x].CPU = 0;
		shmpcb[x].simPID = 0;
		int i;
		for(i=0; i < 20; i++){
			shmpcb[x].claims[i] = 0;
                	shmpcb[x].taken[i] = 0;
			shmpcb[x].needs[i] = 0;
		}
	}
        shmdt(shmpcb);
	//setup the shared memory for the Resource Descriptor
	key_t rdkey = ftok("./oss", 'g');
	size_t RDSize = sizeof(struct RD) * 20;
	shmidRD = shmget(rdkey,RDSize,0666|IPC_CREAT);
	if (shmidRD == -1 || errno){
		perror("Error: oss: Failed to get shared memory");
		exitSafe(1);
	}
	shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
	for(x = 0; x < 20; x++){
		if((rand() % 100) <= 20){
			//it is sharable
			shmrd[x].sharable = true;
		}
		shmrd[x].total = rand() % 10 + 1;
		shmrd[x].available = shmrd[x].total;	
	}
	shmdt(shmrd);	
	//get the shared memory for the pid and quantum
	key_t pidkey = ftok("./oss",'m');
	if(errno){
                perror("Error: oss: Shared Memory key not obtained: ");
                exitSafe(1);
        }
        shmidPID = shmget(pidkey,16,0666|IPC_CREAT);
        if (shmidPID == -1 || errno){
                perror("Error: oss: Failed to get shared memory. ");
                exitSafe(1);
        }
        
	
	//Initialize semaphore
	//get the key for shared memory
	key_t semKey = ftok("/tmp",'j');
	if( semKey == (key_t) -1 || errno){
                perror("Error: oss: IPC error: ftok");
                exitSafe(1);
        }
	//get the semaphore id
	semid = semget(semKey, 1, PERMS);
       	if(semid == -1 || errno){
		perror("Error: oss: Failed to create a private semaphore");
		exitSafe(1);	
	}
	
	//declare semwait and semsignal
	setsembuf(semwait, 0, -1, 0);
	setsembuf(semsignal, 0, 1, 0);
	if (initElement(semid, 0, 1) == -1 || errno){
		perror("Failed to init semaphore element to 1");
		if(removesem(semid) == -1){
			perror("Failed to remove failed semaphore");
		}
		return 1;
	}

	//get message queue key
	key_t msgKey = ftok("/tmp", 'k');
	if(errno){
		
		perror("Error: oss: Could not get the key for msg queue. ");
		exitSafe(1);
	}
	//set message type
	message.mesg_type = 1;
		
	//get the msgid
	msgid = msgget(msgKey, 0600 | IPC_CREAT);
	if(msgid == -1 || errno){
		perror("Error: oss: msgid could not be obtained");
		exitSafe(2);
	}

	
//*******************************************************************
//The loop starts below here. before this there is only setup for 
//shared memory, semaphores, and message queues.
//******************************************************************	
	//open logfile
	FILE *fp;	
	remove(logFile);
	fp = fopen(logFile, "a");

//FOR TESTING
shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
fprintf(fp,"Total in RD: < ");
int abc = 0;
for(abc = 0; abc < 20; abc++){
	fprintf(fp,"%d,",shmrd[abc].total);
}
fprintf(fp,"\n\n");
shmdt(shmrd);

	
	//Create the queues
	struct Queue* priorityZero = createQueue();
		

	//for limit purposes.
	int lines = 0;
	processes = 0;

	const int maxTimeBetweenNewProcsNS = 10;
	const int maxTimeBetweenNewProcsSecs = 2;
	shmpid = (struct Dispatch*) shmat(shmidPID,(void*)0,0);
	struct Clock launchTime;
	launchTime.second = 0;
	launchTime.nano = 0;
	//struct Clock currentTime;
	//make the initial 18 processes
	//for(i = 0; i < 1; i++){
	int lastPid = -1;
	
	//int j;
	//for(j = 0; j < 1; j++){
	while(1){
		//if we do not have a launch time, generate a new one.
		if(launchTime.second == 0 && launchTime.nano == 0){
			//printf("Finding a launch time\n");
			lines++;
			if(lines < 10000){
				printf("Finding a launch time.\n");
				fprintf(fp,"OSS: generating a new launch time.\n");
			}//Create new launch time
			if (r_semop(semid, semwait, 1) == -1){
                                        perror("Error: oss: Failed to lock semid. ");
                                        exitSafe(1);
                        }
                        else{
				shmclock = (struct Clock*) shmat(shmid, (void*)0,0);
                                launchTime.second = shmclock->second + (rand() % maxTimeBetweenNewProcsSecs);
                                launchTime.nano = shmclock->nano + (rand() % maxTimeBetweenNewProcsNS);
				shmdt(shmclock);
					
				if (r_semop(semid, semsignal, 1) == -1) {
                        	       perror("Error: oss: failed to signal Semaphore. ");
                        	       exitSafe(1);
                        	}
			}
				

			//printf("Launch time: %d:%d",launchTime.second,launchTime.nano);
		}
		//check if we need to launch the processes
		//printf("Outside the for loop\n");
		
		//get the semaphore	
		if (r_semop(semid, semwait, 1) == -1){
                       perror("Error: oss: Failed to lock semid. ");
                       exitSafe(1);
                }
                else{	
			shmclock = (struct Clock*) shmat(shmid, (void*)0,0);
			if(shmclock->second > launchTime.second || (shmclock->second == launchTime.second && shmclock->nano > launchTime.nano)){
				//if we have an open bit, fork.
				if(((lastPid = openBit(bitMap,lastPid)) != -1) && (processes < 100)){
					launchTime.second = 0;
					launchTime.nano = 0;
					//launch the process
					pid_t pid = fork();
                       			if(pid == 0){
                                		char arg[20];
                                		snprintf(arg, sizeof(arg), "%d", shmid);
                                		char spid[20];
                                		snprintf(spid, sizeof(spid), "%d", semid);
                                		char msid[20];
                                		snprintf(msid, sizeof(msid), "%d", msgid);
                                		char disId[20];
                                		snprintf(disId, sizeof(disId), "%d", shmidPID);
						char pcbID[20];
						snprintf(pcbID, sizeof(pcbID), "%d", shmidPCB);
						char bitIndex[20];
						snprintf(bitIndex, sizeof(bitIndex), "%d", lastPid);
						char rdid[20];
						snprintf(rdid, sizeof(rdid), "%d", shmidRD);
                                		execl("./user","./user",arg,spid,msid,disId,pcbID,bitIndex,rdid,NULL);
                                		perror("Error: oss: exec failed. ");
                                		//fclose(fp);
						exitSafe(1);
                        		}
					else if(pid == -1){
						perror("OSS: Failed to fork.");
						exitSafe(2);
					}
                        		//printf("forked child %d\n",pid);
                        		lines++;
					if(lines < 10000){
                        		fprintf(fp,"OSS: Generating process %d with pid %lld and putting it in queue 0 at time %d:%d\n",processes,(long long)pid,shmclock->second,shmclock->nano);
					}
					fflush(fp);	
					printf("OSS: Generating process %d with pid %lld and putting it in queue 0 at time %d:%d\n",processes,(long long)pid,shmclock->second,shmclock->nano);
					//place in queue
					enQueue(priorityZero,lastPid);
					processes++;
					//printf("Queue size: %d \n",sizeOfQueue(priorityZero));
					
                       		        //Setting up the PCB
                       		        shmpcb = (struct PCB*) shmat(shmidPCB, (void*)0,0);
                       		        shmpcb[lastPid].simPID = pid;
					//set the bit to 1.		
					setBit(bitMap,lastPid);
                        	        shmdt(shmpcb);
				}
			}
			shmdt(shmclock);
				
			if (r_semop(semid, semsignal, 1) == -1) {
                        	perror("Error: oss: failed to signal Semaphore. ");
                               	exitSafe(1);
                	}        
		        
		}
		//calculate the wait times for queue 1 and 2
		//dispatch the ready process
		//check queue 0
		//keep track of the current queue.	
		int size = sizeOfQueue(priorityZero);
		struct Node* n = deQueue(priorityZero);
		//Look for message

		//if msg received, run algorithm
                if (msgrcv(msgid, &message, sizeof(message), 2, IPC_NOWAIT) != -1){
			if((strcmp(message.mesg_text,"Done")) == 0){
				int status = 0;
				//printf("message received waiting on pid");
				fprintf(fp,"OSS: Message received, process %lld is terminating and releasing its resources\n",message.pid);
				fflush(fp);
				waitpid(message.pid, &status, 0);
				
				int PCBindex = message.bitIndex;
				//release resources
				r_semop(semid, semwait, 1);
			        shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
				shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
				int i = 0;
				//add resources back to available.
				for(i=0; i<20; i++){
					shmrd[i].available += shmpcb[PCBindex].taken[i];
				}
				fprintf(fp, "OSS: Current Allocation = <");
                                int y = 0;
                                for(y=0;y<20;y++){
 	                               fprintf(fp,"%d,",shmrd[y].available);
                                }
                                fprintf(fp, ">\n");

				//clear pcb
     			
               			shmpcb[PCBindex].CPU = 0;
       			        shmpcb[PCBindex].simPID = 0;
       			        int a;
       			        for(a=0; a < 20; a++){
       			                shmpcb[PCBindex].claims[a] = 0;
        		                shmpcb[PCBindex].taken[a] = 0;
        		        }

        				
				shmdt(shmpcb);
				shmdt(shmrd);
				r_semop(semid, semsignal, 1);
				//reset bit map
				resetBit(bitMap,PCBindex);

			}
			else{
				//not terminateing
				if((strcmp(message.mesg_text,"Request")) == 0){
					//run algorithm
						
				}
				else if ((strcmp(message.mesg_text,"Nothing")) == 0){
					printf("Releasing nothing\n");
					fprintf(fp,"OSS: Process %lld Chose to release but did not have any resources.\n",message.pid);
					fflush(fp);
					fflush(stdout);
					//message.mesg_type = 2;
                                       // msgsnd(msgid, &message, sizeof(message), 0);
				}
				else{
					//it is releasing resources
					r_semop(semid, semwait, 1);
                                        shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
                                        shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
					shmrd[message.resourceClass].available += message.quantity;
					shmpcb[message.bitIndex].taken[message.resourceClass] -= message.quantity;
					fprintf(fp,"OSS: Process %lld Releasing %d of resource %d\n",message.pid,message.quantity,message.resourceClass);
                                        printf("OSS: Process %lld Releasing %d of resource %d\n",message.pid,message.quantity,message.resourceClass);
					fprintf(fp, "OSS: Current Allocation = <");
                                        int y = 0;
                                        for(y=0;y<20;y++){
                                                fprintf(fp,"%d,",shmrd[y].available);
                                        }
                                        fprintf(fp, ">\n");
					fflush(stdout);
					fflush(fp);
					pid_t child = shmpcb[message.bitIndex].simPID;
					shmdt(shmpcb);
                                        shmdt(shmrd);
                                        r_semop(semid, semsignal, 1);

					//send message back
                                        //message.mesg_type = 2;
					//message.pid = child;
                                        //msgsnd(msgid, &message, (sizeof(struct mesg_buffer)), 0);
				}


			}
				
		}else{
			errno = 0;
		//	printf("No message Received increment clock and loop back\n");
		}
			//send verdict back to user
			//and check again?
		//sprintf(message.mesg_text,"WOOP");
		//message.mesg_type = 3;
                //msgsnd(msgid, &message, sizeof(message), IPC_NOWAIT);
		r_semop(semid,semwait,1);
                shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
                shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
		shmpid = (struct Dispatch*) shmat(shmidPID, (void*)0,0);
		int c,d;
		bool flag = false;
		for(c=0;c<18;c++){
			flag = false;
			for(d=0;d<20;d++){
				if(shmpcb[c].needs[d] > 0){
					fprintf(fp,"OSS: %lld Requests:\n < ",shmpcb[c].simPID);
					int y = 0;
                                        for(y=0;y<20;y++){
	                                        fprintf(fp,"%d,",shmpcb[c].needs[y]);
                                        }
					fprintf(fp,">\n");
					fprintf(fp,"OSS: Currently Available:\n < ");
					for(y=0;y<20;y++){
                                                fprintf(fp,"%d,",shmrd[y].available);
                                        }
					fprintf(fp,">\n\n");
					if(isSafe(c)){
						flag = true;
						int e = 0;
						for(e=0;e<20;e++){
							shmpcb[c].taken[e] += shmpcb[c].needs[e];
                                                	shmrd[e].available -= shmpcb[c].needs[e];
                                                	shmpcb[c].needs[e] = 0;

						}
					}
					else{
						//fprintf(fp,"OSS: Request for resources from process %lld has been Denied\n",shmpcb[c].simPID);
		                                printf("OSS: Request for resources from process %lld has been Denied.\n",shmpcb[c].simPID);
		
                                		//fprintf(fp,"OSS: New Allocation after request:\n< ");
                                		//int y = 0;
                                		//for(y=0;y<20;y++){
                                		//       fprintf(fp,"%d,",shmrd[y].available);
						//}
					}
					break;
				}
			}
			
			if(flag){
                                fprintf(fp,"OSS: Request for resources from process %lld has been granted.\n",shmpcb[c].simPID);
                                printf("OSS: Request for resources from process %lld has been granted.\n",shmpcb[c].simPID);

 				fprintf(fp,"OSS: New Allocation after request:\n\n< ");
                                int y = 0;
                                for(y=0;y<20;y++){
                                       fprintf(fp,"%d,",shmrd[y].available);
                                }
                                fprintf(fp, ">\n\n");
                                fflush(fp);
				fflush(stdout);
                                pid_t child = shmpcb[c].simPID;

				shmpid->pid = child;
				
			}
		}
		shmdt(shmpcb);
		shmdt(shmrd);
		shmdt(shmpid);
		r_semop(semid,semsignal,1);
		//if not then just increment clock and loop back
		incrementClock();	
	}

	return 0;
}
bool req_lt_avail ( const int * req, const int * avail, const int pnum, const int num_res )
{
	int i = 0;
	for ( ; i < num_res; i++ )
		if ( req[pnum*num_res+i] > avail[i] )
			break;
	return ( i == num_res );
}


bool isSafe(int index){
	//shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
	//shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
	//shmpid = (struct Dispatch*) shmat(shmidPID, (void*)0,0);
	short max[20][18], need[20][18], alloc[20][18], available[20],request[20];//completed[20], safe[18];
	//18 processes 0-17, and 20 resources 0-19
	//
	//init the 2d arrays with double for loop
	int x,y;
	for(x=0;x<18;x++){
		for(y=0;y<20;y++){
			need[x][y] = shmpcb[x].claims[y] - shmpcb[x].taken[y];
			alloc[x][y] = shmpcb[x].taken[y];
			max[x][y] = shmpcb[x].claims[y];
		}
	}
	
	//get available and request.
	for(x=0; x<20;x++){
		request[x] = shmpcb[index].needs[x];
		available[x] = shmrd[x].available;
	}
	for(x=0; x<20; x++){
        	//check that it is not asking for more than it will ever need.
        	if(need[index][x] < request[x]){
        	        printf("Asked for more than initial max request\n");
        	        return false;
        	}
        	if(request[x] <= available[x]){
        	        available[x] -= request[x];
        	        alloc[index][x] += request[x];
        	        need[index][x] -= request[x];
        	}
        	else{
        	        return false;
        	}
	
	}

		
	int p = 18; int m = 20;
	bool finish[p];
	int i;
	//mark the processes as unfinished
	for(i = 0; i < p; finish[i++] = false );
	
	//get the safe sequence
	int safeSeq[p];
	
	int count = 0;
	while (count < p){
		bool found = false;
		int process;
		for(process=0;process < p; process++){
			if(finish[process] == false){
				int j;
				for(j = 0;j<m;j++){
					if(need[process][j] > available[j]){
						break;
					}
				}
				if(j == m){
					int k;
					for(k = 0; k< m; k++){
						//add the resources from this process 
						//back to available
						available[k] += alloc[process][k]; 
					}
					safeSeq[count++] = process;
					
					finish[process] = true;
					
					found = true;
				}
			}
		}
		if (found == false){
			return false;
		}
	}
	//system is safe
	return true;

}





/*
for(x=0; x<20; x++){
	check that it is not asking for more than it will ever need.
	if(need[index][x] < request[x]){
                printf("Asked for more than initial max request\n");
		return false;
        }
        if(request[x] <= available[x]){
                available[x] -= request[x];
                alloc[index][x] += request[x];
                need[index][x] -= request[x];
		return true;
        }
	else{
		return false;
	}
	
}
	//We have simulated the request now check to see if we are in a safe state
	










	int m=20,n=18;
	
	int work[m];
	bool finish[n];
	int i = 0;
	for(i = 0; i < m; work[i] = available[i++] );
	for(i = 0; i < n; finish[i++] = false );
	
	int p = 0;
	for ( ; p < n; p++){
		if(finish[p]){
			continue;
		}
		//simulate function here:
		bool flag;
		int i = 0;
		for(; i < m; i++){
			if(need[p][i] > available[i]){
				break;
			}
		}
		
		if(i == m){//req_lt_avail (need, work, p, m) ){
			finish[p] = true;
			for (i = 0; i< m; i++){
				work[i] += alloc[p*m+i];
			}
			p = -1;
		}
	}
	
	for(p=0; p<n; p++){
		if( !finish[p]){
			break;
		}
	}
	return (p != n);
}
*/
/*int n = 18; //number of proceses
int m = 20; // number of resources
int i=0,j=0,k=0;
y=0;
int f[n], ans[n], ind = 0;
for(y=0;y<n;y++){
	f[n] = 0;
}
y=0;
for (k = 0; k < 18; k++) { 
        for (i = 0; i < n; i++) { 
	    //inited to zero
            if (f[i] == 0) { 
  
                int flag = 0; 
                for (j = 0; j < m; j++) { 
                    if (need[i][j] > available[j]){ 
                        flag = 1; 
                         break; 
                    } 
                } 
  
                if (flag == 0) { 
                    ans[ind++] = i; 
                    for (y = 0; y < m; y++) 
                        available[y] += alloc[i][y]; 
                    f[i] = 1; 
                } 
            } 
        } 
    } 
  
    printf("Following is the SAFE Sequence\n"); 
    for (i = 0; i < n - 1; i++) 
        printf(" P%d ->", ans[i]); 
    printf(" P%d", ans[n - 1]);

    return true; 
}
*/


//Increment the clock after each iteration of the loop.
//by 1.xx seconds with xx nanoseconds between 1-1000
void incrementClock(){
		if (r_semop(semid, semwait, 1) == -1){
                        perror("Error: oss: Failed to lock semid. ");
                        exitSafe(1);
                }
                else{
			/*make sure we convert the nanoseconds to seconds after it gets large enough*/
			shmclock = (struct Clock*) shmat(shmid, (void*)0,0);
                        unsigned int increment = (unsigned int)rand() % 1000 + 5000000;
                        if(shmclock->nano >= 1000000000 - increment){
                                shmclock->second += (unsigned int)(shmclock->nano / (unsigned int)1000000000);
                                shmclock->nano = (shmclock->nano % (unsigned int)1000000000) + increment;
                        }
                        else{
                                shmclock->nano += increment;
                        }
			if(processes < 100){
				passedTime = (shmclock->second) + (double)(shmclock->nano / 1000000000);
                        }
                        shmdt(shmclock);
                        if (r_semop(semid, semsignal, 1) == -1) {
                                perror("Error: oss: failed to signal Semaphore. ");
                                exitSafe(1);
                        }
               }
}
