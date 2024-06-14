/*
 *  examples/thread.c
 * 
 *  Copyleft (C) 2015  Sun Dro (a.k.a. kala13x)
 *
 * Example file of useing threads.
 */


#include "xstd.h"
#include <sys/thread.h>

void* my_thread(void *arg)
{
    int *n = (int*)arg;
    printf("Argument is: %d\n", *n);
    return NULL;
}

int myTask(void *pCtx)
{
    printf("My task function with interval\n");
    return 0;
}

int main() 
{
    xtask_t task;
    XTask_Start(&task, myTask, NULL, 10000);

    xthread_t thread[2];
    int arg = 5;
    int arg1 = 5;

    /* First way (undetached) */
    XThread_Init(&thread[0]);
    thread[0].pArgument = &arg;
    thread[0].functionCb = my_thread;
    XThread_Run(&thread[0]);
    XThread_Join(&thread[0]);
    
    /* Second way */
    XThread_Create(&thread[1], my_thread, &arg1, 1);

    /* Stop working */
    XTask_Stop(&task, 10000);

    return 0;
}
