#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>

#define THREADS_COUNT 16
#define JOBS 100

typedef struct
{
    pthread_mutex_t *mutex;
    pthread_cond_t *cond_main_to_threads;
    pthread_cond_t *cond_thread_to_main;
    int input_data;
    int *result;
    bool work_taken;
    bool work_finished;

} args_t;

void *thread_work(void *data)
{
    pthread_t tid = pthread_self();
    args_t *args = (args_t *)data;

    while(1)
    {   
        pthread_mutex_lock(args->mutex);
        while (args->work_taken == true && !args->work_finished)
        {
            printf("ja ide spac\n");
            pthread_cond_wait(args->cond_main_to_threads, args->mutex); 
            printf("obudzono\n");
        }
        
        if (args->work_finished)
        {
            pthread_mutex_unlock(args->mutex);
            break;
        }

        int index = args->input_data;
        args->work_taken = true;
    
        pthread_cond_signal(args->cond_thread_to_main);
        pthread_mutex_unlock(args->mutex);

        sleep(1); //work
        args->result[index] = index * 100 + 1;
        printf("[%lu] x=%d\n", tid, index);
    }
    printf("zakonczono\n");
    return NULL;
}

int main()
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond_main_to_threads = PTHREAD_COND_INITIALIZER;
    pthread_cond_t cond_thread_to_main = PTHREAD_COND_INITIALIZER;

    int *result = (int *)malloc(JOBS * sizeof(int));

    args_t args = {&mutex, &cond_main_to_threads, &cond_thread_to_main, -1, result,
        true, false};

    pthread_t tid[THREADS_COUNT];

    for (int i = 0; i < THREADS_COUNT; i++)
        if (pthread_create(&tid[i], NULL, &thread_work, (void *)&args) != 0)
            perror("pthread_create error"), exit(1);
            
    // sleep(3);

    for (int i = 0; i < JOBS; i++)
    {
        pthread_mutex_lock(&mutex);
        while (args.work_taken == false)
            pthread_cond_wait(&cond_thread_to_main, &mutex);

        args.input_data = i;
        args.work_taken = false;
        pthread_cond_signal(&cond_main_to_threads);
        pthread_mutex_unlock(&mutex);
    }

    // printf("zaraz im puszcze\n");
    // sleep(3);
    
    pthread_mutex_lock(&mutex);
    while (args.work_taken == false)
         pthread_cond_wait(&cond_thread_to_main, &mutex);

    args.work_finished = true;

    pthread_cond_broadcast(&cond_main_to_threads);
    pthread_mutex_unlock(&mutex);


    for (int i = 0; i < THREADS_COUNT; i++)
        pthread_join(tid[i], NULL);

    printf("All threads joined.\n\n");

    for (int i = 0; i < JOBS; i++)
        printf("result[%i]: %d\n", i, args.result[i]);

    free(result);
}
