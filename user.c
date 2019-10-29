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

//Global shm pointers
struct PCB *shmpcb;
struct Dispatch *dispatch;

//void sigHandler(int sig){
//	exit(1);
//}


int main(int argc, char  **argv) {
	srand(getpid());
	//signal(SIGKILL,sigHandler);
	struct sembuf semsignal[1];
	struct sembuf semwait[1];
	int error;	
	//get IPC data.	
	int shmid = atoi(argv[1]);	
	int semid = atoi(argv[2]);
	int msgid = atoi(argv[3]);
	int shmidPID = atoi(argv[4]);
	int shmidpcb = atoi(argv[5]);	
        if(semid == -1){
                perror("Error: user: Failed to create a private semaphore");
                exit(1);
        }	
	//initialize purpose
	int purpose = 1;
	unsigned int quantum;
        int bitIndex;
	bool isFinished = false;
	setsembuf(semwait, 0, -1, 0);
	setsembuf(semsignal, 0, 1, 0);
	//printf("Before the while loop\n");
    while(1){	
	 dispatch = (struct Dispatch*) shmat(shmidPID,(void*)0,0);
	pid_t myPid = getpid();
	while(1){
			
        	if(dispatch->pid == myPid){
        		break;	
		}
	}

	//wait for semaphore and reset shmpid
	r_semop(semid, semwait, 1);
	dispatch->pid = 0;
	quantum = dispatch->quantum;
        bitIndex = dispatch->index;
	dispatch->index = -1;
	dispatch->quantum = 0;
	shmdt(dispatch);
	r_semop(semid, semsignal, 1);
	
	//get random number
	if(isFinished){
		purpose = rand() % 4;
	}
	else{
		purpose = rand() % 3 + 1;
	}
	//purpose = 0;
	if (purpose == 0){
		//terminate
		message.pid = getppid();
                message.mesg_type = 2;
                /*send back what time you calculated to end on.*/
                sprintf(message.mesg_text, "Done");
                msgsnd(msgid, &message, sizeof(message), 0);
		//shmdt(shmpcb);
		shmdt(dispatch);
                exit(0);
	}	
	else if(purpose == 2){
		//Wait for event
		int r = rand() % 5;
		int s = rand() % 1000;
		if (r_semop(semid, semwait, 1) == -1){
        	        perror("Error: oss: Failed to lock semid. ");
        	        exit(1);
	        }

		//r_semop(semid, semwait, 1);
		struct Clock *shmclock = (struct Clock*) shmat(shmid,(void*)0,0);
                s += shmclock->nano;
                r += shmclock->second;
                if( s >= 1000000000){
                	r += s / 1000000000;
        	        s = s % 1000000000;
	        }

                shmdt(shmclock);
		r_semop(semid, semsignal, 1);	
		bool eventDone = false;
		while(!eventDone){
			if (r_semop(semid, semwait, 1) == -1){
		                perror("Error: user: Failed to lock semid. ");
        		        exit(1);
        		}
	                struct Clock *shmclock = (struct Clock*) shmat(shmid,(void*)0,0);

			//r_semop(semid, semwait, 1);
			if((shmclock->second == r && shmclock->nano > s) || shmclock->second > r){
				eventDone = true;
			}
			shmdt(shmclock);
			if ( r_semop(semid, semsignal, 1) == -1) {
                        perror("Error: user: Failed to clean up semaphore");
                        return 1;
	                }

			//r_semop(semid, semsignal, 1);			
		}
	}
	//use percentage of quantum.
	else if(purpose == 3){
		int part = rand() % 99 + 1;
		double percentage = (double)part/100;
		quantum = (unsigned int)((double)quantum*percentage);
		sprintf(message.mesg_text,"not using its entire time quantum");
	}
	unsigned long long int duration; 	
	//unsigned long long int currentTime;	
	//unsigned long long  burst;
	unsigned int ns = quantum;
	unsigned int sec;
	//unsigned long long int startTime;
	//struct Clock start;	
	if (r_semop(semid, semwait, 1) == -1){
                 perror("Error: oss: Failed to lock semid. ");
                 exit(1);
        }
	else{
		//inside critical section
	        struct Clock *shmclock = (struct Clock*) shmat(shmid,(void*)0,0);
		ns += shmclock->nano;
		sec = shmclock->second;
	//	start.nano = shmclock->nano;
	//	start.second = shmclock->second;
	 //	startTime = (1000000000 * shmclock->second) + shmclock->nano; 
	        shmdt(shmclock);
		//printf("got the clock ns= %d sec= %d\n",ns,sec);	
		shmpcb = (struct PCB*) shmat(shmidpcb, (void*)0,0);
	//	burst = shmpcb[bitIndex].burst;
		shmdt(shmpcb);	
		//exit the Critical Section
		if ( r_semop(semid, semsignal, 1) == -1) {
			perror("User: Failed to clean up semaphore");
			return 1;
		}
	
	}
	
	
	
	//Make sure we convert the nanoseconds to seconds if big enough
	if( ns >= 1000000000){
		sec += ns / 1000000000;
		ns = ns % 1000000000;
	}
	fflush(stdout);
	bool timeElapsed = false;
	while(!timeElapsed){
		if ((error = r_semop(semid, semwait, 1)) == -1){
			perror("Error: user: Child failed to lock semid. ");
	                return 1;
	        }
	        else {
		
			//inside CS
	                struct Clock *shmclock = (struct Clock*) shmat(shmid,(void*)0,0);
			if((shmclock->nano >= ns && shmclock->second == sec) || shmclock->second > sec){
				timeElapsed = true;
				//currentTime = (1000000000 * shmclock->second) + shmclock->nano;
				dispatch = (struct Dispatch*) shmat(shmidPID,(void*)0,0);
			        shmpcb = (struct PCB*) shmat(shmidpcb,(void*)0,0);
 
				//struct PCB *shmpcb = (struct PCB*) shmat(shmidpcb, (void*)0,0);
				if (shmpcb == (struct PCB*)(-1)){

					perror("USER: PCB SHMAT");
					exit(1);

				}
				shmpcb[bitIndex].duration = quantum;
				shmpcb[bitIndex].CPU += shmpcb[bitIndex].duration;
				//shmpcb[bitIndex].CPU += quantum;
				shmpcb[bitIndex].system = ((shmclock->second - shmpcb[bitIndex].startTime.second) * 1000000000);
				shmpcb[bitIndex].system += shmclock->nano - shmpcb[bitIndex].startTime.nano;
				if(shmpcb[bitIndex].CPU >= 50000000){
					isFinished = true;
				}
				duration = shmpcb[bitIndex].duration;
				shmdt(shmpcb);	
				
			}
			shmdt(shmclock);

			//exit CS
	                if ((error = r_semop(semid, semsignal, 1)) == -1) {
	                        //printf("Failed to clean up");
       		                return 1;
                	}

		}
	}
	
	//purpose = 0;
		fflush(stdout);	
		//message.mesg_text = "Not Done";
		message.pid = 0;//getppid();
		message.mesg_type = 2;
		if(purpose != 3){
			sprintf(message.mesg_text, "total time this dispatch was %lld nanoseconds",duration);
		}
		msgsnd(msgid, &message, sizeof(message), 0);
			
    }
	return 0;
}	
