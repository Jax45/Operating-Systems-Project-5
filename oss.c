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
#define NUM_RES 20
//global var.
int processes = 0;
bool verbose = false;
//prototype for exit safe
void exitSafe(int);
bool isSafe(int,FILE*);
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
	//long double througput = passedTime / processes;
	//printf("Process has ended due to timeout, Processses finished: %d, time passed: %Lf\n",processes,passedTime);
	//printf("OSS: it takes about %Lf seconds to generate and finish one process \n",througput);
	//printf("OSS: throughput = %Lf\n",processes / passedTime);
	//fflush(stdout);
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

void printTable(FILE *fp);
void incrementClock();
//Global for increment clock
struct sembuf semwait[1];
struct sembuf semsignal[1];


int main(int argc, char **argv){
	//const int ALPHA = 1;
	//const int BETA = 1;
	int opt;
	//get the options from the user.
	while((opt = getopt (argc, argv, "ht:v:")) != -1){
		switch(opt){
			case 'h':
				printf("Usage: ./oss [-t [timeout in seconds]]\n");
				exit(1);
			case 't':
				timeout = atoi(optarg);
				break;
			case 'v':
				verbose = true;
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
		for(i=0; i < NUM_RES; i++){
			shmpcb[x].claims[i] = 0;
                	shmpcb[x].taken[i] = 0;
			shmpcb[x].needs[i] = 0;
			shmpcb[x].release[i] = 0;
		}
	}
        shmdt(shmpcb);
	//setup the shared memory for the Resource Descriptor
	key_t rdkey = ftok("./oss", 'g');
	size_t RDSize = sizeof(struct RD) * NUM_RES;
	shmidRD = shmget(rdkey,RDSize,0666|IPC_CREAT);
	if (shmidRD == -1 || errno){
		perror("Error: oss: Failed to get shared memory");
		exitSafe(1);
	}
	shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
	for(x = 0; x < NUM_RES; x++){
		if((rand() % 100) <= 20){
			//it is sharable
			shmrd[x].sharable = true;
		}
		else{
                        shmrd[x].sharable = false;

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

	shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
	fprintf(fp,"Total in RD: < ");
	int abc = 0;
	for(abc = 0; abc < NUM_RES; abc++){
		fprintf(fp,"%d,",shmrd[abc].total);
	}
	fprintf(fp,">\n\n");
	shmdt(shmrd);

	//for limit purposes.
	int lines = 0;
	processes = 0;
	int grantedRequests = 0;
	const int maxTimeBetweenNewProcsNS = 10;
	const int maxTimeBetweenNewProcsSecs = 2;
	shmpid = (struct Dispatch*) shmat(shmidPID,(void*)0,0);
	struct Clock launchTime;
	launchTime.second = 0;
	launchTime.nano = 0;
	int lastPid = -1;
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
				

		}
		//check if we need to launch the processes
		
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
                        		fprintf(fp,"OSS: Generating process %d with pid %lld at time %d:%d\n",processes,(long long)pid,shmclock->second,shmclock->nano);
					}
					fflush(fp);	
					printf("OSS: Generating process %d with pid %lld at time %d:%d\n",processes,(long long)pid,shmclock->second,shmclock->nano);
					processes++;
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
		 //Look for message

		//if msg received, run algorithm
                if (msgrcv(msgid, &message, sizeof(message), 2, IPC_NOWAIT) != -1){
			if((strcmp(message.mesg_text,"Done")) == 0){
				int status = 0;
				//printf("message received waiting on pid");
				fprintf(fp,"OSS: Message received, process %lld is terminating and releasing its resources\n",(long long)message.pid);
				fflush(fp);
				waitpid(message.pid, &status, 0);
				
				int PCBindex = message.bitIndex;
				//release resources
				r_semop(semid, semwait, 1);
			        shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
				shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
				int i = 0;
				//add resources back to available.
				for(i=0; i<NUM_RES; i++){
					if(!shmrd[i].sharable){
						shmrd[i].available += shmpcb[PCBindex].taken[i];
					}
				}
				fprintf(fp, "OSS: Current Allocation = <");
                                int y = 0;
                                for(y=0;y<NUM_RES;y++){
 	                               fprintf(fp,"%d,",shmrd[y].available);
                                }
                                fprintf(fp, ">\n");

				//clear pcb
     			
               			shmpcb[PCBindex].CPU = 0;
       			        shmpcb[PCBindex].simPID = 0;
       			        int a;
       			        for(a=0; a < NUM_RES; a++){
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
					//printf("Releasing nothing\n");
					fprintf(fp,"OSS: Process %lld Chose to release but did not have any resources.\n",(long long)message.pid);
					fflush(fp);
					//fflush(stdout);
					//message.mesg_type = 2;
                                       // msgsnd(msgid, &message, sizeof(message), 0);
				}
				else if ((strcmp(message.mesg_text,"Release")) == 0){
					//it is releasing resources
					r_semop(semid, semwait, 1);
                                        shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
                                        shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
					//shmrd[message.resourceClass].available += message.quantity;
					//shmpcb[message.bitIndex].taken[message.resourceClass] -= message.quantity;
					int i = 0;
					bool isReleasing = false;
					for(i=0;i<NUM_RES;i++){
						if(shmpcb[message.bitIndex].release[i] > 0){
							isReleasing = true;
							break;
						}
					}
					fprintf(fp,"OSS: Process %lld Releasing some Resources:\n<",(long long)message.pid);
                                        printf("OSS: Process %lld Releasing some Resources\n",(long long)message.pid);
					
                                        
					i = 0;
                                        for(i=0;i<NUM_RES;i++){
						fprintf(fp,"%2d,",shmpcb[message.bitIndex].release[i]);
                                                
						shmpcb[message.bitIndex].taken[i] -= shmpcb[message.bitIndex].release[i];
                                                if(shmrd[i].sharable == false){
							shmrd[i].available += shmpcb[message.bitIndex].release[i];
                                                }
						shmpcb[message.bitIndex].release[i] = 0;
                                        }
					pid_t child = shmpcb[message.bitIndex].simPID;
					fprintf(fp,">\n");
					fprintf(fp,"Available Now:\n<");
					int y = 0;
                                        for(y=0;y<NUM_RES;y++){
                                                fprintf(fp,"%2d,",shmrd[y].available);
                                        }
                                        fprintf(fp, ">\n");
					fflush(stdout);
					fflush(fp);
					//pid_t child = shmpcb[message.bitIndex].simPID;
					shmdt(shmpcb);
                                        shmdt(shmrd);
			                //shmpid = (struct Dispatch*) shmat(shmidPID, (void*)0,0);
					//shmpid->pid = child;
					//shmdt(shmpid);
                                        r_semop(semid, semsignal, 1);
					message.mesg_type = child;
                                        msgsnd(msgid, &message, sizeof(message), 0);

					
				}


			}
				
		}else{
			errno = 0;
		//	printf("No message Received increment clock and loop back\n");
		}
		r_semop(semid,semwait,1);
                shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
                shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
		shmpid = (struct Dispatch*) shmat(shmidPID, (void*)0,0);
		int c,d;
		bool flag = false;
		for(c=0;c<18;c++){
			/*grantedRequests++;
	                if((grantedRequests % 20) == 0){
        	                printTable(fp);
        	        }
			*/
			flag = false;
			for(d=0;d<NUM_RES;d++){
				if(shmpcb[c].needs[d] > 0){
					grantedRequests++;
					if((grantedRequests % 20) == 0){
                		                printTable(fp);
		                        }

					if(isSafe(c,fp)){
						flag = true;
						int e = 0;
						for(e=0;e<NUM_RES;e++){

							if(shmrd[e].sharable == false){
								shmpcb[c].taken[e] += shmpcb[c].needs[e];
                                                		shmrd[e].available -= shmpcb[c].needs[e];
                                                	}
							else{
								//shmrd[e].available -= shmpcb[c].needs[e];
                                                                shmpcb[c].taken[e] += shmpcb[c].needs[e];
							}
							shmpcb[c].needs[e] = 0;
							
						}
					}
					break;
				}
			}
			
			if(flag){
                                fprintf(fp,"OSS: Request for resources from process %lld has been granted.\n",(long long)shmpcb[c].simPID);
                                printf("OSS: Request for resources from process %lld has been granted.\n",(long long)shmpcb[c].simPID);
				//grantedRequests++;
 				fprintf(fp,"OSS: New Allocation after request:\n\n< ");
                                int y = 0;
                                for(y=0;y<NUM_RES;y++){
                                       fprintf(fp,"%d,",shmrd[y].available);
                                }
                                fprintf(fp, ">\n\n");
                                fflush(fp);
				fflush(stdout);
                                pid_t child = shmpcb[c].simPID;
				
				//message.mesg_type = child;
			//	msgsnd(msgid, &message, sizeof(message),0);	
				
				shmpid->pid = child;
				while(1){
					if(shmpid->pid == getpid()){
						break;
					}
				}
				//msgrcv(msgid, &message, sizeof(message), getpid(), 0);
				//if((grantedRequests % 20) == 0){
				//	printTable(fp);
				//}	
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
void printTable(FILE *fp){
	int x = 0, y = 0;
	//print allocated
	fprintf(fp,"Currently Allocated: \n");
	for(x=0;x<18;x++){
		fprintf(fp,"P%6d: <",shmpcb[x].simPID);
		for(y=0;y<NUM_RES;y++){
			fprintf(fp,"%2d,",shmpcb[x].taken[y]);
		}
		fprintf(fp,">\n");
	}
	fprintf(fp,"\n");
	//print max claim
	fprintf(fp,"Max Claims: \n");
        for(x=0;x<18;x++){
                fprintf(fp,"P%6d: <",shmpcb[x].simPID);
                for(y=0;y<NUM_RES;y++){
                        fprintf(fp,"%2d,",shmpcb[x].claims[y]);
                }
                fprintf(fp,">\n");
        }
        fprintf(fp,"\n");
	//print needed
	fprintf(fp,"Needed: \n");
        for(x=0;x<18;x++){
                fprintf(fp,"P%6d: <",shmpcb[x].simPID);
                for(y=0;y<NUM_RES;y++){
                        fprintf(fp,"%2d,",shmpcb[x].claims[y] - shmpcb[x].taken[y]);
                }
                fprintf(fp,">\n");
        }
        fprintf(fp,"\n");

	//print available
	fprintf(fp,"Currently Available: \n");
	fprintf(fp,"         <");
        for(x=0;x<NUM_RES;x++){
              fprintf(fp,"%2d,",shmrd[x].available);
        }
        fprintf(fp,"\n");

}
bool req_lt_avail ( const int * req, const int * avail, const int pnum, const int num_res )
{
	int i = 0;
	for ( ; i < num_res; i++ )
		if ( req[pnum*num_res+i] > avail[i] )
			break;
	return ( i == num_res );
}


bool isSafe(int index, FILE *fp){
	//shmpcb = (struct PCB*) shmat(shmidPCB,(void*)0,0);
	//shmrd = (struct RD*) shmat(shmidRD,(void*)0,0);
	//shmpid = (struct Dispatch*) shmat(shmidPID, (void*)0,0);
	short max[18][NUM_RES], need[18][NUM_RES], alloc[18][NUM_RES], available[NUM_RES],request[NUM_RES];//completed[20], safe[18];
	//18 processes 0-17, and 20 resources 0-19
	//
	//init the 2d arrays with double for loop
	int x,y;
	for(x=0;x<18;x++){
		for(y=0;y<NUM_RES;y++){
			need[x][y] = shmpcb[x].claims[y] - shmpcb[x].taken[y];
			alloc[x][y] = shmpcb[x].taken[y];
			max[x][y] = shmpcb[x].claims[y];
		}
	}
	
	//get available and request.
	for(x=0; x<NUM_RES;x++){
		request[x] = shmpcb[index].needs[x];
		available[x] = shmrd[x].available;
	}
	//FILE* fp;
	//fp = fopen("test.txt" "w");
	for(x = 0; x < 5; x ++){
		if(x == 0){
			fprintf(fp,"\n%lld's Request vector:\n<",(long long)shmpcb[index].simPID);
			for(y=0;y<NUM_RES;y++){
	                        fprintf(fp,"%2d,",request[y]);//,shmpcb[index].needs[y]);//request[x]);
	                }
		}
		else if(x == 1){
                        fprintf(fp,"MaxClaim vector:\n<");
                        for(y=0;y<NUM_RES;y++){
                                fprintf(fp,"%2d,",max[index][y]);//,shmpcb[index].claims[y]);
                        }
                }
		else if(x == 2){
                        fprintf(fp,"taken/Allocated vector:\n<");
                        for(y=0;y<NUM_RES;y++){
                                fprintf(fp,"%2d,",alloc[index][y]);//,shmpcb[index].taken[y]);
                        }
                }

		else if(x == 3){
                        fprintf(fp,"MaxClaim-taken or NEED vector:\n<");
			for(y=0;y<NUM_RES;y++){
        	                fprintf(fp,"%2d,",need[index][y]);//,(shmpcb[x].claims[y] - shmpcb[x].taken[y]));
	                }
		}
		else{
                        fprintf(fp,"Available vector:\n<");
			for(y=0;y<NUM_RES;y++){
				fprintf(fp,"%2d,",available[y]);//,shmrd[y].available);
			}
		}
		fprintf(fp,">\n");
	}

	for(x=0; x<NUM_RES; x++){
        	//check that it is not asking for more than it will ever need.
        	if(need[index][x] < request[x]){
        	        printf("Asked for more than initial max request\n");
        	        return false;
        	}
        	if(request[x] <= available[x]){
			if(shmrd[x].sharable == false){
        	        	available[x] -= request[x];
        	        	alloc[index][x] += request[x];
        	        	need[index][x] -= request[x];
			}
        	}
        	else{
        	        return false;
        	}
	
	}

		
	int p = 18; int m = NUM_RES;
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
					if(need[process][j] > available[j] && !shmrd[j].sharable){
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
				//	count++;
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
	fprintf(fp,"Safe Sequence: <");
	for(i=0;i<p;i++){
		fprintf(fp, "P%d,", safeSeq[i]);
	}
	fprintf(fp,"\n");
	printTable(fp);
	return true;
	
}





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
			//printf("Current Time: %d:%d\n",shmclock->second,shmclock->nano);
			shmclock->second += 1;
                        shmdt(shmclock);
                        if (r_semop(semid, semsignal, 1) == -1) {
                                perror("Error: oss: failed to signal Semaphore. ");
                                exitSafe(1);
                        }
               }
}
