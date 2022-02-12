#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <assert.h>

#include "err.h"
#include "Tree.h"
#include "Structs.h"

#define MOVING_TO_SUBTREE -1

struct Tree {
    Node *r;
};

// Denotes leaving the subtrees on the path from 'n' to 'root'.
static void rollback_rec(Node *n) {
    if (!n)
        return;

    Node *parent = n->parent;
    leaving_subtree(n);
    rollback_rec(parent);
}

static void cleanup_create_remove(Node *parent) {
    after_write(parent);
    rollback_rec(parent);
}

// Finds the direct parent of the folder.
static int find_parent(int *i, int count, Node **parent, Node **child,
                      char dir[], const char **subpath) {

    while (*i < count) {
        before_read(*parent);
        *child = (Node*) hmap_get((*parent)->children, dir);
        entering_subtree(*child);
        after_read(*parent);

        if (!(*child)) {
            rollback_rec(*parent);
            return ENOENT;
        }

        *parent = *child;
        *subpath = split_path(*subpath, dir);

        ++(*i);
    }
    return 0;
}

// Leave the subtree rooted in 'common_parent'.
static void rollback_rec_move(Node *common_parent, Node *n) {
    Node *cur = n;
    Node *next;
    while (cur != common_parent) {
        next = cur->parent;
        leaving_subtree(cur);
        cur = next;
    }
}

static void cleanup_move(Node *parent_src, Node *parent_trg, Node *common_parent, bool should_release_common) {

    if (parent_trg) {
        if (parent_trg != common_parent && parent_trg != parent_src) {
            after_write(parent_trg);
        }
        rollback_rec_move(common_parent, parent_trg);
    }
    if (parent_src) {
        if (parent_src != common_parent) {
            after_write(parent_src);
        }
        rollback_rec_move(common_parent, parent_src);
    }
    if (common_parent) {
        if (should_release_common) {
            after_write(common_parent);
        }
        rollback_rec(common_parent);
    }
}

// We already found a common_parent of source and target.
// Function continues traversal and finds the direct parent of source/target.
static int find_parent_move(int *i, int count, Node **parent, Node *common_parent, Node **child,
                            const char **subpath, char dir[]) {

    while(*i < count) {
        if (*parent != common_parent)
            before_read(*parent);

        *child = (Node*) hmap_get((*parent)->children, dir);
        entering_subtree(*child);

        if (*parent != common_parent)
            after_read(*parent);

        if (!(*child)) {
            rollback_rec_move(common_parent, *parent);
            after_write(common_parent);
            rollback_rec(common_parent);
            return ENOENT;
        }

        *parent = *child;
        *subpath = split_path(*subpath, dir);
        ++(*i);
    }

    return 0;
}

Tree* tree_new() {
    Node *n = new_node();
    if (!n)
        exit(1);

    Tree *tree = (Tree*) malloc(sizeof (Tree));
    if (!tree)
        exit(1);

    tree->r = n;
    return tree;
}

void tree_free(Tree* tree) {
    free_rec(tree->r);
    free(tree);
}

char* tree_list(Tree* tree, const char* path) {
    if (!is_path_valid(path)) return NULL;

    Node *parent = tree->r;
    Node *child = parent;
    entering_subtree(parent);

    char dir[MAX_FOLDER_NAME_LENGTH + 1];
    const char* subpath = path;
    while ((subpath = split_path(subpath, dir)) && child) {
        before_read(parent);
        child = (Node*) hmap_get(parent->children, dir);
        entering_subtree(child);
        after_read(parent);

        if (child) {
            parent = child;
        }
        else {
            rollback_rec(parent);
            return NULL;
        }
    }

    if (subpath) {
        rollback_rec(parent);
        return NULL;
    }

    before_read(parent);
    char* res = make_map_contents_string(parent->children);
    after_read(parent);

    rollback_rec(parent);
    return res;
}

int tree_create(Tree* tree, const char* path) {
    if (!is_path_valid(path)) return EINVAL;
    if (strcmp(path, "/") == 0) return EEXIST;

    Node *parent = tree->r;
    Node *child = parent;
    entering_subtree(parent);

    char dir[MAX_FOLDER_NAME_LENGTH + 1];
    const char* subpath = split_path(path, dir);
    int count = dir_count(path) - 1;
    int i = 0;

    int err;

    if ((err = find_parent(&i, count, &parent, &child, dir, &subpath)) != 0) {
        return err;
    }

    before_write(parent);

    child = hmap_get(parent->children, dir);

    if (child) {
        cleanup_create_remove(parent);
        return EEXIST;
    }

    child = new_node();
    child->parent = parent;
    hmap_insert(parent->children, dir, child);

    cleanup_create_remove(parent);
    return 0;
}

int tree_remove(Tree* tree, const char* path) {
    if (!is_path_valid(path)) return EINVAL;
    if (strcmp(path, "/") == 0) return EBUSY;

    Node *parent = tree->r;
    Node *child = parent;
    entering_subtree(parent);

    char dir[MAX_FOLDER_NAME_LENGTH + 1];
    const char* subpath = split_path(path, dir);
    int count = dir_count(path) - 1;
    int i = 0;
    int err;

    if ((err = find_parent(&i, count, &parent, &child, dir, &subpath)) != 0)
        return err;

    before_write(parent);
    child = (Node*) hmap_get(parent->children, dir);

    if (!child) {
        cleanup_create_remove(parent);
        return ENOENT;
    }

    before_remove(child);

    if (hmap_size(child->children) > 0) {
        cleanup_create_remove(parent);
        return ENOTEMPTY;
    }

    hmap_remove(parent->children, dir);

    cleanup_create_remove(parent);

    // Acquiring mutex before removal so helgrind does not report false positives.
    // (data race between mutex_destroy and mutex_release)
    if (pthread_mutex_lock(&child->mutex))
        syserr("mutex lock in remove", err);
    if (pthread_mutex_unlock(&child->mutex))
        syserr("mutex unlock in remove", err);

    free_rec(child);
    return 0;
}

int tree_move(Tree* tree, const char* source, const char* target) {
    if (!is_path_valid(source)) return EINVAL;
    if (!is_path_valid(target)) return EINVAL;
    if (strcmp(source, "/") == 0) return EBUSY;
    if (strcmp(target, "/") == 0) return EEXIST;

    size_t src_len = strlen(source);
    size_t trg_len = strlen(target);

    if (trg_len > src_len && strncmp(source, target, src_len) == 0) return MOVING_TO_SUBTREE;

    Node *parent_src = tree->r;
    Node *child_src;
    char dir_src[MAX_FOLDER_NAME_LENGTH + 1];
    const char* subpath_src = split_path(source, dir_src);
    int count_src = dir_count(source) - 1;
    int i_src = 0;

    Node *parent_trg = tree->r;
    Node *child_trg;
    char dir_trg[MAX_FOLDER_NAME_LENGTH + 1];
    const char* subpath_trg = split_path(target, dir_trg);
    int count_trg = dir_count(target) - 1;
    int i_trg = 0;

    entering_subtree(parent_src);
    int err;

    // Finds the youngest common ancestor of source and target
    while (i_src < count_src && i_trg < count_trg && strcmp(dir_src, dir_trg) == 0) {
        before_read(parent_src);
        child_src = (Node*) hmap_get(parent_src->children, dir_src);
        entering_subtree(child_src);
        after_read(parent_src);

        if (!child_src) {
            rollback_rec(parent_src);
            return ENOENT;
        }

        child_trg = child_src;

        parent_src = child_src;
        parent_trg = child_trg;

        subpath_trg = split_path(subpath_trg, dir_trg);
        subpath_src = split_path(subpath_src, dir_src);

        ++i_src;
        ++i_trg;
    }

    bool flag = true; // flag telling if we need to release lock from common_parent while performing cleanup.
    Node *common_parent = parent_src;
    // writer-lock common_parent so there is no deadlock caused by 'intersecting' moves.
    before_write(common_parent);

    if ((err = find_parent_move(&i_src, count_src, &parent_src, common_parent,
                                &child_src, &subpath_src, dir_src)) != 0) {
        return err;
    }

    if ((err = find_parent_move(&i_trg, count_trg, &parent_trg, common_parent,
                                &child_trg, &subpath_trg, dir_trg)) != 0) {
        // Clean the path from parent_src to common_parent (doesn't matter when we do it).
        rollback_rec_move(common_parent, parent_src);
        return err;
    }

    // writer-lock parent_src if needed
    if (parent_src != common_parent)
        before_write(parent_src);

    // writer-lock parent_trg if needed
    if (parent_trg != common_parent && parent_trg != parent_src)
        before_write(parent_trg);

    // We found and put writer-locks on parents of the source and target.
    // We can release writer-lock from the common parent, so it doesn't block the path anymore.
    if (common_parent != parent_src && common_parent != parent_trg) {
        flag = false;
        after_write(common_parent);
    }

    child_src = (Node*) hmap_get(parent_src->children, dir_src);
    child_trg = (Node*) hmap_get(parent_trg->children, dir_trg);

    if (!child_src) {
        cleanup_move(parent_src, parent_trg, common_parent, flag);
        return ENOENT;
    }

    if (strcmp(source, target) == 0) {
        cleanup_move(parent_src, parent_trg, common_parent, flag);
        return 0;
    }

    if (child_trg) {
        cleanup_move(parent_src, parent_trg, common_parent, flag);
        return EEXIST;
    }

    // Wait until everything in the subtree ends.
    before_remove(child_src);

    hmap_remove(parent_src->children, dir_src);
    hmap_insert(parent_trg->children, dir_trg, child_src);
    child_src->parent = parent_trg;

    cleanup_move(parent_src, parent_trg, common_parent, flag);
    return 0;
}