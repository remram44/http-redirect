/* Optionnal features */
#ifndef __WIN32__
    #ifndef ENABLE_FORK
        #ifndef DISABLE_FORK
            #define ENABLE_FORK
        #endif
    #endif
#else
    #ifdef ENABLE_FORK
        #warning ENABLE_FORK is not available on Windows
        #undef ENABLE_FORK
    #endif
#endif

/* Configuration */
#ifndef MAX_PENDING_REQUESTS
    #define MAX_PENDING_REQUESTS 16
#endif

#ifndef RECV_BUFFER_SIZE
    #define RECV_BUFFER_SIZE 512
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __WIN32__
    #define _WIN32_WINNT 0x0501 /* needed for getaddrinfo(); means WinXP */
    #include <winsock2.h>
    #include <ws2tcpip.h>

    typedef int socklen_t;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/param.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <signal.h>
    #include <netdb.h>
    #include <errno.h>
    #include <unistd.h>

    typedef int SOCKET;
#endif

int setup_server(int *serv_sock, const char *addr, const char *port);
int serve(int serv_sock, const char *dest);

void pack_array(void **array, size_t size)
{
    size_t src, dest = 0;
    for(src = 0; src < size; ++src)
        if(array[src] != NULL)
            array[dest++] = array[src];
    for(; dest < size; ++dest)
        array[dest] = NULL;
}

void my_closesocket(int sock)
{
#ifdef __WIN32__
    closesocket(sock);
#else
    shutdown(sock, SHUT_RDWR);
    close(sock);
#endif
}

void print_help(FILE *f)
{
    fprintf(
            f,
            "Usage: http-redirect [options] <destination>\n"
            "\n"
            "  Starts a very simple HTTP server that will always send back 301 "
            "redirects to\n"
            "the specified destination.\n"
            "  Example:\n"
            "    http-redirect -p 80 http://www.google.com/\n"
            "\n"
            "Recognized options:\n"
            "  -h, --help: print this message and exit\n"
#ifndef ENABLE_FORK
            "  -d, --daemon: use fork() to daemonize\n"
#endif
            "  -p, --port <port>: port on which to listen\n");
}

int main(int argc, char **argv)
{
    const char *bind_addr = NULL;
    const char *port = NULL;
    const char *dest = NULL;
#ifdef ENABLE_FORK
    int daemonize = 0;
#endif

    (void)argc; /* unused */
    while(*(++argv) != NULL)
    {
        if(strcmp(*argv, "-h") == 0 || strcmp(*argv, "--help") == 0)
        {
            print_help(stdout);
            return 0;
        }
        else if(strcmp(*argv, "-b") == 0 || strcmp(*argv, "--bind") == 0)
        {
            if(bind_addr != NULL)
            {
                fprintf(stderr, "Error: --bind was passed multiple times\n");
                return 1;
            }
            if(*(++argv) == NULL)
            {
                fprintf(stderr, "Error: missing argument for --bind\n");
                return 1;
            }
            bind_addr = *argv;
        }
        else if(strcmp(*argv, "-p") == 0 || strcmp(*argv, "--port") == 0)
        {
            if(port != NULL)
            {
                fprintf(stderr, "Error: --port was passed multiple times\n");
                return 1;
            }
            if(*(++argv) == NULL)
            {
                fprintf(stderr, "Error: missing argument for --port\n");
                return 1;
            }
            port = *argv;
        }
        else if(strcmp(*argv, "-d") == 0 || strcmp(*argv, "--daemon") == 0)
        {
#ifdef ENABLE_FORK
            daemonize = 1;
#else
            fprintf(stderr, "Error: --daemon is not available\n");
            return 1;
#endif
        }
        else
        {
            if(dest != NULL)
            {
                fprintf(stderr, "Error: multiple destinations specified\n");
                return 1;
            }
            dest = *argv;
        }
    }

    if(port == NULL)
        port = "80";

    if(dest == NULL)
    {
        fprintf(stderr, "Error: no destination specified\n");
        return 1;
    }

#ifdef __WIN32__
    {
        /* Initializes WINSOCK */
        WSADATA wsa;
        if(WSAStartup(MAKEWORD(1, 1), &wsa) != 0)
        {
            fprintf(stderr, "Error: can't initialize WINSOCK\n");
            return 3;
        }
    }
#endif

    {
        int serv_sock;

        /* Poor man's exception handling... */
        int ret = setup_server(&serv_sock, bind_addr, port);
        if(ret != 0)
            return ret;

#ifdef ENABLE_FORK
        if(daemonize)
        {
            switch(fork())
            {
            case -1:
                perror("Error: fork() failed");
                break;
            case 0:
                /* child: go on... */
                fclose(stdin);
                fclose(stdout);
                fclose(stderr);
                break;
            default:
                /* parent: exit with success */
                return 0;
                break;
            }
        }
#endif

        ret = serve(serv_sock, dest);
        my_closesocket(serv_sock);
        return ret;
    }
}

int setup_server(int *serv_sock, const char *addr, const char *port)
{
    int ret;
    struct addrinfo hints, *results, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    ret = getaddrinfo(addr, port, &hints, &results);
    if(ret != 0)
    {
        fprintf(stderr, "Error: getaddrinfo failed: %s\n", gai_strerror(ret));
        return 1;
    }

    for(rp = results; rp != NULL; rp = rp->ai_next)
    {
        *serv_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(*serv_sock == -1)
            continue;

        if(bind(*serv_sock, rp->ai_addr, rp->ai_addrlen) != -1)
            break;

        my_closesocket(*serv_sock);
    }

    freeaddrinfo(results);

    if(rp == NULL)
    {
        fprintf(stderr, "Could not bind to %s:%s\n",
                ((addr == NULL)?"*":addr), port);
        *serv_sock = -1;
        return 2;
    }

    if(listen(*serv_sock, 5) == -1)
    {
        perror("Error: can't listen for incoming connections");
        return 2;
    }

    return 0;
}

char *build_redirect(const char *dest, size_t *response_size)
{
    const char *pattern =
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: %s\r\n"
            "Server: http-redirect\r\n"
            "\r\n";
    char *response;
    *response_size = strlen(pattern) - 2 + strlen(dest);
    response = malloc(*response_size + 1);
    snprintf(response, *response_size + 1, pattern, dest);

    return response;
}

struct Client {
    int sock;
    int state;
};

int serve(int serv_sock, const char *dest)
{
    size_t response_size;
    char *response_data = build_redirect(dest, &response_size);

    struct Client *connections[MAX_PENDING_REQUESTS];
    size_t i;
    for(i = 0; i < MAX_PENDING_REQUESTS; ++i)
        connections[i] = NULL;

    while(1)
    {
        int greatest;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET((SOCKET)serv_sock, &fds);
        greatest = serv_sock;

        for(i = 0; i < MAX_PENDING_REQUESTS && connections[i] != NULL; ++i)
        {
            int s = connections[i]->sock;
            FD_SET((SOCKET)s, &fds);
            if(s > greatest)
                greatest = s;
        }

        select(greatest + 1, &fds, NULL, NULL, NULL);

        if(FD_ISSET(serv_sock, &fds))
        {
            /* If all connections are taken */
            if(connections[MAX_PENDING_REQUESTS - 1] != NULL)
            {
                my_closesocket(connections[0]->sock);
                free(connections[0]);
                connections[0] = NULL;
                pack_array((void**)connections, MAX_PENDING_REQUESTS);
            }
            /* Accept the new connection */
            {
                struct sockaddr_in clientsin;
                socklen_t size = sizeof(clientsin);
                int sock = accept(serv_sock,
                                  (struct sockaddr*)&clientsin, &size);
                if(sock != -1)
                {
                    for(i = 0; i < MAX_PENDING_REQUESTS; ++i)
                        if(connections[i] == NULL)
                            break;
                    connections[i] = malloc(sizeof(struct Client));
                    connections[i]->sock = sock;
                    connections[i]->state = 0;
                }
            }
        }
        else for(i = 0; i < MAX_PENDING_REQUESTS && connections[i] != NULL; ++i)
        {
            int s = connections[i]->sock;
            if(FD_ISSET(s, &fds))
            {
                int *const state = &connections[i]->state;
                int j;
                /* Read stuff */
                static char buffer[RECV_BUFFER_SIZE];
                int len = recv(s, buffer, RECV_BUFFER_SIZE, 0);

                /* Decode HTTP request (really just wait for "\r\n\r\n") */
                for(j = 0; j < len; ++j)
                {
                    if(buffer[j] == '\r')
                    {
                        if(*state == 0 || *state == 2)
                            ++*state;
                        else
                            *state = 1;
                    }
                    else if(buffer[j] == '\n')
                    {
                        if(*state < 2)
                            *state = 2;
                        else
                        {
                            *state = 4;
                            break;
                        }
                    }
                    else
                        *state = 0;
                }
                if(*state == 4)
                    /* Print redirect */
                    send(s, response_data, response_size, 0);

                /* Client closed the connection OR request complete */
                if(len <= 0 || *state == 4)
                {
                    my_closesocket(s);
                    free(connections[i]);
                    connections[i] = NULL;
                    pack_array((void**)connections, MAX_PENDING_REQUESTS);
                }
            }
        }
    }

    free(response_data);

    return 0;
}
