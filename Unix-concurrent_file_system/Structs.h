#pragma once

#include <pthread.h>
#include <memory.h>
#include "HashMap.h"
#include "err.h"

typedef struct Node Node;

// Each directory is represented by a Node.
// Each Node holds a map containing his children.
// Access to this map is protected by a rw-lock, where:
// - Readers use hmap_get,
// - Writers use hmap_remove, hmap_get, hmap_size and/or hmap_insert
// Many readers can read at the same time (assuming there is no reader or writer using the map).
// Writers can use the map only if there are no readers or writers using the map.
// When removing/moving specific Node, we need to wait until all operations in the subtree are finished.
// (Only then can we guarantee that our operations return such results as if they were performed sequentially, in some order).

// Each thread - while traversing the path - increases the number of the threads in subtrees.
// After performing its operation on the target Node, it simply rollbacks.
// When doing rollback, it decreases number of the threads in the subtrees - waking up any removers if possible, obviously.
struct Node {
    Node *parent; // Needed so we can perform rollback after finishing each operation.
    HashMap *children; // Map <char*, Node>
    pthread_mutex_t mutex;
    pthread_cond_t readers;
    pthread_cond_t writers;
    pthread_cond_t removers;
    int reading, writing;
    int r_wait, w_wait;
    int in_subtree; // Tells how many threads are in given subtree.
    int change;
};

Node* new_node();

void node_destroy(Node *n);

void free_rec(Node *n);

// Starting protocol called before reading from the map.
void before_read(Node *n);

// Ending protocol called after reading from the map.
void after_read(Node *n);

// Starting protocol called before inserting into the map.
void before_write(Node *n);

// Ending protocol called after inserting into the map.
void after_write(Node *n);

// Increases 'in_subtree' counter.
void entering_subtree(Node *n);

// Decreases 'in_subtree' counter. Wakes up the remover if possible.
void leaving_subtree(Node *n);

// Starting protocol called before removing from the map.
// We need to wait until everything in the subtree is done.
void before_remove(Node *n);