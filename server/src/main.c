#include "conversion.h"
#include "copy.h"
#include "error.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <sys/stat.h>

#define MAX_PENDING 5
#define MAX_CLIENTS 10
#define DEFAULT_PORT 5000
#define BACKLOG 5
#define DIRECTORY_SIZE 100

struct options {
    char *file_name;
    char *ip_in;
    in_port_t port_in;
    int fd_in;
    char *download_path;
};


static void options_init(struct options *opts);

static void parse_arguments(int argc, char *argv[], struct options *opts);

static void options_process(struct options *opts);

static void cleanup(const struct options *opts);

static void set_signal_handling(struct sigaction *sa);

static void signal_handler(int sig);

static ssize_t error_check(ssize_t status);

static void write_file(char *clientIPAdd, char *downloads_path, int socketFD);

static ssize_t write_complete_buffer(int socketFD, char buffer[], ssize_t size, int fd);

static ssize_t read_complete_buffer(void *buffer, ssize_t size, int socketFD);

ssize_t create_download_directory(const char *filepath, mode_t mode, int fail_on_exist);


static volatile sig_atomic_t running;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)


int main(int argc, char *argv[]) {
    struct options opts;

    options_init(&opts);
    parse_arguments(argc, argv, &opts);
    options_process(&opts);

    if (opts.ip_in) {
        struct sigaction sa;

        set_signal_handling(&sa);
        running = 1;

        while (running) {
            int fd;
            struct sockaddr_in accept_addr;
            socklen_t accept_addr_len;
            char *accept_addr_str;
            in_port_t accept_port;

            accept_addr_len = sizeof(accept_addr);
            fd = accept(opts.fd_in, (struct sockaddr *) &accept_addr, &accept_addr_len);

            if (fd == -1) {
                if (errno == EINTR) {
                    break;
                }

                fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
            }

            accept_addr_str = inet_ntoa(accept_addr.sin_addr);  // NOLINT(concurrency-mt-unsafe)
            accept_port = ntohs(accept_addr.sin_port);
            printf("Accepted from IP address-> %s:%d\n", accept_addr_str, accept_port);
            write_file(accept_addr_str, opts.download_path, fd);
            printf("Closing %s:%d\n", accept_addr_str, accept_port);
            close(fd);
        }
    }

    cleanup(&opts);
    return EXIT_SUCCESS;
}


static void options_init(struct options *opts) {
    memset(opts, 0, sizeof(struct options));
    opts->fd_in = STDIN_FILENO;
    opts->port_in = DEFAULT_PORT;
    opts->download_path = malloc(DIRECTORY_SIZE);
    strcpy(opts->download_path, "receivedFiles");
}


static void parse_arguments(int argc, char *argv[], struct options *opts) {
    int c;

    while ((c = getopt(argc, argv, ":i:d:p:")) != -1)   // NOLINT(concurrency-mt-unsafe)
    {
        switch (c) {
            case 'i': {
                opts->ip_in = optarg;
                break;
            }
            case 'd': {
                opts->download_path = optarg;
                break;
            }
            case 'p': {
                opts->port_in = parse_port(optarg,
                                           10); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
                break;
            }
            case ':': {
                fatal_message(__FILE__, __func__, __LINE__, "\"Option requires an operand\"",
                              5); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
            }
            case '?': {
                fatal_message(__FILE__, __func__, __LINE__, "Unknown",
                              6); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
            }
            default: {
                assert("should not get here");
            };
        }
    }

    if (optind < argc) {
        opts->file_name = argv[optind];
    }
}


static void options_process(struct options *opts) {
    if (opts->file_name && opts->ip_in) {
        fatal_message(__FILE__, __func__, __LINE__, "Can't pass -i and a filename", 2);
    }

    if (opts->file_name) {
        opts->fd_in = open(opts->file_name, O_RDONLY);

        if (opts->fd_in == -1) {
            fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
        }
    }

    if (opts->ip_in) {
        struct sockaddr_in addr;
        int result;
        int option;

        opts->fd_in = socket(AF_INET, SOCK_STREAM, 0);

        if (opts->fd_in == -1) {
            fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
        }

        addr.sin_family = AF_INET;
        addr.sin_port = htons(opts->port_in);
        addr.sin_addr.s_addr = inet_addr(opts->ip_in);

        if (addr.sin_addr.s_addr == (in_addr_t) -1) {
            fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
        }

        option = 1;
        setsockopt(opts->fd_in, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

        result = bind(opts->fd_in, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));

        if (result == -1) {
            fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
        }

        result = listen(opts->fd_in, BACKLOG);

        if (result == -1) {
            fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
        }
    }
}


static void cleanup(const struct options *opts) {
    if (opts->file_name || opts->ip_in) {
        close(opts->fd_in);
    }
    free(opts->download_path);
}


static void set_signal_handling(struct sigaction *sa) {
    int result;

    sigemptyset(&sa->sa_mask);
    sa->sa_flags = 0;
    sa->sa_handler = signal_handler;
    result = sigaction(SIGINT, sa, NULL);

    if (result == -1) {
        fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
    }
}

static ssize_t error_check(ssize_t status) {
    if (status == -1) {
        fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
    }
    return status;
}

static void write_file(char *clientIPAdd, char *downloads_path, int socketFD) {
    uint16_t sizeOfFileName;
    char *newDownloadDir = malloc(DIRECTORY_SIZE);
    strcpy(newDownloadDir, downloads_path);
    read_complete_buffer(&sizeOfFileName, sizeof(uint16_t), socketFD);
    sizeOfFileName = ntohs(sizeOfFileName);
    char *fileName = malloc(sizeOfFileName + 1);
    read_complete_buffer(fileName, sizeOfFileName, socketFD);
    fileName[sizeOfFileName] = '\0';
    int32_t size = 0;

    read_complete_buffer(&size, sizeof(uint32_t), socketFD);

    size = ntohl(size);

    strcat(clientIPAdd, "/");
    strcat(newDownloadDir, "/");
    strcat(newDownloadDir, clientIPAdd);
    printf("Creating directory for file\n");
    printf("File name -> %s\n", fileName);
    error_check(create_download_directory(newDownloadDir, 0777, 0)); // NOLINT(cppcoreguidelines-avoid-magic-numbers)


    strcat(newDownloadDir, fileName);


    int fd = open(newDownloadDir, O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC,
                  0666); //NOLINT(hicpp-signed-bitwise) //NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers) //NOLINT(clang-analyzer-unix.Malloc) //NOLINT(clang-analyzer-unix.Malloc)

    char *buffer = malloc(size); // NOLINT(clang-diagnostic-vla)
    memset(buffer, 0, size); // NOLINT(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    write_complete_buffer(socketFD, buffer, size, fd);

    close(fd);
    free(newDownloadDir);
    free(buffer);
    printf("Exit\n");
    free(fileName);
}


static ssize_t write_complete_buffer(int socketFD, char buffer[], ssize_t size, int fd) {
    ssize_t ssize = 0;
    while (ssize < size) {
        printf("Entered loops\n");
        ssize = recv(socketFD, buffer, size, 0);
        error_check(ssize);
        printf("Size sent written file -> %ld\n", ssize);
        ssize_t wb = write(fd, buffer, ssize);
        printf("Size sent written file -> %ld\n", wb);

        memset(buffer, 0, size); // NOLINT(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        printf("Size sent to file -> %ld\n", ssize);
        if (ssize == 0) {
            break;
        }
    }
    return ssize;
}

static ssize_t read_complete_buffer(void *buffer, ssize_t size, int socketFD) {
    size_t numberOfBytesRead = 0;
    ssize_t sum = 0;
    while (numberOfBytesRead < size) {
        printf("Entered loops 1 \n");
        numberOfBytesRead = error_check(recv(socketFD, buffer, size, 0));
        sum += numberOfBytesRead;
        if (numberOfBytesRead == 0 || sum == size) {
            return numberOfBytesRead;
        }
        printf("NUmber of Bytes read -> %zd \n", numberOfBytesRead);
    }
    return numberOfBytesRead;
}

// credit to https://stackoverflow.com/questions/28873249/creating-multiple-recursive-directories-in-c
ssize_t create_download_directory(const char *path, mode_t mode, int fail_on_exist) {
    ssize_t result = 0;
    char *dir = NULL;
    struct stat st = {0};

    do {
        if (NULL == path) {
            errno = EINVAL;
            result = -1;
            break;
        }

        if ((dir = strrchr(path, '/'))) {
            *dir = '\0';
            result = create_download_directory(path, mode, fail_on_exist);
            *dir = '/';

            if (result) {
                break;
            }
        }

        if (strlen(path)) {
            if (stat(path, &st) == -1) {
                if ((result = error_check(mkdir(path, mode)))) {

                    if ((EEXIST == result) && (0 == fail_on_exist)) {
                        result = 0;
                    } else {
                        break;
                    }
                }
            }

        }
    } while (0);

    return result;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void signal_handler(int sig) {
    running = 0;
}

#pragma GCC diagnostic pop

