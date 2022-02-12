#include <stdlib.h>
#include "Structs.h"

Node* new_node() {
    Node *n = (Node*) malloc(sizeof (Node));
    if(!n)
        return NULL;

    int err;

    if ((err = pthread_mutex_init(&n->mutex, 0)) != 0)
        syserr("mutex init", err);
    if ((err = pthread_cond_init(&n->readers, 0)) != 0)
        syserr("cond readers init", err);
    if ((err = pthread_cond_init(&n->writers, 0)) != 0)
        syserr("cond writers init", err);
    if ((err = pthread_cond_init(&n->removers, 0)) != 0)
        syserr("cond removers init", err);

    n->reading = 0;
    n->r_wait = 0;
    n->writing = 0;
    n->w_wait = 0;
    n->in_subtree = 0;
    n->change = -1;
    n->children = hmap_new();
    n->parent = NULL;

    if (!n->children)
        fatal("hmap_new");

    return n;
}

void node_destroy(Node *n) {
    int err;

    if ((err = pthread_cond_destroy(&n->readers)) != 0)
        syserr("cond destroy readers", err);
    if ((err = pthread_cond_destroy(&n->writers)) != 0)
        syserr("cond destroy writers", err);
    if ((err = pthread_cond_destroy(&n->removers)) != 0)
        syserr("cond destroy removers", err);
    if ((err = pthread_mutex_destroy(&n->mutex)) != 0)
        syserr("mutex destroy", err);
}

void free_rec(Node *n) {
    const char *key;
    void *value;
    Node *child;

    HashMapIterator it = hmap_iterator(n->children);
    while (hmap_next(n->children, &it, &key, &value)) {
        child = (Node*) value;
        free_rec(child);
    }

    node_destroy(n);
    hmap_free(n->children);
    free(n);
}

void before_read(Node *n) {
    int err;

    if ((err = pthread_mutex_lock(&n->mutex)) != 0)
        syserr("mutex lock in before_read", err);

    if (n->w_wait > 0 || n->writing > 0) {
        n->r_wait++;

        if ((err = pthread_cond_wait(&n->readers, &n->mutex)) != 0)
            syserr("wait readers", err);
        while(n->writing > 0) {
            if ((err = pthread_cond_wait(&n->readers, &n->mutex)) != 0)
                syserr("wait readers", err);
        }
        n->r_wait--;
    }

    n->reading++;

    if (n->r_wait > 0) {
        n->change = 1;
        if ((err = pthread_cond_signal(&n->readers)) != 0)
            syserr("signal readers", err);
    }

    if ((err = pthread_mutex_unlock(&n->mutex)) != 0)
        syserr("mutex unlock in before_read", err);
}

void after_read(Node *n) {
    int err;

    if ((err = pthread_mutex_lock(&n->mutex)) != 0)
        syserr("mutex lock in after_red", err);

    n->reading--;

    if (n->reading == 0) {
        n->change = 0;
        if ((err = pthread_cond_signal(&n->writers)) != 0)
            syserr("signal removers", err);
    }

    if ((err = pthread_mutex_unlock(&n->mutex)) != 0)
        syserr("mutex unlock in after_read", err);
}

void before_write(Node *n) {
    int err;

    if ((err = pthread_mutex_lock(&n->mutex)) != 0)
        syserr("mutex lock i before_create", err);

    if (n->reading > 0 || n->writing > 0) {
        n->w_wait++;
        while(n->change != 0 || n->reading > 0 || n->writing > 0) {
            if ((err = pthread_cond_wait(&n->writers, &n->mutex)) != 0)
                syserr("wait removers in before_create", err);
        }
        n->w_wait--;
    }

    n->writing++;

    if ((err = pthread_mutex_unlock(&n->mutex)) != 0)
        syserr("mutex unlock in before_create", err);
}

void after_write(Node *n) {
    int err;

    if ((err = pthread_mutex_lock(&n->mutex)) != 0)
        syserr("mutex lock after_create", err);

    n->writing--;

    if (n->r_wait > 0) {
        n->change = 1;
        if ((err = pthread_cond_signal(&n->readers)) != 0)
            syserr("signal readers in after_create", err);
    }
    else if (n->w_wait > 0) {
        n->change = 0;
        if ((err = pthread_cond_signal(&n->writers)) != 0)
            syserr("signal removers in after_create", err);
    }

    if ((err = pthread_mutex_unlock(&n->mutex)) != 0)
        syserr("mutex unlock in after_create", err);
}

void before_remove(Node *n) {
    int err;

    if ((err = pthread_mutex_lock(&n->mutex)) != 0)
        syserr("mutex lock in before_write", err);

    // Wait if there are any threads working in the subtree.
    if (n->in_subtree > 0) {
        while(n->change != 2 || n->in_subtree > 0) {
            if ((err = pthread_cond_wait(&n->removers, &n->mutex)) != 0)
                syserr("wait writers", err);
        }
    }

    if ((err = pthread_mutex_unlock(&n->mutex)) != 0)
        syserr("mutex unlock in before_write", err);
}

void entering_subtree(Node *n) {
    if (!n)
        return;

    int err;

    if ((err = pthread_mutex_lock(&n->mutex)) != 0)
        syserr("mutex lock in entering_subtree", err);

    // Denote entering the subtree.
    n->in_subtree++;

    if ((err = pthread_mutex_unlock(&n->mutex)) != 0)
        syserr("mutex unlock in entering_subtree", err);
}

void leaving_subtree(Node *n) {

    if (!n)
        return;

    int err;

    if ((err = pthread_mutex_lock(&n->mutex)) != 0)
        syserr("mutex lock in leaving_subtree", err);

    // Denote leaving the subtree.
    n->in_subtree--;

    // Since I am the last one leaving the subtree,
    // I wake up a thread (if there waits one) that wants to remove/move this Node.
    if (n->in_subtree == 0) {
        n->change = 2;
        if ((err = pthread_cond_signal(&n->removers)) != 0)
            syserr("signal removers", err);
    }

    if ((err = pthread_mutex_unlock(&n->mutex)) != 0)
        syserr("mutex unlock in leaving_subtree", err);
}
