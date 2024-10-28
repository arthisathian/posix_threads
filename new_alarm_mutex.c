/*
 * alarm_mutex.c
 *
 * This is an enhancement to the alarm_thread.c program, which
 * created an "alarm thread" for each alarm command. This new
 * version uses a single alarm thread, which reads the next
 * entry in a list. The main thread places new requests onto the
 * list, in order of absolute expiration time. The list is
 * protected by a mutex, and the alarm thread sleeps for at
 * least 1 second, each iteration, to ensure that the main
 * thread can lock the mutex to add new work to the list.
 */
#include <pthread.h>
#include <time.h>
#include "errors.h"
#include <stdio.h>      // Added libraries (Arthi S)
#include <string.h>
#include <stdlib.h>

/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    time_t              time;   /* seconds from EPOCH */
    char                message[128];   // Updated to allow 128 characters per message (Arthi S)
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
alarm_t *alarm_list = NULL;

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm;
    int sleep_time;
    time_t now;
    int status;

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits.
     */
    while (1) {
        status = pthread_mutex_lock (&alarm_mutex);
        if (status != 0)
            err_abort (status, "Lock mutex");
        alarm = alarm_list;

        /*
         * If the alarm list is empty, wait for one second. This
         * allows the main thread to run, and read another
         * command. If the list is not empty, remove the first
         * item. Compute the number of seconds to wait -- if the
         * result is less than 0 (the time has passed), then set
         * the sleep_time to 0.
         */
        if (alarm == NULL)
            sleep_time = 1;
        else {
            alarm_list = alarm->link;
            now = time (NULL);
            if (alarm->time <= now)
                sleep_time = 0;
            else
                sleep_time = alarm->time - now;
#ifdef DEBUG
            printf ("[waiting: %d(%d)\"%s\"]\n", alarm->time,
                sleep_time, alarm->message);
#endif
            }

        /*
         * Unlock the mutex before waiting, so that the main
         * thread can lock it to insert a new alarm request. If
         * the sleep_time is 0, then call sched_yield, giving
         * the main thread a chance to run if it has been
         * readied by user input, without delaying the message
         * if there's no input.
         */
        status = pthread_mutex_unlock (&alarm_mutex);
        if (status != 0)
            err_abort (status, "Unlock mutex");
        if (sleep_time > 0)
            sleep (sleep_time);
        else
            sched_yield ();

        /*
         * If a timer expired, print the message and free the
         * structure.
         */
        if (alarm != NULL) {
            printf ("(%d) %s\n", alarm->seconds, alarm->message);
            free (alarm);
        }
    }
}

int main (int argc, char *argv[]) {
    int status;
    char line[256];     // Increased the buffer for command parsing (Arthi S)
    alarm_t *alarm, **last, *next;
    pthread_t thread;

    status = pthread_create (&thread, NULL, alarm_thread, NULL);
    if (status != 0) {err_abort (status, "Create alarm thread");}

    while (1) {
        printf ("alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;
        
        /* Truncate message if it exceeds 128 characters (Arthi S)
         * Ensures no overflow in error messages, truncates if necessary, 
         * and warns user if message was truncated.
         */
        if (strlen(line) > 128){
            line[127] = '\0';
            fprintf(stderr, "WARNING: Message trunated to 128 characters.\n");
        }

        // Variables used in command parsing (Arthi S)
        char command[16];
        int alarm_id;
        char type[3];
        int alarm_duration;
        char message[128];

        /* Parse and validate command input (Arthi S)
         * Extracts the command, alarm ID, type, time, and message by parsing the command.
         * Ensures the proper formatting and validity of commands.
         */
        if (sscanf (line, "%15s(%d): %2s %d %127[^\n]", command, &alarm_id, type, &alarm_duration, message) == 5) {
            if (strcmp(command, "Start Alarm") == 0) {
                /* Start_Alarm command handling
                * Allocates memory for new alarm, sets time & message,
                * and inserts it into the sorted list
                */
                alarm = (alarm_t *)malloc(sizeof(alarm_t));
                if (alarm == NULL) {errno_abort("Allocate alarm");}
                
                alarm -> seconds = alarm_duration;
                strncpy(alarm -> message, message, sizeof(alarm -> message) - 1);
                alarm -> time = time(NULL) + alarm -> seconds;

                /* Locks mutex for thread safe insertion
                * Lock ensures that only one thread can modify the alarm_list
                * at any given time (prevents race conditions during insertion)
                */
                status = pthread_mutex_lock(&alarm_mutex);
                if (status != 0) {err_abort(status, "Lock mutex");}

                /*
                * Insert the new alarm into the list of alarms, sorted by expiration time.
                */
                last = &alarm_list;
                next = *last;

                while (next != NULL){
                    if (next -> time >= alarm -> time){
                        alarm -> link = next;
                        *last = alarm;
                        break;
                    }
                    last = &next -> link;
                    next = next -> link;
                }
                /*
                * If we reached the end of the list, insert the new
                * alarm there. ("next" is NULL, and "last" points
                * to the link field of the last item, or to the
                * list header).
                */
                if (next == NULL){
                    *last = alarm;
                    alarm -> link = NULL;
                }

                // Unlock mutex post-insert so other threads can access/modify alarm_list
                status = pthread_mutex_unlock(&alarm_mutex);
                if (status != 0) {err_abort(status, "Unlock mutex");}

                printf("Alarm(%d) set at %ld with message: %s\n", alarm_id, time(NULL), alarm -> message);

            } else if (strcmp(command, "Change_Alarm") == 0) {
                /* Change_Alarm command handling
                * Locks the mutex, finds + updates alarm using specified ID,
                * and unlocks after modification
                */
                status = pthread_mutex_lock (&alarm_mutex);
                if (status != 0) {err_abort (status, "Lock mutex");}
                
                alarm = alarm_list;

                while (alarm != NULL){
                    if (alarm -> seconds == alarm_id){
                        alarm -> seconds = alarm_duration;
                        strncpy(alarm -> message, message, sizeof(alarm -> message) - 1);
                        printf("Alarm (%d) changed to %d seconds with message: %s\n", alarm_id, alarm_duration, message);
                        break;
                    }
                    alarm = alarm -> link;
                }
                
                if (alarm == NULL){
                    fprintf(stderr, "ERROR: Alarm ID %d not found for modification.\n", alarm_id);
                }
                status = pthread_mutex_unlock(&alarm_mutex);
                if (status != 0) {err_abort(status, "Unlock mutex");}

            } else if (strcmp(command, "Cancel_Alarm") == 0) {
                /* Cancel_Alarm command handling
                * Locks mutex, finds specified alarm using ID,
                * removes from list if found, then unlocks mutex
                */
                status = pthread_mutex_lock(&alarm_mutex);
                if(status != 0) {err_abort(status, "Lock mutex");}

                last = &alarm_list;
                alarm = *last;

                while (alarm != NULL){
                    if (alarm -> seconds == alarm_id){
                        *last = alarm -> link;
                        free(alarm);
                        printf("Alarm(%d) cancelled.\n", alarm_id);
                        break;
                    }
                    last = &alarm -> link;
                    alarm = *last;
                }

                if (alarm == NULL){
                    fprintf(stderr, "ERROR: Alarm ID %d not found for cancellation.\n", alarm_id);
                }
                status = pthread_mutex_unlock(&alarm_mutex);
                if (status != 0) {err_abort(status, "Unlock mutex");}
            } else if (strcmp(line, "View_Alarms") == 0) {
                /* View_Alarm command handling
                * Locks mutex, iterates through alarm_list to display all active alarms
                * (ensures thread-safe access), then unlocks mutex
                */
                status = pthread_mutex_lock(&alarm_mutex);
                if(status != 0) {err_abort(status, "Lock mutex");}
                printf("Viewing all alarms:\n");
                alarm = alarm_list;
                int i = 1;

                while(alarm != NULL){
                    printf("Alarm %d: %d seconds - %s\n", i++, alarm -> seconds, alarm -> message);
                    alarm = alarm -> link;
                }
                status = pthread_mutex_unlock(&alarm_mutex);
                if (status != 0) {err_abort(status, "Unlock mutex");}
            } else{
            fprintf(stderr, "ERROR: Invalid command %s\n", command);
            }
#ifdef DEBUG
            printf ("[list: ");
            for (next = alarm_list; next != NULL; next = next->link)
                printf ("%d(%d)[\"%s\"] ", next->time,
                    next->time - time (NULL), next->message);
            printf ("]\n");
#endif
            status = pthread_mutex_unlock (&alarm_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");
        }
    }
}
