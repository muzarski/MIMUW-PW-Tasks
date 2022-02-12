#include "HashMap.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "path_utils.h"
#include <assert.h>
#include "Tree.h"
#include <errno.h>

void print_map(HashMap* map) {
    const char* key = NULL;
    void* value = NULL;
    printf("Size=%zd\n", hmap_size(map));
    HashMapIterator it = hmap_iterator(map);
    while (hmap_next(map, &it, &key, &value)) {
        printf("Key=%s Value=%p\n", key, value);
    }
    printf("\n");
}

int main(void) {

    Tree *t = tree_new();

    tree_create(t, "/c/");
    tree_create(t, "/c/a/");
    printf("elo\n");
    tree_create(t, "/c/a/a/");
    tree_create(t, "/c/a/a/a/");

    tree_create(t, "/c/c/");
    tree_create(t, "/c/c/c/");
    tree_create(t, "/c/c/d/");

    tree_create(t, "/b/");
    tree_create(t, "/b/a/");
    printf("elo\n");

    tree_remove(t, "/");
    assert(tree_remove(t, "/c/a/a/") == ENOTEMPTY);
    printf("elo\n");
    assert(tree_remove(t, "/c/c/c/") == 0);
    assert(tree_remove(t, "/a/a/a/a/") == ENOENT);
    assert(tree_remove(t, "/b/") == ENOTEMPTY);
    assert(tree_remove(t, "/a/a/c/a/") == ENOENT);
    assert(tree_remove(t, "/a/a/a/a/") == ENOENT);
    assert(tree_remove(t, "/") == EBUSY);
    assert(tree_remove(t, "/") == EBUSY);
    assert(tree_remove(t, "/c/c/") == ENOTEMPTY);
    assert(tree_remove(t, "/a/c/c/") == ENOENT);
    assert(tree_remove(t, "/a/b/c/a/") == ENOENT);
    assert(tree_remove(t, "/c/") == ENOTEMPTY);
    int x = tree_remove(t, "/c/b/b/b/");
    printf("%d\n", x);

    tree_free(t);
    return 0;
}