//this function is used to create the wait and signal functions.
//it populates the sembuf struct with the parameters given
//i found this code in the textbook on chapter 15
void setsembuf(struct sembuf *s, int num, int op, int flg) {
        s->sem_num = (short)num;
        s->sem_op = (short)op;
        s->sem_flg = (short)flg;
        return;
}
//this function runs a semaphore operation "sops" on the
//semaphore with the id of semid. it will loop continuously
//through the operation until it comes back as true.
int r_semop(int semid, struct sembuf *sops, int nsops) {
        while(semop(semid, sops, nsops) == -1){
                if(errno != EINTR){
                        return -1;
                }
        }
        return 0;
}
//a simple function to destroy the semaphore data.
int removesem(int semid) {
        return semctl(semid, 0, IPC_RMID);
}

//a function to initialize the semaphone to a number in semvalue
//it is used later to set the critical resource to number to 1
//since we only have one shared memory clock.
int initElement(int semid, int semnum, int semvalue) {
        union semun {
                int val;
                struct semid_ds *buf;
                unsigned short *array;
        } arg;
        arg.val = semvalue;
        return semctl(semid, semnum, SETVAL, arg);
}

