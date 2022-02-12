#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include "lib/infint/InfInt.h"
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <cstring>

#include "collatz.hpp"

using ull = unsigned long long;

// Wersja korzystająca z new_process.cpp działa dużo wolniej.
// Testy procesowe (bez drużyn X) przechodzą wtedy w ok. 30 minuty, zaś w wersji korzystającej z pamięci anonimowej ok. 15 minut.
// Na samym dole pliku teams.cpp zostawiłem zakomentowane wersje obu drużyn (bez X) korzystające z new_process.cpp.
int main(int argc, char *argv[]) {
    int read_dsc = -1, write_dsc = -1;

    read_dsc = shm_open("/collatz_mem_in", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    write_dsc = shm_open("/collatz_mem_out", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (read_dsc == -1) {
        printf("read - errno(%d): %s\n", errno, std::strerror(errno));
    }
    if (write_dsc == -1) {
        printf("write - errno(%d): %s\n", errno, std::strerror(errno));
    }
    size_t begin = strtoul(argv[1], NULL, 10);
    size_t my_input_size = strtoul(argv[2], NULL, 10);
    size_t input_size = strtoul(argv[3], NULL, 10);

    char (*mapped_input)[MAX_INFINT_LEN];
    uint64_t *mapped_output;

    int flags, prot;
    prot = PROT_READ | PROT_WRITE;
    flags = MAP_SHARED;
    mapped_input =(char(*)[MAX_INFINT_LEN]) mmap(NULL, input_size * MAX_INFINT_LEN, prot, flags, read_dsc, 0);
    if (mapped_input == MAP_FAILED) {
        printf("mmap 1 - errno(%d): %s\n", errno, std::strerror(errno));
    }
    mapped_output = (uint64_t*) mmap(NULL, input_size * sizeof(uint64_t), prot, flags, write_dsc, 0);
    if (mapped_output == MAP_FAILED) {
        printf("mmap 2 - errno(%d): %s\n", errno, std::strerror(errno));
    }

    for (size_t i = 0; i < my_input_size; ++i) {
        mapped_output[begin + i] = calcCollatz(mapped_input[begin + i]);
    }

    close(read_dsc);
    close(write_dsc);
    munmap(mapped_input, input_size * MAX_INFINT_LEN);
    munmap(mapped_output, input_size * sizeof(uint64_t));

    return 0;
}