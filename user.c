//Name: Jackson Hoenig
//Class: CMP-SCI 4760
//Description: This is the child process of the parent program oss.c
//please check that one for details on the whole project. this program is not
//designed to be called on its own.
//This program takes in 5 arguments, the shared memory id, semaphore id, the message queue id, the dispatch shmid, and the PCB shmid.
//after it gets that data the program waits for the dispatch to show its pid, then waits on the semaphore to prevent race condition.
//then calculates what it will do with it's quantum and when it is done returns the control to the OSS with a message.
//the process will only end if the oss kills it or a 0 is generated in the purpose variable.
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/sem.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/msg.h>
#include <stdbool.h>
#include "customStructs.h"
#include "semaphoreFunc.h"
#define PERMS (IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
#define NUM_RES 20

int main(int argc, char  **argv) {
	srand(getpid());
	struct sembuf semsignal[1];
	struct sembuf semwait[1];
	//int error;	
	//get IPC data.	
	int shmid = atoi(argv[1]);	
	int semid = atoi(argv[2]);
	int msgid = atoi(argv[3]);
	int shmidPID = atoi(argv[4]);
	int shmidpcb = atoi(argv[5]);
	int bitIndex = atoi(argv[6]);
	int shmidrd = atoi(argv[7]);	
        if(semid == -1){
                perror("Error: user: Failed to create a private semaphore");
                exit(1);
        }	
	//initialize purpose
	int purpose = 0;
	bool isFinished = false;
	setsembuf(semwait, 0, -1, 0);
	setsembuf(semsignal, 0, 1, 0);
	
	//Read/write the PCB and get max claims.
	int numClasses = rand() % NUM_RES + 1;
	int x = 0;
	int y = 0;
	r_semop(semid, semwait, 1);
	struct RD *shmrd = (struct RD*) shmat(shmidrd,(void*)0,0);
	struct PCB *shmpcb = (struct PCB*) shmat(shmidpcb, (void*)0,0);
	bool done = false;
	while(x < numClasses){
		y = rand() % NUM_RES + 1;
		if(shmpcb[bitIndex].claims[y] == 0){
			shmpcb[bitIndex].claims[y] = rand() % (shmrd[y].total + 1);
			x++;
			
		}
	}
	shmdt(shmpcb);
	shmdt(shmrd);
	r_semop(semid, semsignal, 1);
	bool finished = false;
    while(1){	

	
	//generate random time to execute event
	int executeMS = rand() % 250 + 1;
	struct Clock execute;
	r_semop(semid, semwait, 1);
        struct Clock *shmclock = (struct Clock*) shmat(shmid,(void*)0,0);
	long long exeNS = shmclock->nano + (executeMS * 1000000);
	if(exeNS > 1000000000){
		execute.second = (exeNS / 1000000000) + shmclock->second;
		execute.nano = exeNS % 1000000000;
	}
	else{
		execute.second = shmclock->second;
		execute.nano = exeNS;
	}
	shmdt(shmclock);
	r_semop(semid, semsignal, 1);
	// 0 - 250 ms
		

			
	//printf("Waiting for time %d:%d\n",execute.second,execute.nano);
	//fflush(stdout);
	//bool isWaiting = true;
	//while(isWaiting){
	r_semop(semid, semwait, 1);
        shmclock = (struct Clock*) shmat(shmid,(void*)0,0);
        struct PCB *shmpcb = (struct PCB*) shmat(shmidpcb, (void*)0,0);
	finished = true;
        for(x=0; x<NUM_RES; x++){
 	       if(shmpcb[bitIndex].claims[x] != shmpcb[bitIndex].taken[x]){
	               finished = false;
               }
        }

	if((execute.second == shmclock->second && execute.nano > shmclock->nano) || execute.second > shmclock->second){
	//isWaiting = false;
		//add cpu time
		struct PCB *shmpcb = (struct PCB*) shmat(shmidpcb, (void*)0,0);
		shmpcb[bitIndex].CPU += executeMS;
		if(shmpcb[bitIndex].CPU > 1000){
			isFinished = true;
		}
	}
	shmdt(shmpcb);	
	shmdt(shmclock);
	r_semop(semid, semsignal, 1);
	//}
	
	

	//get random number
	if(isFinished){
		purpose =  rand() % 3;
	}
	else{
		purpose =  rand() % 2 + 1;
	}
	//purpose = 1;	
	if (purpose == 0 || finished){
		//terminate
		message.pid = getpid();
                message.mesg_type = 2;
                /*send back what time you calculated to end on.*/
                sprintf(message.mesg_text, "Done");
		message.bitIndex = bitIndex;
                msgsnd(msgid, &message, sizeof(message), 0);
                exit(0);
	}	
	else if(purpose == 1){
		//Request for more resources
        	int x = 0; 
	       	r_semop(semid,semwait,1);
                struct RD *shmrd = (struct RD*) shmat(shmidrd,(void*)0,0);
                struct PCB *shmpcb = (struct PCB*) shmat(shmidpcb, (void*)0,0);
		bool isRequesting = false;
		for(x=0; x<NUM_RES; x++){
                        if(shmpcb[bitIndex].claims[x] > 0 && shmpcb[bitIndex].taken[x] < shmpcb[bitIndex].claims[x]){
				shmpcb[bitIndex].needs[x] =  rand() % (shmpcb[bitIndex].claims[x] - shmpcb[bitIndex].taken[x] + 1);
                        	if(shmpcb[bitIndex].needs[x] > 0){
	                       	        isRequesting = true;

				}
			}
                }
		/*finished = true;
		for(x=0; x<NUM_RES; x++){
			if(shmpcb[bitIndex].claims[x] != shmpcb[bitIndex].taken[x]){
				finished = false;
			}
		}*/
		shmdt(shmpcb);
                shmdt(shmrd);
                r_semop(semid,semsignal,1);
	        //msgsnd(msgid, &message, sizeof(message), 0);
	        if(isRequesting){
			struct Dispatch* dispatch = (struct Dispatch*) shmat(shmidPID, (void*)0,0);
			while(1){
				if(dispatch->pid == getpid()){
					break;	
				}
			}
			
			//r_semop(semid,semwait,1);
			dispatch->pid = getppid();
			shmdt(dispatch);
			//r_semop(semid,semsignal,1);
		//message.mesg_type = getppid();
		//msgsnd(msgid, &message, sizeof(message), 0);
		//msgrcv(msgid, &message, sizeof(message), getpid(), 0);
		}
	}
	else if(purpose == 2){
		//Release some Resources
		r_semop(semid,semwait,1);
	        struct RD *shmrd = (struct RD*) shmat(shmidrd,(void*)0,0);
                struct PCB *shmpcb = (struct PCB*) shmat(shmidpcb, (void*)0,0);
		sprintf(message.mesg_text, "Release");
		int x = 0;
		bool isReleasing = false;
		for(x=0; x<NUM_RES; x++){
			if(shmpcb[bitIndex].taken[x] > 0){
				int releaseAmount = rand() % shmpcb[bitIndex].taken[x] + 1;
				shmpcb[bitIndex].release[x] = releaseAmount;
				if(releaseAmount > 0){
					isReleasing = true;
				}
			}
		}
		shmdt(shmrd);
                shmdt(shmpcb);
                r_semop(semid,semsignal,1);

		if(!isReleasing){
	                sprintf(message.mesg_text, "Nothing");
			
			//message.quantity = 1;
		}
		else {
			//message.quantity = rand() % shmpcb[bitIndex].taken[x] + 1;
			message.pid = getpid();
			message.mesg_type = 2;
	        	message.bitIndex = bitIndex;
	        	msgsnd(msgid, &message, sizeof(message), 0);
			/*struct Dispatch* dispatch = (struct Dispatch*) shmat(shmidPID, (void*)0,0);
                        while(1){
                                if(dispatch->pid == getpid()){
                                        break;
                                }
                        }

                        r_semop(semid,semwait,1);
                        dispatch->pid = 0;
                        shmdt(dispatch);
                        r_semop(semid,semsignal,1);
			*/
			
			msgrcv(msgid, &message, sizeof(message), getpid(), 0);
		}
	}
	//continue back.	
	}
	return 0;
}	
