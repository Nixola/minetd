#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <limits.h>

#define PORT_LENGTH_MAX 5
#define DOMAIN_LENGTH_MAX 253
#define LINE_LENGTH (DOMAIN_LENGTH_MAX + PORT_LENGTH_MAX + 2)

//Constants and global variable declaration goes here
struct Entry *config;
int lines;
fd_set rfd_set_master;

//Service structure definition goes here

struct Entry {
    // Values to be read from the config file
    unsigned short port;
    char *hostname;
    struct Entry *next;
};

//Function prototype devoted to handle the death of child processes, as well as reloading the config file.
void handle_signal (int sig);

// Look through sensible paths for a readable configuration file. Does not check for validity.
FILE *get_config_file(char **path);

//Function prototype to read the configuration file. Returns the amount of valid lines/services.
int read_config();

void free_config();

//Function prototype to create the necessary sockets for the various services. last will be filled with the highest FD.
fd_set create_socket(short port, int *last);

//Function prototype, mostly debug-related, to print an entry's data.
void print_entry(struct Entry* e);

// Return a valid port number, or 0
unsigned short str_to_port(char *prt);

// Read a minecraft VarInt from a byte stream
int read_varint(char *head, size_t length, char **out_head);

unsigned short str_to_port(char *prt) {
    char *tmp;
    long n = strtol(prt, &tmp, 10);

    if (n > 65535 || n < 1 || prt == tmp) {
        // port is outside limits or not a valid number
        return 0;
    }
    return (unsigned short) n;
}

int read_varint(char *head, size_t length, char **out_head) {
    int result = 0;
    size_t position = 0;
    if (out_head != NULL) *out_head = head;

    while (*(head + position) & 0x80) {
        result = result | ( (*(head + position) & 0x7F) << (7 * position));
        if (++position > length) {
            return 0;
        }
    }
    result = result + (*(head + position) << (7 * position));
    if (out_head != NULL) {
        *out_head = head + position + 1;
    }
    return result;
}

FILE *get_config_file(char **out_path) {
    char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    char *home = getenv("HOME");
    FILE *file;
    char *path = "./minetd.conf";
    size_t path_size;
    const char *path_suffix = "/minetd/minetd.conf";
    int free_xdg = 0;

    // try all files in order; this'll be fun
    file = fopen(path, "r");
    if (file != NULL) {
        if (out_path != NULL) *out_path = path;
        return file;
    }

    if ((xdg_config_home == NULL || strlen(xdg_config_home) == 0) && (home != NULL && strlen(home) > 0)) {
        path_size = strlen(home) + strlen("/.config") + 1;
        xdg_config_home = malloc(path_size);
        free_xdg = 1;
        snprintf(xdg_config_home, path_size, "%s/.config", home);
    } else {
        xdg_config_home = NULL;
    }

    path_size = strlen(xdg_config_home) + strlen(path_suffix) + 1;
    path = malloc(path_size);
    snprintf(path, path_size, "%s%s", xdg_config_home, path_suffix);

    // try all files in order; this'll be fun
    file = fopen(path, "r");
    if (file != NULL) {
        if (out_path != NULL) *out_path = path;
        return file;
    }
    free(path);
    if (free_xdg) free(xdg_config_home);

    path = "/etc/minetd/minetd.conf";
    // try all files in order; this'll be fun
    file = fopen(path, "r");
    if (file != NULL) {
        if (out_path != NULL) *out_path = path;
        return file;
    }

    return NULL;
}

int read_config() {
    FILE *file = get_config_file(NULL);
    if (file == NULL) return 0;
    int i = 0;
    //while there are lines left
    struct Entry *config_head;
    if (config != NULL) {
        free_config();
    }
    while (!feof(file)) {
        struct Entry *entry;
        // Valid domain names cannot be longer than 253 characters, ports cannot be longer than 5 characters.
        char line[LINE_LENGTH];
        unsigned int domain_length;
        char *port;
        long port_number;

        *line = '\0';
        fgets(line, LINE_LENGTH, file);

        //Find the last space (not the first, so that stuff can be aligned)
        port = strrchr(line, ' ');
        domain_length = (int) (strchr(line, ' ') - line);

        if (strlen(line) <= 1 || !strncmp(line, "#", 1) || !strncmp(line, "//", 2) || (port == NULL) || (domain_length > DOMAIN_LENGTH_MAX)) {
            // empty line or comment, ignore
            continue;
        }

        entry = malloc(sizeof(*entry));
        if (config == NULL) {
            config = entry;
        } else {
            config_head->next = entry;
        }

        config_head = entry;
        entry->next = NULL;
        entry->hostname = malloc(domain_length + 1);
        entry->hostname[domain_length] = '\0';
        strncpy(entry->hostname, line, domain_length);

        port = port + 1;

        port_number = str_to_port(port);
        
        if (port_number == 0) {
            // invalid characters found, or invalid port
            free(entry->hostname);
            free(entry);
            continue;
        }

        entry->port = port_number;

        printf("Read entry: ");
        print_entry(entry);
        i++;
    }
    fclose(file);
    return i;
}

void free_config() {
    if (config != NULL) {
        struct Entry *head = config;
        while (head != NULL) {
            struct Entry *cur = head;
            free(head->hostname);
            head = head->next;
            free(cur);
        }
        config = NULL;
    }
}

fd_set create_socket(short port, int *last) {
    fd_set rfd_set;
    FD_ZERO(&rfd_set);

    //struct Entry *line = config[i];
    struct sockaddr_in serverAddress;
    int domain = AF_INET;
    int type = SOCK_STREAM;
    int protocol = IPPROTO_TCP;
    int fd = socket(domain, type, protocol);

    //Create and fill an address
    serverAddress.sin_family = domain;
    serverAddress.sin_port = htons(port);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    //And bind a socket to that address
    if (bind(fd, (struct sockaddr*) &serverAddress, sizeof(serverAddress))) {
        perror("Bind failed");
        exit(-2);
    }

    if (listen(fd, 5)) {
        perror("Listen failed");
        exit(-2);
    }
    //And add it to the FD set
    FD_SET(fd, &rfd_set);
    //"Return" the FD
    *last = fd;

    return rfd_set;
}

void print_entry(struct Entry* e) {
    printf("%s: %d\n", e->hostname, e->port);
}

int main(int argc,char **argv){
    fd_set rfd_set;
    unsigned short port;

    if (argc != 2) {
        printf("Usage: %s <port number>\n", *argv);
        return 0;
    }

    port = str_to_port(argv[1]);
    if (port == 0) {
        fprintf(stderr, "Invalid port\n");
        return -2;
    }

    // Read the configuration file
    lines = read_config();

    if (!lines) {
        fprintf(stderr, "Config file absent or empty\n");
        return -3;
    }

    int master_socket;
    rfd_set = create_socket(port, &master_socket);
    rfd_set_master = rfd_set;

    signal(SIGCHLD,handle_signal); /* Handle signals sent by son processes - call this function when it's ought to be */
    signal(SIGHUP, handle_signal);
    while (1) {
        //update the local working FD set
        rfd_set = rfd_set_master;
        //Select available sockets
        int selRet = select(master_socket + 1, &rfd_set, NULL, NULL, NULL);//&timeout);
        if (selRet == -1) {
            int err = errno;
            switch(err) {
            case EINTR:
                // Interrupted!
                continue;
                break;
            default:
                // Error!
                fprintf(stderr, "Select failed! %s\n", strerror(err));
                exit(-1);
                break;
            }
        }
        int fdClient = accept(master_socket, NULL, NULL);
        pid_t child = fork();
        //If this is the child
        if (!child) {
            //Close access to parent's sockets
            close(master_socket);
            char *buffer = malloc(5);
            recv(fdClient, buffer, 5, MSG_PEEK);
            int packet_length = read_varint(buffer, 5, NULL);
            free(buffer);
            if (!packet_length) {
                // However long the rest of the packet is, it's supposed to be >0
                return 0;
            }

            buffer = malloc(packet_length);
            recv(fdClient, buffer, packet_length, MSG_PEEK);
            char *buffer_end = buffer + packet_length;
            char *head = buffer;
            char *tmp;

            read_varint(head, buffer_end-head, &tmp); // skip the packet length, don't need that
            if (head == tmp) {
                // Not a valid Minecraft packet
                return 0;
            }
            head = tmp;
            read_varint(head, buffer_end-head, &tmp); // skip the packet id, don't need that
            if (head == tmp) {
                // Not a valid Minecraft packet
                return 0;
            }
            head = tmp;
            read_varint(head, buffer_end-head, &tmp); // skip the protocol version, don't need that
            if (head == tmp) {
                // Not a valid Minecraft packet
                return 0;
            }
            head = tmp;
            
            char length = *head;
            char *host = malloc(length+1);
            unsigned short port;
            host[length] = '\0';
            if (host[length-1] == '.') { // Minecraft sends out a FQDN when resolving a SRV record
                host[length-1] = '\0';
            }
            strncpy(host, head+1, length);
            int found = 0;
            free(buffer);
            struct Entry *entry = config;
            while (entry != NULL) {
                if (!strcmp(entry->hostname, host)) {
                    found = 1;
                    port = entry->port;
                    printf("Found port %d for host %s\n", entry->port, entry->hostname);
                    break;
                }
                entry = entry->next;
            }
            if (!found) {
                printf("Nothing found for host %s\n", host);
                free(host);
                close(fdClient);
                return 0;
            }
            free(host);
            //And substitute our own
            dup2(fdClient,0);
            dup2(fdClient,1);
            dup2(fdClient,2);
            //And run the server
            char *arg2 = malloc(21);
            snprintf(arg2, 20, "TCP:127.0.0.1:%d", port);
            execlp("socat", "socat", "STDIO", arg2, NULL);
        }

        //If this is the parent
        close(fdClient);
    }
    return 0;
}

// handle_signal implementation
void handle_signal (int sig) {
    // Call to wait system-call goes here
    //printf("Something's off...\n");

    switch (sig) {
        case SIGCHLD : ;
            // Implementation of SIGCHLD handling goes here
            //fprintf(stderr, "A child died today.\n");
            pid_t deadChild = wait(NULL);
            //fprintf(stderr, "Mourn %u's death\n", deadChild);

            // Replace the signal handler, since it is reset to default after being called.
    		signal(SIGCHLD, handle_signal);
            break;
        case SIGHUP : ;
            lines = read_config();

            if (!lines) {
                fprintf(stderr, "Config file absent or empty\n");
                exit(-3);
            }
            signal(SIGHUP, handle_signal);
            break;
        default : //printf ("Signal not known!\n");
            break;
    }
}
