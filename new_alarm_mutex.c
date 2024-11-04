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
#include <unistd.h>     //Added libraries (Hien L)

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
    char                type[3];
    int                 alarm_ID;
    int                 is_assigned;
} alarm_t;

typedef struct display_tag {
    pthread_t   threadid;
    char        type[3];
    int         assigned_alarm_count;
    alarm_t     *assigned_alarm[2];
} display_t;


pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;
alarm_t *alarm_list = NULL;

display_t *display_threads[10];
int display_thread_count = 0;


/*
* Display Threads
*/
void *display_thread (void *arg) {
   display_t *display_thread = (display_t*) arg;
   int status;

   while(1){
        status = pthread_mutex_lock (&display_mutex);
        if (status != 0)
            err_abort (status, "Lock mutex");
        
        
        int active_alarm = 0;

        for(int i = 0; i < 2; i++){
            alarm_t *alarm = display_thread->assigned_alarm[i];
            
            if(alarm != NULL){
                time_t now = time(NULL);
                if(now >= alarm->time){
                    printf("Alarm(%d) Expired; Display Thread (%lu) Stopped Printing Alarm Message at %ld: %s %d %s\n", alarm->alarm_ID, display_thread->threadid, now, alarm->type, alarm->seconds, alarm->message);
                    display_thread->assigned_alarm[i] = NULL;   //Clear the expired alarm
                }else {
                    printf("Alarm(%d) Message PERIODICALLY PRINTED BY Display Thread (%lu) at %ld: %s %d %s\n", alarm->alarm_ID, display_thread->threadid, now, alarm->type, alarm->seconds, alarm->message);
                    active_alarm++;
                }
            }
        }

        if(active_alarm == 0) {
            printf("Display Thread Terminated (%lu) at %ld\n", display_thread->threadid, time(NULL));
            status = pthread_mutex_unlock (&display_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");
            free(display_thread);
            display_thread_count--;
            pthread_exit(NULL);
        }
        status = pthread_mutex_unlock (&display_mutex);
        if (status != 0)
             err_abort (status, "Unlock mutex");
        sleep(5);      //Set 5 later
   }
}

/*
* Create a display thread function
*/
display_t *create_display_thread(char *type) {
    if(display_thread_count >= 10) return NULL;   //Limits the number of threads

    display_t *new_thread = (display_t*) malloc(sizeof(display_t));
    if (new_thread == NULL) {
        fprintf(stderr, "Error: Could not allocate memory for new display thread.\n");
        return NULL;
    }

    //Create thread base on alarm type
    strcpy(new_thread->type, type);
    new_thread->assigned_alarm_count = 0;

    //Create the thread
    int status = pthread_create(&new_thread->threadid, NULL, display_thread, new_thread);
    if(status != 0){
        free(new_thread);
        err_abort(status, "Create display Thread");
    }

    //Increase the count of display thread;
    display_thread_count++;

    //Add the thread to the array of threads where empty
    for(int i = 0; i < display_thread_count; i++){
        if(display_threads[i] == NULL){
            display_threads[i] = new_thread;
        }
    }
    return new_thread;
}

/*
* Assign Alarm to the Right Thread
*/
void assign_alarm_to_display_thread(alarm_t *new_alarm) {
    int thread_found = 0;
    int type_found = 0;
    display_t *target_thread = NULL;
    int status;
    alarm_t *temp_alarm = new_alarm;

    // Lock the mutex to safely modify shared data structures
    status = pthread_mutex_lock(&display_mutex);
    if (status != 0) {
        err_abort(status, "Lock mutex");
    }

    //Find the target thread for the alarm based on their type and the display capacity
    for(int i = 0; i < display_thread_count; i++){
        if(strcmp(display_threads[i]->type, temp_alarm->type) == 0){
            type_found = 1;
            if(display_threads[i]->assigned_alarm_count < 2){
                thread_found = 1;
                target_thread = display_threads[i];
                break;
            }
        }
    }

    //Two cases for creating new thread
    if(!thread_found && !type_found){
        target_thread = create_display_thread(temp_alarm->type);
        printf("First New Display Thread (%lu) Created at %ld: %s %d %s\n", target_thread->threadid, time(NULL), temp_alarm->type, temp_alarm->seconds, temp_alarm->message);
    }else if(!thread_found && type_found){
        target_thread = create_display_thread(temp_alarm->type);
        printf("Additional New Display Thread (%lu) Created at %ld: %s %d %s\n", target_thread->threadid, time(NULL), temp_alarm->type, temp_alarm->seconds, temp_alarm->message);
    }

    //Assign the alarm to the target thread
    if(target_thread != NULL){
        target_thread->assigned_alarm[target_thread->assigned_alarm_count++] = temp_alarm;
        printf("Alarm (%d) Assigned to Display Thread (%lu) at %ld: %s %d %s\n", temp_alarm->alarm_ID, target_thread->threadid, time(NULL), temp_alarm->type, temp_alarm->seconds, temp_alarm->message); 
    } else {
        fprintf(stderr, "Error: Could not create new display thread.\n");
    }

    // Unlock the mutex after modifying shared data structures
    status = pthread_mutex_unlock(&display_mutex);
    if (status != 0) {
        err_abort(status, "Unlock mutex");
    }
}

void cancel_alarm_in_display_thread (alarm_t *target_alarm){
    int status;

    // Lock the mutex to safely modify shared data structures
    status = pthread_mutex_lock(&display_mutex);
    if (status != 0) {
        err_abort(status, "Lock mutex");
    }

    //Find the thread that has the alarm
    for(int i = 0; i < display_thread_count; i++){
        display_t *temp_display = display_threads[i];
        for(int k = 0; k < temp_display->assigned_alarm_count; k++){

            //Check for match alarm
            if(target_alarm->alarm_ID == temp_display->assigned_alarm[k]->alarm_ID){
                //Remove this alarm and print the message
                temp_display->assigned_alarm[k] = NULL;
                temp_display->assigned_alarm_count--;

                printf("Alarm(%d) Cancelled; Display Thread (%lu) Stopped Printing Alarm Message at %ld: %s %d %s\n", target_alarm->alarm_ID, temp_display->threadid, time(NULL), target_alarm->type, target_alarm->seconds, target_alarm->message);
            }
        }
    }
    // Unlock the mutex after modifying shared data structures
    status = pthread_mutex_unlock(&display_mutex);
    if (status != 0) {
        err_abort(status, "Unlock mutex");
    }
}

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm, *prev, *current;
    alarm_t *expired_alarms[50];
    int expired_count = 0;
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
        
        //alarm = alarm_list;
        
        prev = NULL;
        current = alarm_list;
        now = time(NULL);
        expired_count = 0;

        //Traverse through the alarm list
        while(current != NULL){

            //Find the expired alarm
            if(current->time <= now){
                //Expired alarm - print expiration message and remove the list
                printf("Alarm(%d): Alarm Expired at %ld: Alarm Removed From Alarm List\n", alarm -> seconds, time(NULL));

                //Remove expired alarm from the list
                if(prev == NULL){
                    alarm_list = current->link;
                }else{
                    prev->link = current->link;
                }
                
                if(expired_count < 50){
                    expired_alarms[expired_count++] = current;
                }

            } else if(!current->is_assigned){
                //Assign only active, unassigned alarm to the display thread
                assign_alarm_to_display_thread(current);
                current->is_assigned = 1;
                prev = current;
                current = current->link;
            } else {
                //Skip already assigned active alarms
                prev = current;
                current = current->link;
            }
            //Exit the thread after finish traversing the list
            if (current == NULL) {
                pthread_exit(NULL);
            }
        }
        // Handle reassignment if alarm type change
        for(int i = 0; i < display_thread_count; i++){
            display_t *display = display_threads[i];
            for(int j = 0; j < 2; j++){
                alarm_t *assign_alarm = display->assigned_alarm[j];

                if(assign_alarm && strcmp(assign_alarm->type, display->type) != 0){
                    //If alarm type has changed, remove it from the current thread
                    printf("Alarm (%d) Changed Type; Display Thread (%lu) Stopped Printing Alarm Message at %ld: %s %d %s\n", assign_alarm->alarm_ID, display->threadid, time(NULL), assign_alarm->type, assign_alarm->seconds, assign_alarm->message);
                    display->assigned_alarm[j] = NULL;
                    display->assigned_alarm_count--;

                    //Reassign Alarm as if it were new
                    assign_alarm_to_display_thread(assign_alarm);
                }
            }
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
        
        // Process expired alarms outside of the mutex lock
        for (int i = 0; i < expired_count; i++) {
            alarm_t *expired_alarm = expired_alarms[i];
            
            // Free the expired alarm memory here
            free(expired_alarm);
        }

        //Sleep briefly before re-checking the alarm list
        sleep(1);
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
            if (strcmp(command, "Start_Alarm") == 0) {
                /* Start_Alarm command handling
                * Allocates memory for new alarm, sets time & message,
                * and inserts it into the sorted list
                */
                alarm = (alarm_t *)malloc(sizeof(alarm_t));
                if (alarm == NULL) {errno_abort("Allocate alarm");}
                
                alarm -> seconds = alarm_duration;
                strncpy(alarm -> message, message, sizeof(alarm -> message) - 1);
                alarm -> message[127] = '\0';   // Ensures null termination
                alarm -> time = time(NULL) + alarm -> seconds;
                strncpy(alarm->type, type, sizeof(alarm->type) - 1);
                alarm->alarm_ID = alarm_id;
                alarm->is_assigned = 0;

                /* Locks mutex for thread safe insertion
                * Lock ensures that only one thread can modify the alarm_list
                * at any given time (prevents race conditions during insertion)
                */
                status = pthread_mutex_lock(&alarm_mutex);
                if (status != 0) {err_abort(status, "Lock mutex");}
                
                /*
                * Insert the new alarm into the list of alarms, sorted by alarm ID.
                */
                last = &alarm_list;
                next = *last;

                while (next != NULL){
                    if (next->alarm_ID >= alarm->alarm_ID){     ///Sorted by their IDs
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
                printf("Alarm(%d) Inserted by Main Thread (%lu) Into Alarm List at %ld: %s %d %s\n", alarm_id, thread, time(NULL), type, alarm_duration, message);
                //pthread_self(), time(NULL), type, alarm_duration, time(NULL), alarm -> message);

            } else if (strcmp(command, "Change_Alarm") == 0) {
                /* Change_Alarm command handling
                * Locks the mutex, finds + updates alarm using specified ID,
                * and unlocks after modification
                */
                status = pthread_mutex_lock (&alarm_mutex);
                if (status != 0) {err_abort (status, "Lock mutex");}
                
                alarm = alarm_list;

                while (alarm != NULL){
                    if (alarm->alarm_ID == alarm_id){
                        alarm -> seconds = alarm_duration;
                        strncpy(alarm -> message, message, sizeof(alarm -> message) - 1);
                        printf("Alarm(%d) Changed at %ld: %s %d %s\n", alarm_id, time(NULL), type, alarm_duration, message);
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
                    if (alarm->alarm_ID == alarm_id){
                        *last = alarm -> link;
                        free(alarm);
                        printf("Alarm(%d) Cancelled at %ld: %s %d %s\n", alarm_id, time(NULL), type, alarm_duration, message);
                        cancel_alarm_in_display_thread(alarm);
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
                printf("View Alarms at %ld:\n", time(NULL));
                // alarm = alarm_list;
                // int i = 1;

                // while(alarm != NULL){
                //     printf("%d. Display Thread %lu Assigned:\n", i, pthread_self());
                //     printf("    %da. Alarm(%d): %s %d %s\n", i, alarm -> seconds, type, alarm_duration, alarm -> message);
                //     alarm = alarm -> link;
                // }

                for(int i = 0; i < display_thread_count; i++){
                    display_t *temp_display = display_threads[i];
                    printf("%d. Display Thread %lu Assigned:\n", i, temp_display->threadid);

                    for(int k = 0; k < temp_display->assigned_alarm_count; k++){
                        alarm_t *temp_alarm = temp_display->assigned_alarm[k];
                        printf("\t%d%c. Alarm(%d): %s %d %s\n", i, k + 97, alarm_id, type, alarm_duration, message);
                    }
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
        sleep(2);
    }
}
