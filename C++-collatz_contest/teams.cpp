#include <utility>
#include <deque>
#include <future>

#include <algorithm>
#include "teams.hpp"
#include "contest.hpp"
#include "collatz.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */


static uint64_t computeValue(const InfInt &in, const std::shared_ptr<SharedResults> &shared) {
    uint64_t val;
    if (shared) {
        auto res = shared->tryRead(in);
        val = res.second;
        if (!res.first) {
            val = calcCollatz(in);
            shared->assignComputed(in, val);
        }
    }
    else {
        val = calcCollatz(in);
    }

    return val;
}

static void newThreadsFun(std::mutex &m, std::condition_variable_any &cv,
                          ContestResult &result, const InfInt &in, size_t pos,
                          uint32_t &workingThreads, const std::shared_ptr<SharedResults> &shared) {

    result[pos] = computeValue(in, shared);

    m.lock();

    --workingThreads;
    // Weak up parent thread (so it can create new thread as my replacement).
    cv.notify_one();

    m.unlock();
}

ContestResult TeamNewThreads::runContestImpl(const ContestInput &contestInput) {
    ContestResult r(contestInput.size());

    uint32_t thread_count = this->getSize();
    std::mutex m;
    std::condition_variable_any cv;
    uint32_t workingThreads = 0;

    std::vector<std::thread> threads(contestInput.size());

    for (size_t i = 0; i < contestInput.size(); ++i) {
        m.lock();

        while(workingThreads == thread_count) {
            // Wait until any child thread finishes its job.
            cv.wait(m);
        }
        ++workingThreads;

        m.unlock();

        threads[i] = createThread(newThreadsFun, std::ref(m), std::ref(cv), std::ref(r),
                                  std::ref(contestInput[i]), i, std::ref(workingThreads), this->getSharedResults());
    }

    for (std::thread &t : threads)
        t.join();

    return r;
}

static void threadFunEqualSize(uint32_t id, uint32_t thread_count, const ContestInput &input,
                               ContestResult &res, const std::shared_ptr<SharedResults> &shared) {
    ContestResult r(input.size());
    size_t i = id;
    while (i < input.size()) {
        res[i] = computeValue(input[i], shared);
        i += thread_count;
    }
}

ContestResult TeamConstThreads::runContestImpl(const ContestInput &contestInput) {
    ContestResult r(contestInput.size());
    uint32_t thread_count = this->getSize();

    std::vector<std::thread> threads;

    for (size_t i = 0; i < thread_count; ++i) {
        threads.push_back(this->createThread(threadFunEqualSize, i, thread_count, std::ref(contestInput),
                                             std::ref(r), this->getSharedResults()));
    }

    for (size_t i = 0; i < thread_count; ++i) {
        threads[i].join();
    }

    return r;
}

ContestResult TeamPool::runContest(const ContestInput &contestInput) {
    ContestResult r(contestInput.size());
    uint32_t thread_count = this->getSize();

    std::vector<std::future<void>> futures(thread_count);

    for (size_t i = 0; i < thread_count; ++i) {
        futures[i] = this->pool.push(threadFunEqualSize, i, thread_count, std::ref(contestInput),
                                     std::ref(r), this->getSharedResults());
    }

    cxxpool::get(futures.begin(), futures.end());
    return r;
}

static size_t min(uint32_t x, unsigned long y) {
    if (x <= y)
        return x;
    return y;
}

static void print_error(const char *s) {
    printf("%s - errno(%d): %s\n", s, errno, std::strerror(errno));
    exit(1);
}

void processTask(const ContestInput &input, uint64_t *output, size_t begin_id, size_t my_size) {
    for (size_t i = 0; i < my_size; ++i)
        output[i + begin_id] = calcCollatz(input[i + begin_id]);
}

ContestResult TeamNewProcesses::runContest(const ContestInput &contestInput) {
    ContestResult r(contestInput.size());
    uint32_t p_count = this->getSize();

    pid_t pid;
    size_t w_count = 0;

    size_t result_bytes = contestInput.size() * sizeof(uint64_t);
    uint64_t *mapped_output;
    int fd_mem_out = -1;
    int flags = MAP_SHARED | MAP_ANONYMOUS, prot = PROT_READ | PROT_WRITE;

    mapped_output = (uint64_t*) mmap(NULL, result_bytes, prot, flags, fd_mem_out, 0);
    if (mapped_output == MAP_FAILED)
        print_error("NewProcesses mmap");

    for (size_t i = 0; i < contestInput.size(); ++i) {
        pid = fork();

        if (pid == -1) {
            print_error("NewProcesses fork");
        }
        else if (pid == 0) {
            processTask(contestInput, mapped_output, i, 1);
            if (munmap(mapped_output, result_bytes) == -1)
                print_error("NewProcesses munmap child");

            exit(0);
        }
        else {
            if (i >= p_count - 1) {
                ++w_count;
                if (wait(nullptr) == -1)
                    print_error("NewProcesses wait 1");
            }
        }
    }

    size_t to_wait_for = min(p_count - 1, contestInput.size());
    for (size_t i = 0; i < to_wait_for; ++i) {
        ++w_count;
        if (wait(nullptr) == -1)
            print_error("NewProcesses wait 2");
    }

//    assert(w_count == contestInput.size());

    for (size_t i = 0; i < contestInput.size(); ++i)
        r[i] = mapped_output[i];

    if (munmap(mapped_output, result_bytes) == -1)
        print_error("NewProcesses munmap parent");

    return r;
}

ContestResult TeamConstProcesses::runContest(ContestInput const &contestInput) {
    ContestResult r(contestInput.size());
    uint32_t p_count = this->getSize();

    pid_t pid;

    size_t result_bytes = contestInput.size() * sizeof(uint64_t);
    uint64_t *mapped_output;
    int fd_mem_out = -1;
    int flags = MAP_SHARED | MAP_ANONYMOUS, prot = PROT_READ | PROT_WRITE;

    mapped_output = (uint64_t*) mmap(NULL, result_bytes, prot, flags, fd_mem_out, 0);
    if (mapped_output == MAP_FAILED)
        print_error("ConstProcesses mmap");

    size_t begin = 0;
    for (size_t i = 0; i < p_count; ++i) {

        size_t my_size = contestInput.size() / p_count + (i < contestInput.size() % p_count ? 1 : 0);

        pid = fork();
        if (pid == -1) {
            print_error("ConstProcesses fork");
        }
        else if (pid == 0) {
            processTask(contestInput, mapped_output, begin, my_size);

            if (munmap(mapped_output, result_bytes) == -1)
                print_error("ConstProcesses munmap child");

            exit(0);
        }
        else {
            begin += my_size;
        }
    }

    for (size_t i = 0; i < p_count; ++i) {
        if (wait(nullptr) == -1)
            print_error("ConstProcesses wait");
    }

    assert(begin == contestInput.size());

    for (size_t i = 0; i < contestInput.size(); ++i)
        r[i] = mapped_output[i];

    if (munmap(mapped_output, result_bytes) == -1)
        print_error("ConstProcesses munmap parent");

    return r;
}

// interval [l, r)
static void asyncFun(size_t l, size_t r, size_t rec_depth, const ContestInput &input,
                     ContestResult &result, const std::shared_ptr<SharedResults> &shared) {
    // 32 threads are enough.
    if (r - l == 1 || rec_depth == 5) {
        for (size_t i = l; i < r; ++i) {
            result[i] = computeValue(input[i], shared);
        }
        return;
    }

    size_t m = (l + r) / 2;
    std::future<void> fut = std::async(std::launch::async, asyncFun, m, r, rec_depth + 1,
                                       std::ref(input), std::ref(result), shared);
    asyncFun(l, m, rec_depth + 1, input, result, shared);
    fut.get();
}

ContestResult TeamAsync::runContest(const ContestInput &contestInput) {
    ContestResult r(contestInput.size());
    asyncFun(0, contestInput.size(), 0, contestInput, r, this->getSharedResults());
    return r;
}



// Versions of the processes Teams using new_process.cpp and named SHM.

//ContestResult TeamNewProcesses::runContest(const ContestInput &contestInput) {
//    ContestResult r(contestInput.size());
//    uint32_t p_count = this->getSize();
//
//    pid_t pid;
//    size_t w_count = 0;
//
//    size_t input_bytes = contestInput.size() * MAX_INFINT_LEN;
//    size_t result_bytes = contestInput.size() * sizeof(uint64_t);
//    char (*mapped_input)[MAX_INFINT_LEN];
//    uint64_t *mapped_output;
//    int fd_mem_in = -1, fd_mem_out = -1;
//    int flags, prot;
//
//    fd_mem_in = shm_open("/collatz_mem_in", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
//    if (fd_mem_in == -1) {
//        printf("fd_mem_in - errno(%d): %s\n", errno, std::strerror(errno));
//        exit(1);
//    }
//    if (ftruncate(fd_mem_in, input_bytes) == -1) {
//        printf("ftruncate in - errno(%d): %s\n", errno, std::strerror(errno));
//        exit(1);
//    }
//
//    fd_mem_out = shm_open("/collatz_mem_out", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
//    if (fd_mem_out == -1) {
//        printf("fd_mem_out - errno(%d): %s\n", errno, std::strerror(errno));
//        exit(1);
//    }
//    if (ftruncate(fd_mem_out, result_bytes) == -1) {
//        printf("ftruncate out - errno(%d): %s\n", errno, std::strerror(errno));
//        exit(1);
//    }
//
//    prot = PROT_READ | PROT_WRITE;
//    flags = MAP_SHARED;
//
//    mapped_input = (char(*)[MAX_INFINT_LEN]) mmap(NULL, input_bytes, prot, flags, fd_mem_in, 0);
//    if (mapped_input == MAP_FAILED) {
//        printf("essa0 - errno(%d): %s\n", errno, std::strerror(errno));
//    }
//
//    for (size_t i = 0; i < contestInput.size(); ++i) {
//        strcpy(mapped_input[i], contestInput[i].toString().c_str());
//    }
//
//    for (size_t i = 0; i < contestInput.size(); ++i) {
//
//        char begin_id[10];
//        char is[15];
//        sprintf(begin_id, "%lu", i);
//        sprintf(is, "%lu", contestInput.size());
//
//        pid = fork();
//        if (pid == -1) {
//            printf("fork - errno(%d): %s\n", errno, std::strerror(errno));
//            exit(1);
//        }
//        else if (pid == 0) {
//            if (munmap(mapped_input, input_bytes) == -1) {
//                printf("munmap 1 child - errno(%d): %s\n", errno, std::strerror(errno));
//                exit(1);
//            }
//            if (close(fd_mem_in) == -1) {
//                printf("close 1 child - errno(%d): %s\n", errno, std::strerror(errno));
//                exit(1);
//            }
//            if (close(fd_mem_out) == -1) {
//                printf("close 2 child - errno(%d): %s\n", errno, std::strerror(errno));
//                exit(1);
//            }
//            execl("./new_process", "new_process", begin_id, "1", is, NULL);
//            std::cerr << "error in execl\n";
//            exit(1);
//        }
//        else {
//            if (i >= p_count - 1) {
//                ++w_count;
//                if (wait(nullptr) == -1) {
//                    printf("wait 1 - errno(%d): %s\n", errno, std::strerror(errno));
//                    exit(1);
//                }
//            }
//        }
//    }
//
//    size_t to_wait_for = min(p_count - 1, contestInput.size());
//    for (size_t i = 0; i < to_wait_for; ++i) {
//        ++w_count;
//        if (wait(nullptr) == -1) {
//            printf("wait 2 - errno(%d): %s\n", errno, std::strerror(errno));
//            exit(1);
//        }
//    }
//
//    std::cout << w_count << " " << contestInput.size() << std::endl;
//    assert(w_count == contestInput.size());
//
//    mapped_output = (uint64_t*) mmap(NULL, result_bytes, prot, flags, fd_mem_out, 0);
//    if (mapped_output == MAP_FAILED) {
//        printf("essa-1 - errno(%d): %s\n", errno, std::strerror(errno));
//    }
//
//    for (size_t i = 0; i < contestInput.size(); ++i) {
//        r[i] = mapped_output[i];
//    }
//
//    close(fd_mem_in);
//    close(fd_mem_out);
//    munmap(mapped_input, input_bytes);
//    munmap(mapped_output, result_bytes);
//    if (shm_unlink("/collatz_mem_in") == -1) {
//        printf("shm_unlink_in par - errno(%d): %s\n", errno, std::strerror(errno));
//    }
//    if (shm_unlink("/collatz_mem_out") == -1) {
//        printf("shm_unlink_in par - errno(%d): %s\n", errno, std::strerror(errno));
//    }
//
//    return r;
//}

//ContestResult TeamConstProcesses::runContest(ContestInput const &contestInput) {
//    ContestResult r(contestInput.size());
//    uint32_t p_count = this->getSize();
//
//    pid_t pid;
//    size_t w_count = 0;
//
//    size_t input_bytes = contestInput.size() * MAX_INFINT_LEN;
//    size_t result_bytes = contestInput.size() * sizeof(uint64_t);
//    char (*mapped_input)[MAX_INFINT_LEN];
//    uint64_t *mapped_output;
//    int fd_mem_in = -1, fd_mem_out = -1;
//    int flags, prot;
//
//    fd_mem_in = shm_open("/collatz_mem_in", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
//    if (fd_mem_in == -1) {
//        printf("fd_mem_in - errno(%d): %s\n", errno, std::strerror(errno));
//        exit(1);
//    }
//    if (ftruncate(fd_mem_in, input_bytes) == -1) {
//        printf("ftruncate in - errno(%d): %s\n", errno, std::strerror(errno));
//        exit(1);
//    }
//
//    fd_mem_out = shm_open("/collatz_mem_out", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
//    if (fd_mem_out == -1) {
//        printf("fd_mem_out - errno(%d): %s\n", errno, std::strerror(errno));
//        exit(1);
//    }
//    if (ftruncate(fd_mem_out, result_bytes) == -1) {
//        printf("ftruncate out - errno(%d): %s\n", errno, std::strerror(errno));
//        exit(1);
//    }
//
//    prot = PROT_READ | PROT_WRITE;
//    flags = MAP_SHARED;
//
//    mapped_input = (char(*)[MAX_INFINT_LEN]) mmap(NULL, input_bytes, prot, flags, fd_mem_in, 0);
//    if (mapped_input == MAP_FAILED) {
//        printf("essa0 - errno(%d): %s\n", errno, std::strerror(errno));
//    }
//
//    for (size_t i = 0; i < contestInput.size(); ++i) {
//        strcpy(mapped_input[i], contestInput[i].toString().c_str());
//        //std::cout << mapped_input[i] << std::endl;
//    }
//
//    size_t begin = 0;
//    for (size_t i = 0; i < p_count; ++i) {
//
//        size_t input_size = contestInput.size() / p_count + (i < contestInput.size() % p_count ? 1 : 0 );
//        char fd_in_str[10];
//        char fd_out_str[10];
//        char begin_id[10];
//        char amount[15];
//        char is[15];
//        sprintf(fd_in_str, "%d", fd_mem_in);
//        sprintf(fd_out_str, "%d", fd_mem_out);
//        sprintf(amount, "%lu", input_size);
//        sprintf(begin_id, "%lu", begin);
//        sprintf(is, "%lu", contestInput.size());
//
//        pid = fork();
//        if (pid == -1) {
//            printf("fork - errno(%d): %s\n", errno, std::strerror(errno));
//            exit(1);
//        }
//        else if (pid == 0) {
//            munmap(mapped_input, input_bytes);
//            close(fd_mem_in);
//            close(fd_mem_out);
//            execl("./new_process", "new_process", begin_id, amount, is, NULL);
//            std::cerr << "error in execl\n";
//            exit(1);
//        }
//        else {
//            begin += input_size;
//        }
//    }
//
//    for (size_t i = 0; i < p_count; ++i) {
//        if (wait(nullptr) == -1) {
//            printf("wait - errno(%d): %s\n", errno, std::strerror(errno));
//            exit(1);
//        }
//    }
//
//    assert(begin == contestInput.size());
//
//    mapped_output = (uint64_t*) mmap(NULL, result_bytes, prot, flags, fd_mem_out, 0);
//    if (mapped_output == MAP_FAILED) {
//        printf("essa-1 - errno(%d): %s\n", errno, std::strerror(errno));
//    }
//
//    for (size_t i = 0; i < contestInput.size(); ++i) {
//        r[i] = mapped_output[i];
//    }
//
//    close(fd_mem_in);
//    close(fd_mem_out);
//    munmap(mapped_input, input_bytes);
//    munmap(mapped_output, result_bytes);
//    if (shm_unlink("/collatz_mem_in") == -1) {
//        printf("shm_unlink_in par - errno(%d): %s\n", errno, std::strerror(errno));
//    }
//    if (shm_unlink("/collatz_mem_out") == -1) {
//        printf("shm_unlink_in par - errno(%d): %s\n", errno, std::strerror(errno));
//    }
//
//    return r;
//}
