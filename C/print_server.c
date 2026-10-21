#include "job_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NUM_PRINTERS 4
#define DEFAULT_JOBS 50

typedef struct {
    int id;
    job_queue_t *queue;
    long jobs_completed;
} worker_arg_t;

static void simulate_printing(void)
{
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000L}; /* 1 ms */
    (void)nanosleep(&ts, NULL);
}

static void *printer_worker(void *arg)
{
    worker_arg_t *w = (worker_arg_t *)arg;
    int job = 0;

    while (queue_pop(w->queue, &job)) {
        simulate_printing();
        w->jobs_completed++;
        printf("PROCESSED job=%d printer=%d\n", job, w->id);
        fflush(stdout);
    }

    return NULL;
}

static long parse_positive_long(const char *s, const char *name)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value <= 0) {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return value;
}

static int run_core_server(long num_jobs)
{
    job_queue_t queue;
    pthread_t printers[NUM_PRINTERS];
    worker_arg_t workers[NUM_PRINTERS] = {0};

    if (queue_init(&queue) != 0) {
        fprintf(stderr, "queue_init failed\n");
        return EXIT_FAILURE;
    }

    int started = 0;
    for (int i = 0; i < NUM_PRINTERS; i++) {
        workers[i].id = i;
        workers[i].queue = &queue;
        workers[i].jobs_completed = 0;
        int rc = pthread_create(&printers[i], NULL, printer_worker, &workers[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            queue_shutdown(&queue);
            for (int j = 0; j < started; j++) {
                pthread_join(printers[j], NULL);
            }
            queue_destroy(&queue);
            return EXIT_FAILURE;
        }
        started++;
    }

    for (long job = 1; job <= num_jobs; job++) {
        if (queue_push(&queue, (int)job) != 0) {
            fprintf(stderr, "Failed to submit job %ld\n", job);
            queue_shutdown(&queue);
            for (int i = 0; i < NUM_PRINTERS; i++) {
                pthread_join(printers[i], NULL);
            }
            queue_destroy(&queue);
            return EXIT_FAILURE;
        }
        printf("SUBMITTED job=%ld\n", job);
    }

    queue_shutdown(&queue);

    for (int i = 0; i < NUM_PRINTERS; i++) {
        int rc = pthread_join(printers[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(rc));
        }
    }

    long total = 0;
    for (int i = 0; i < NUM_PRINTERS; i++) {
        total += workers[i].jobs_completed;
    }

    printf("TOTAL_COMPLETED %ld\n", total);

    queue_destroy(&queue);
    return (total == num_jobs) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 * BONUS (+10): Multiple concurrent client/producers.
 *
 * Required command line when implemented:
 *     ./print_server --bonus <producers> <jobs-per-producer>
 *
 * Each producer must run as its own pthread and submit exactly the requested
 * number of jobs into the SAME bounded queue while the printer threads consume
 * concurrently. Use unique positive job IDs. For every accepted job print:
 *
 *     SUBMITTED producer=<p> job=<id>
 *
 * The printer output remains:
 *
 *     PROCESSED job=<id> printer=<id>
 *
 * Main must join all producer threads BEFORE calling queue_shutdown(). Then it
 * must join the printer threads and print:
 *
 *     TOTAL_COMPLETED <n>
 *
 * The autograder awards bonus credit only when every submitted job is processed
 * exactly once and all producer counts are correct.
 */
typedef struct {
    int id;
    long jobs;
    job_queue_t *queue;
    long accepted;
} producer_arg_t;

static void *producer_worker(void *arg)
{
    producer_arg_t *p = (producer_arg_t *)arg;

    for (long k = 0; k < p->jobs; k++) {
        long id = (long)p->id * p->jobs + k + 1; /* globally unique, positive */
        if (queue_push(p->queue, (int)id) != 0) {
            break;
        }
        p->accepted++;
        printf("SUBMITTED producer=%d job=%ld\n", p->id, id);
        fflush(stdout);
    }
    return NULL;
}

static int run_bonus_server(long producers, long jobs_per_producer)
{
    job_queue_t queue;
    pthread_t printers[NUM_PRINTERS];
    worker_arg_t workers[NUM_PRINTERS] = {0};
    pthread_t *prod_threads = calloc((size_t)producers, sizeof(*prod_threads));
    producer_arg_t *prod_args = calloc((size_t)producers, sizeof(*prod_args));
    int status = EXIT_SUCCESS;
    int printers_started = 0;
    long prod_started = 0;

    if (prod_threads == NULL || prod_args == NULL) {
        fprintf(stderr, "out of memory\n");
        free(prod_threads);
        free(prod_args);
        return EXIT_FAILURE;
    }
    if (queue_init(&queue) != 0) {
        fprintf(stderr, "queue_init failed\n");
        free(prod_threads);
        free(prod_args);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < NUM_PRINTERS; i++) {
        workers[i].id = i;
        workers[i].queue = &queue;
        int rc = pthread_create(&printers[i], NULL, printer_worker, &workers[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            status = EXIT_FAILURE;
            break;
        }
        printers_started++;
    }

    if (status == EXIT_SUCCESS) {
        for (long p = 0; p < producers; p++) {
            prod_args[p].id = (int)p;
            prod_args[p].jobs = jobs_per_producer;
            prod_args[p].queue = &queue;
            int rc = pthread_create(&prod_threads[p], NULL, producer_worker, &prod_args[p]);
            if (rc != 0) {
                fprintf(stderr, "pthread_create: %s\n", strerror(rc));
                status = EXIT_FAILURE;
                break;
            }
            prod_started++;
        }
    }

    /* 1. Join ALL producers first; only then is it safe to shut down. */
    for (long p = 0; p < prod_started; p++) {
        pthread_join(prod_threads[p], NULL);
    }
    /* 2. No more jobs can arrive: wake and drain consumers. */
    queue_shutdown(&queue);
    /* 3. Join all printers. */
    for (int i = 0; i < printers_started; i++) {
        pthread_join(printers[i], NULL);
    }

    long total = 0;
    for (int i = 0; i < NUM_PRINTERS; i++) {
        total += workers[i].jobs_completed;
    }
    printf("TOTAL_COMPLETED %ld\n", total);

    if (status == EXIT_SUCCESS && total != producers * jobs_per_producer) {
        status = EXIT_FAILURE;
    }

    queue_destroy(&queue);
    free(prod_threads);
    free(prod_args);
    return status;
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "--bonus") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s --bonus <producers> <jobs-per-producer>\n", argv[0]);
            return EXIT_FAILURE;
        }
        long producers = parse_positive_long(argv[2], "producer count");
        long jobs = parse_positive_long(argv[3], "jobs per producer");
        return run_bonus_server(producers, jobs);
    }

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [number-of-jobs]\n", argv[0]);
        return EXIT_FAILURE;
    }

    long num_jobs = (argc == 2) ? parse_positive_long(argv[1], "job count") : DEFAULT_JOBS;
    return run_core_server(num_jobs);
}
