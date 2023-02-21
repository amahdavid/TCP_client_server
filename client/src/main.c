#include "conversion.h"
#include "copy.h"
#include "error.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define DEFAULT_PORT 5000

struct options {
    char *file_name;
    char *ip_out;
    in_port_t port_out;
    int fd_in;
    int fd_out;
};


static void options_init(struct options *opts);

static void parse_arguments(int argc, char *argv[], struct options *opts);

static void options_process(struct options *opts);

static void sendfile(int socket_fd, char *filename);

static ssize_t error_check(ssize_t status);

static void cleanup(const struct options *opts);


int main(int argc, char *argv[]) {
    struct options opts;

    options_init(&opts);
    parse_arguments(argc, argv, &opts);

    cleanup(&opts);

    return EXIT_SUCCESS;
}

/**
 * this function initializes the options struct by setting all of its values to zero using memset
 * setting the input file descriptor to STDIN_FILENO
 * setting the output file descriptor to STDIN_FILENO
 * setting the output port to the default port
 * */
static void options_init(struct options *opts) {
    memset(opts, 0, sizeof(struct options));
    opts->fd_in = STDIN_FILENO;
    opts->fd_out = STDOUT_FILENO;
    opts->port_out = DEFAULT_PORT;
}

/**
 * parses command line arguments passed to the program
 * @param argc the number of arguments passed to the program
 * @param argc the actual arguments passed to the program
 * @param opts a pointer to the struct options
 * */
static void parse_arguments(int argc, char *argv[], struct options *opts) {
    int c;

    /*function uses getopt to parse the command line arguments*/
    while ((c = getopt(argc, argv, ":s:p:")) != -1)   // NOLINT(concurrency-mt-unsafe)
    {
        switch (c) {
            /*this specifies the IP address to point to*/
            case 's': {
                opts->ip_out = optarg;
                break;
            }
                /*this specifies the port to listen on*/
            case 'p': {
                opts->port_out = parse_port(optarg,
                                            10); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
                break;
            }
            case ':': {
                fatal_message(__FILE__, __func__, __LINE__, "\"Option requires an operand\"",
                              5); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
                break;
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

    /*this iterates through the options passed, if it finds an option it performs and action based
     on the option*/
    while (argc > optind) {
        opts->file_name = argv[optind];
        options_process(opts);
        argc = argc + 1 - 1;
        optind++;
    }
}

/**
 * this function processes the specified options
 * */
static void options_process(struct options *opts) {
    // checks if a file name was specified, if so it opens the file and assigns the fd to the fd_in field
    if (opts->file_name) {
        opts->fd_in = open(opts->file_name, O_RDONLY);

        if (opts->fd_in == -1) {
            fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
        }
    }

    // if the ip_out is not null it creates a socket, sets the address and port info for the socket
    // and connects to the address
    if (opts->ip_out) {
        int result;
        struct sockaddr_in addr;

        opts->fd_out = socket(AF_INET, SOCK_STREAM, 0);

        if (opts->fd_out == -1) {
            fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
        }

        addr.sin_family = AF_INET;
        addr.sin_port = htons(opts->port_out);
        addr.sin_addr.s_addr = inet_addr(opts->ip_out);

        if (addr.sin_addr.s_addr == (in_addr_t) -1) {
            fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
        }

        result = connect(opts->fd_out, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));

        if (result == -1) {
            fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
        }
        sendfile(opts->fd_out, opts->file_name);
    }
}

/**
 * this function appears to send a file over a socket connection
 * it starts by opening the file specified by the given filename and obtaining info about it using the stat function
 * it then sends the size of the filename followed by the filename itself over the socket connection, using the write function
 * it also sends the file size using the write function
 * */
static void sendfile(int socket_fd, char *filename) {
    struct stat statVar;
    printf("Obtaining file\n");

    ssize_t fd = error_check(open(filename, O_RDONLY | O_CLOEXEC));
    stat(filename, &statVar);

    printf("Sending size of file name\n");
    size_t realSize = strlen(filename);

    uint16_t filename_size = htons(realSize);
    write(socket_fd, &filename_size, sizeof(uint16_t));

    printf("Size of file name %lu\n", realSize);
    printf("sending filename\n");

    write(socket_fd, filename, realSize);
    printf("%s \n", filename);

    printf("sending size of file %ld \n", statVar.st_size);
    uint32_t fileSIze = htonl(statVar.st_size);

    ssize_t send = write(socket_fd, &fileSIze, sizeof(uint32_t));
    printf("size sent %ld \n", send);

    copy(fd, socket_fd, statVar.st_size);
}

static ssize_t error_check(ssize_t status) {
    if (status == -1) {
        fatal_errno(__FILE__, __func__, __LINE__, errno, 2);
    }
    return status;
}


static void cleanup(const struct options *opts) {
    if (opts->file_name) {
        close(opts->fd_in);
    }

    if (opts->ip_out) {
        close(opts->fd_out);
    }
}
