#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Function to find an open spot in the bitMap
//function takes in a bitMap of unsigned char[3]
//and the last index used. initialize the lastpid to -1
//then this function returns -1 if none was found and
//returns the first open spot if not.
//also limits 0-17 in the bitMap.
int openBit(unsigned char * bitMap, int lastPid){
        lastPid++;
        if(lastPid > 17){
          lastPid = 0;
        }
        int tempPid = lastPid;
            while(1){
                if((bitMap[lastPid/8] & (1 << (lastPid % 8 ))) == 0){
                        /*bit has not been set yet*/
                        return lastPid;
                }

                lastPid++;
                if(lastPid > 17){
                        lastPid = 0;
                }
                if( lastPid == tempPid){
                        return -1;
                }

            }
}


//Just flips the bit to 1  in the given bitmap
//at the given lastPid.
void setBit(unsigned char * bitMap, int lastPid){
        bitMap[lastPid/8] |= (1<<(lastPid%8));
}

//Just flips the bit to 0 in the given bitmap and given LastPid
void resetBit(unsigned char * bitMap, int finPid){
        bitMap[finPid/8] &= ~(1 << (finPid % 8));

}
