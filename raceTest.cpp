// raceTest.cpp
//
// pThreads and Synchronization
//
// Alex Viznytsya
// 12/05/2017

#include <iostream>
#include <string>
#include <cstdlib>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>


//#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
//
//#else
//union semun {
//    int val;
//    struct semid_ds *buf;
//    unsigned short *array;
//    struct seminfo *__buf;
//};
//#endif


typedef struct workerStruct {
    int nBuffers;
    int workerID;
    double sleepTime;
    int semID;
    int mutexID;
    int *buffers;
    int nReadErrors;
} workerStruct;

void printUssage() {
    std::cout << "Program ussage:" << std::endl;
    std::cout << "./raceTest nBuffers nWorkers [ sleepMin sleepMax ]";
    std::cout << "[ randSeed ] [ -lock | -nolock ]" << std::endl;
}

void printMyInfo() {
    std::cout << "pThreads and Synchronization" << std::endl;
    std::cout << "Alex Viznytsya" << std::endl;
    std::cout << "12/05/2017" << std::endl << std::endl;
}

void cleanScreenMutexSemaphore(int screenMutexID) {
    if(semctl(screenMutexID, 1, IPC_RMID) < 0) {
        perror("Error: cleanSharedResources() -> semctr(screenMutex IPC_RMID)");
        exit(-5);
    }
}

void cleanBufferSemaphore(int semID, int nBuffers) {
    if(semID != -1 && semctl(semID, nBuffers, IPC_RMID) < 0) {
        perror("Error: cleanSharedResources() -> semctr(sem IPC_RMID)");
        exit(-5);
    }
}

void cleanSharedResources(int screenMutexID, int semID, int nBuffers) {
    cleanScreenMutexSemaphore(screenMutexID);
    cleanBufferSemaphore(semID, nBuffers);
}

void *worker(void *workerData) {
    workerStruct *tWorker = (workerStruct*)workerData;
    sembuf lockScreen = {0, -1, 0};
    sembuf unlockScreen {0, 1, 0};
    int workerID = tWorker->workerID;
    int nBuffers = tWorker->nBuffers;
    int screenMutexID = tWorker->mutexID;
    int semID =tWorker->semID;
    int curPos = tWorker->workerID;
    int readWriteCounter = 1;
    sembuf lockBuffer;
    sembuf unlockBuffer;

    while(true) {
        lockBuffer = {(unsigned short)curPos, -1, 0};
        unlockBuffer = {(unsigned short)curPos, 1, 0};
        
        // Lock buffer if lock argument is set:
        if(semID != -1 && semop(semID, &lockBuffer, 1) < 0) {
            perror("Error: worker() -> semop(buffer, -1)");
            cleanSharedResources(screenMutexID, semID, nBuffers);
            exit(-6);
        }
        int read = curPos;
        int initialReadValue = tWorker->buffers[read];
        
        usleep(tWorker->sleepTime * 1000000);
        
        int finalReadValue = tWorker->buffers[read];
        
        // Start wriring:
        if((readWriteCounter %= 3) == 0) {
            tWorker->buffers[read] = initialReadValue + (1 << (workerID - 1));
            readWriteCounter = 1;
            if(curPos == 0) {
                if(semID != -1 && semop(semID, &unlockBuffer, 1) < 0) {
                    perror("Error: worker() -> semop(buffer, 1)");
                    cleanSharedResources(screenMutexID, semID, nBuffers);
                    exit(-6);
                }
                break;
            }
        } else {
            // Start reading:
            if(initialReadValue != finalReadValue) {
                
                // Lock screen mutex:
                if(semop(screenMutexID, &lockScreen, 1) < 0) {
                    perror("Error: worker() -> semop(screenMutex, -1)");
                    cleanSharedResources(screenMutexID, semID, nBuffers);
                    exit(-6);
                }
                std::cout << "    Worker #" << workerID << " reported change from ";
                std::cout << initialReadValue << " to " << finalReadValue;
                std::cout << " in buffer #"<< curPos << ".";
                std::cout << std::endl;
                std::cout << "    Changed bits: ";
                
                // Check for changed bits:
                for(int i = 0; i < 32; i++) {
                    int maskedInitialValue = (initialReadValue >> i) & 1;
                    int maskedFinalValue = (finalReadValue >> i) & 1;
                    if(maskedInitialValue > maskedFinalValue) {
                        std::cout << -i << " ";
                        tWorker->nReadErrors += 1;
                    } else if(maskedInitialValue < maskedFinalValue){
                        std::cout << i << " ";
                        tWorker->nReadErrors += 1;
                    } else {
                        continue;
                    }
                }
                std::cout << std::endl << std::endl;
                
                // Unlock screen mutex:
                if(semop(screenMutexID, &unlockScreen, 1) < 0) {
                    perror("Error: worker() -> semop(screenMutex, 1)");
                    cleanSharedResources(screenMutexID, semID, nBuffers);
                    exit(-6);
                }
            }
            readWriteCounter += 1;
        }
        
        // Unlock buffer if lock argument is set:
        if(semID != -1 && semop(semID, &unlockBuffer, 1) < 0) {
            perror("Error: worker() -> semop(buffer, 1)");
            cleanSharedResources(screenMutexID, semID, nBuffers);
            exit(-6);
        }
        
        // Go to next buffer element:
        curPos = (read + workerID) % nBuffers;
    }
    
    pthread_exit(NULL);
}

int main(int argc, const char * argv[]) {

    // Programs settings:
    int nBuffers = 0;
    int nWorkers = 0;
    double sleepMin = 1.0;
    double sleepMax = 5.0;
    unsigned int randSeed = 0;
    bool lock = false;
    
    printMyInfo();
    
    // Check if nBuffer and nWorkers are valid:
    if(argc >= 3) {
        int tnBuffers = atoi(argv[1]);
        int tnWorkers = atoi(argv[2]);
        if(tnBuffers < 3 || tnBuffers > 31 ||(tnBuffers % 2) == 0 ||
          (tnBuffers % 3) == 0 || (tnBuffers % 5) == 0) {
            std::cerr << "Error: nBuffer arguments must between 2 and 32, ";
            std::cerr << "and not evenly divisible by 2, 3 or 5." << std::endl;
            exit(-1);
        } else if(tnWorkers < 0 || tnWorkers < tnBuffers){
            nBuffers = tnBuffers;
            nWorkers = tnWorkers;
        } else {
            std::cerr << "Error: nWorkers arguments must be positive and les";
            std::cerr << "then nBuffer integer." << std::endl;
            exit(-1);
        }
    } else {
        std::cerr << "Error: Insuficient minimun command line arguments." << std::endl;
        printUssage();
        exit(-1);
    }
    
    // Check other possible command line arguments:
    if(argc > 3) {
        for(int i = 3; i < argc; i++) {
            std::string arg = argv[i];
            if(arg == "-lock") {
                lock = true;
                continue;
            }
            if(arg == "-nolock") {
                lock = false;
                continue;
            }
            if(arg.find(".") != std::string::npos){
                if(argc > (i + 1)) {
                    std::string arg2 = argv[i + 1];
                    if(arg2.find(".") != std::string::npos) {
                        double tsleepMin = atof(argv[i]);
                        double tsleepMax = atof(argv[i + 1]);
                        if(tsleepMin < 0.0 || tsleepMax < tsleepMin) {
                            std::cerr << "Error: incorrect [ sleepMin sleepMax ]";
                            std::cerr << "command line arguments." << std::endl;
                            exit(-1);
                        } else {
                            sleepMin = tsleepMin;
                            sleepMax = tsleepMax;
                            i += 1;
                            continue;
                        }
                    } else {
                        std::cerr << "Error: [ sleepMin sleepMax] command line ";
                        std::cerr << "arguments must be in pair." << std::endl;
                        exit(-1);
                    }
                } else {
                    std::cerr << "Error: [ sleepMin sleepMax] command line ";
                    std::cerr << "arguments must be in pair." << std::endl;
                    exit(-1);
                }
            }
            int trandSeed = atoi(argv[i]);
            if(trandSeed != 0) {
                randSeed = (unsigned int)trandSeed;
            }
        }
    }
    
    // Initialize random seed:
    if(randSeed == 0) {
        srand((unsigned int)time(NULL));
    } else {
        srand(randSeed);
    }
    
    //Create and initialize buffer array:
    int buffers[nBuffers];
    for(int i = 0; i < nBuffers; i++) {
        buffers[i] = 0;
    }
    
    // Create screen lock mutex:
    int screenMutexID = -1;
    unsigned short mutexSetupArray[] = {1};
    semun screenMutexUnion;
    screenMutexUnion.array = mutexSetupArray;
    if((screenMutexID = semget(IPC_PRIVATE, 1 , 0600 | IPC_CREAT)) < 0) {
        perror("Error: main() -> semget(screenMutex)");
        exit(-3);
    }
    if(semctl(screenMutexID, 0, SETALL, screenMutexUnion) < 0) {
        perror("Error: main() -> semctl(screenMutex SETALL)");
        cleanScreenMutexSemaphore(screenMutexID);
        exit(-4);
    }
    
    // Create buffer semaphores:
    int semID = -1;
    if(lock == true) {
        unsigned short buffersSetupArray[nBuffers];
        for(int i = 0; i < nBuffers; i++) {
            buffersSetupArray[i] = 1;
        }
        semun buffersSemUnion;
        buffersSemUnion.array = buffersSetupArray;
        if((semID = semget(IPC_PRIVATE, nBuffers , 0600 | IPC_CREAT)) < 0) {
            perror("Error: main() -> semget(buffers)");
            exit(-3);
        }
        if(semctl(semID, 0, SETALL, buffersSemUnion) < 0) {
            perror("Error: main() -> semctl(buffers SETALL)");
            cleanSharedResources(screenMutexID, semID, nBuffers);
            exit(-4);
        }
    }
    
    
    // Create and initialize worker struct array:
    workerStruct workers[nWorkers];
    for(int i = 0; i < nWorkers; i++) {
        workers[i].nBuffers = nBuffers;
        workers[i].workerID = i + 1;
        workers[i].sleepTime = sleepMin + (sleepMax - sleepMin) * rand() / RAND_MAX;
        workers[i].semID = semID;
        workers[i].mutexID = screenMutexID;
        workers[i].buffers = buffers;
        workers[i].nReadErrors = 0;
    }
   
    std::cout << "Starting simulation for " << nWorkers;
    std::cout << " workers(threads) accessing " << nBuffers << " buffers, ";
    if(lock == true){
        std::cout << "with locking.";
    }else {
        std::cout << "without locking.";
    }
    std::cout << std::endl << std::endl;
    std::cout << "Reported read errors by workers(threads):" << std::endl;
    
    // Create threds:
    pthread_t tid[nWorkers];
    for(int i = 0; i < nWorkers; i++) {
        if(pthread_create(&tid[i], NULL, worker, &workers[i]) != 0) {
            std::cerr << "Error: main() -> pthread_create()" << std::endl;
            exit(-1);
        }
    }
    
    // Wait fot all worker to complete their jobs:
    for(int i = 0; i < nWorkers; i++) {
        pthread_join(tid[i], NULL);
    }
    
    // Print work stats:
    int finalValue = (1 << nWorkers) - 1;
    int totalReadErrors = 0;
    int totalWriteErrors = 0;
    
    for(int i = 0; i < nWorkers; i++) {
        totalReadErrors += workers[i].nReadErrors;
    }
    
    if(totalReadErrors == 0) {
        std::cout << "    There are no reported read errors." << std::endl;
    }
    std::cout << std::endl;
    std::cout << "Reported write errors by worker's manager(main program): " << std::endl;
    for(int i = 0; i < nBuffers; i++) {
        if(buffers[i] != finalValue) {
            std::cout << "    Error detected in buffer #" << i << "." << std::endl;
            std::cout << "    Expected value: " << finalValue;
            std::cout << ", current value: " << buffers[i] << ". " << std::endl;
            std::cout << "    Changed bits: ";
            //Check bits:
            int bitChanges = buffers[i] ^ finalValue;
            for(int j = 0; j < nWorkers; j++) {
                if(((bitChanges >> j) & 1) == 1) {
                    std::cout << j << " ";
                    totalWriteErrors += 1;
                }
            }
            std::cout << std::endl << std::endl;
        }
    }
    
    if(totalWriteErrors == 0) {
        std::cout << "    There are no reported write errors." << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "During this simulation " << totalReadErrors << " read and ";
    std::cout << totalWriteErrors << " write errors were detected.";
    std::cout << std::endl << std::endl;
    
    cleanSharedResources(screenMutexID, semID, nBuffers);
    
    std::cout << "Thank you for using this program. Bye-bye." << std::endl;
    std::cout << "Exiting program ..." << std::endl;
    
    return 0;
}
