#include <iostream>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <unistd.h>

#include "macros.h"
using namespace std;
FILE *fp;

pthread_cond_t cv_empty;
pthread_cond_t feed_empty[NUM_READ_THREADS];

void populate_graph(vector<vector<int>> &graph_input)
{
    // reads the file and populates the graph
    // assuming the edges are non directed

    FILE *file = fopen(FILE_PATH, "r");
    if (file == NULL)
    {
        printf("Unable to open file %s\n", FILE_PATH);
        return;
    }
    char line[20];
    fgets(line, 20, file);
    cout << line << "\n";

    while (fgets(line, 20, file))
    {
        char *token = strtok(line, ",");
        int first = atoi(token);
        token = strtok(NULL, ",");
        int second = atoi(token);

        graph_input[first].push_back(second);
        graph_input[second].push_back(first);

        nodes[first].degree++;
        nodes[second].degree++;
    }
}

void *simulateUserAction(void *arg)
{
    FILE *fp = (FILE *)arg;
    int nodeId, n, degree, num_action, action_type, cnt;
    while (1)
    {   
        // select 100 random nodes
        for (int i = 0; i < 100; i++)
        {
            // select a random node id
            nodeId = rand() % 37700;
            degree = nodes[nodeId].degree;
            degree = 1 + floor(log2(degree));
            num_action = PCONSTANT * degree;

            if (pthread_mutex_lock(&lock_wallq))
            {
                perror("wall queue lock failed\n");
                exit(1);
            }
            else {
                fprintf(fp, "userSimulator thread locked the wall queue\n");
            }
            fprintf(fp, "\nNode id selected by userSimulator: %d\nNo of Actions generated: %d\nDegree of node id %d: %d\n", nodeId, num_action, nodeId, nodes[nodeId].degree);
            for (int j = 0; j < num_action; j++)
            {
                Action newAction;
                newAction.user_id = nodeId;
                action_type = rand() % 3;
                newAction.action_type = action_type;

                // action_id is a countervariable associated with each node and specific to each action
                cnt = ++nodes[nodeId].action_id[action_type];
                newAction.action_id = cnt++;

                time_t curr_time = time(NULL);
                // cout << curr_time << "\n";
                newAction.time_stamp = curr_time;

                // push the action to the WallQueue of user node
                nodes[nodeId].WallQueue.push(newAction);

                // push the action to a queue monitored by pushUpdatethreads
                GlbWallQueue.push(newAction);

                // print the details of new action to sns.log
                fprintf(fp, "----Action----\nAction id: %d\nAction type: %d\nTimestamp: %d\n", newAction.action_id, newAction.action_type, newAction.time_stamp);
            }
            if (pthread_mutex_unlock(&lock_wallq))
            {
                perror("wall queue unlock failed\n");
                exit(1);
            }
            else {
                fprintf(fp, "userSimulator thread unlocked the wall queue\n");
            }
        }
        // sleep for 2 minutes after pushing all actions
        sleep(120);
    }
    pthread_exit(NULL);
}

void *pushUpdate(void *arg)
{

    int id = *(int *)arg;
    bool got_action = false;
    Action newAction;

    // worker threads come here access the queue (critical) and dequeue "Action". 
    // If the queue is empty they wait on the condition using pthread_cond_wait 
    // variable else the thread deques an element from queue and signals after checking again for the condition
    // After that the thread pushes the actions to neighours feed

    while (1)
    {
        if (pthread_mutex_lock(&lock_wallq))
        {
            perror("wall queue lock failed\n");
            exit(1);
        }
        else {
            fprintf(fp, "[pushUpdate] Thread id %d locked the wall queue\n", id);
        }

        if (!GlbWallQueue.empty())
        {
            newAction = GlbWallQueue.front();
            got_action = true;
            GlbWallQueue.pop();

            // if condition is reached then send signal
            if (!GlbWallQueue.empty())
            {
                pthread_cond_broadcast(&cv_empty);
                fprintf(fp, "[pushUpdate] Thread id %d sent condition signal.\n", id);
            }
        }
        else
        {
            // wait for some actions to be added to queue
            while (GlbWallQueue.empty())
            {
                fprintf(fp, "[pushUpdate] Thread id %d going into wait for actions to be added in queue...\n", id);
                pthread_cond_wait(&cv_empty, &lock_wallq);
            }
        }

        if (pthread_mutex_unlock(&lock_wallq))
        {
            perror("wall queue unlock failed\n");
            exit(1);
        }
        else {
            fprintf(fp, "[pushUpdate] Thread id %d unlocked the wall queue\n", id);
        }

        // do the work
        if (got_action)
        {
            // add element to feed queue of each neighbour
            for (int i = 0; i < graph[newAction.user_id].size(); i++)
            {
                int neighbourId = graph[newAction.user_id][i];
                int _id = neighbourId % NUM_READ_THREADS;

                if ( pthread_mutex_lock(&lock_feedq[_id]) ) {
                    perror("feed queue lock failed");
                    exit(1);
                } 
                else {
                    fprintf(fp, "[pushUpdate] Thread id %d locked the feed queue\n", id);
                }

                // TODO: add a mutex to protect the feed-queue
                nodes[neighbourId].FeedQueue.push(newAction);                
                feed_queue[_id].push(neighbourId);


                // adding a pthread_cond_signal/broadcast to wake up
                // stuck readPost threads
                fprintf(fp, "[pushUpdate] Thread id %d added action of Node id %d to feed of Node id %d\n", id, newAction.user_id, neighbourId);

                pthread_cond_broadcast(&feed_empty[_id]);
                fprintf(fp, "[pushUpdate] This thread %d sent the broadcast\n", id);

                if ( pthread_mutex_unlock(&lock_feedq[_id]) )
                {
                    perror("wall queue unlock failed\n");
                    exit(1);
                }
            }
            got_action = false;
        }
    }
    pthread_exit(NULL);
}

void *readPost(void *arg) {
    int id = *(int *)arg;

    /**
     * We're basically dividing the nodes up into 10 classes
     * and making the threads take up one class each.
     * 
     * For each of these classes, there is a mutex defined,
     * when we access a particular node's feed-queue, we will acquire
     * the lock, and update it, and then free the lock
    */
    
    // getting the lock to wait on
    pthread_mutex_t feed_lock = lock_feedq[id - 1];

    while (1) {
    
        // waiting on its feed-queue's lock
        if ( pthread_mutex_lock(&feed_lock) ) {
            perror("feed queue lock failed");
            exit(1);
        } 
        else {
            fprintf(fp, "[readPost] Thread id %d locked the feed queue\n", id);
        }

        while (!feed_queue[id - 1].empty()) {

            int node = feed_queue[id - 1].front();
            feed_queue[id - 1].pop();

            nodes[node].FeedQueue.pop();

            fprintf(fp, "[readPost] Thread id %d popped %d node's Feed\n", id, node);
        }

        while (feed_queue[id - 1].empty())
        {
            fprintf(fp, "[readPost] Thread id %d going into wait for actions to be added in queue...\n", id);
            pthread_cond_wait(&feed_empty[id - 1], &feed_lock);
        }

        // unlocking its feed-queue's lock
        if ( pthread_mutex_unlock(&feed_lock) ) {
            perror("feed queue lock failed");
            exit(1);
        } 
        else {
            fprintf(fp, "[readPost] Thread id %d unlocked the feed queue\n", id);
        }
    }


    pthread_exit(NULL);
}

int main()
{

    srand(time(0)); // seed the random number generator with the current time

    for (int i = 0; i < ROWS; i++)
    {
        int randInt = rand() % 2;
        nodes[i].priority = randInt;
        nodes[i].degree = 0;
        // No of "post", "comment", "like" set to 0 for all nodes
        nodes[i].action_id[0] = 0;
        nodes[i].action_id[1] = 0;
        nodes[i].action_id[2] = 0;
    }

    // populate the graph
    populate_graph(graph);

    int max_degree = -1;
    for (int i = 0; i < ROWS; i++)
    {
        max_degree = max(max_degree, nodes[i].degree);
    }
    printf("Number of nodes -> %ld\n", graph.size());
    cout << "Maximum degree of node: " << max_degree << "\n";

    actionCnt = 0;
    int return_value;
    pthread_t userSimulator;
    pthread_attr_t attr;

    // initialize pthread mutex protecting GlbWallQueue and condition variable objects
    pthread_mutex_init(&lock_wallq, NULL);
    pthread_cond_init(&cv_empty, NULL);

    // explicitly creating threads in a joinable state 
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    fp = fopen(FILE_PATH_LOG, "aw");
    if ((return_value = pthread_create(&userSimulator, &attr, simulateUserAction, (void *)fp)))
    {
        perror("pthread_create\n");
        exit(0);
    }
    // create 10 readPost 25 pushUpdate thread
    pthread_t readPost_pool[10];
    pthread_t pushUpdate_pool[25];

    for (int i = 0; i < 25; i++)
    {
        int tid = i + 1;
        pthread_create(&pushUpdate_pool[i], &attr, pushUpdate, &tid);
    }
    
    int thread_ids[NUM_READ_THREADS];
    for(int i=0; i < 10; i++) {
        
        thread_ids[i] = i + 1;
        pthread_create(&readPost_pool[i], &attr, readPost, &thread_ids[i]);
    }
    
    // block until the thread completes
    pthread_join(userSimulator, NULL);
    for (int i = 0; i < 25; ++i)
    {
        pthread_join(pushUpdate_pool[i], NULL);
    }

    fclose(fp);
    // Clean up and exit 
    // pthread_mutex_destroy(&lock_wallq);
    // pthread_cond_destroy(&cv_empty);
    // pthread_exit(NULL);
    return 0;
}