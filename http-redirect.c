/* Optional features */
#ifndef __WIN32__
    #ifndef ENABLE_FORK
        #ifndef DISABLE_FORK
            #define ENABLE_FORK
        #endif
    #endif
    #ifndef ENABLE_CHGUSER
        #ifndef DISABLE_CHGUSER
            #define ENABLE_CHGUSER
        #endif
    #endif
#else
    #ifdef ENABLE_FORK
        #warning ENABLE_FORK is not available on Windows
        #undef ENABLE_FORK
    #endif
    #ifdef ENABLE_CHGUSER
        #warning ENABLE_CHGUSER is not available on Windows
        #undef ENABLE_CHGUSER
    #endif
#endif
/* ENABLE_REGEX: not enabled by default */

/* Configuration */
#ifndef MAX_PENDING_REQUESTS
    #define MAX_PENDING_REQUESTS 16
#endif

#ifndef RECV_BUFFER_SIZE
    #define RECV_BUFFER_SIZE 1024
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

#ifdef ENABLE_REGEX
    #include <sys/types.h>
    #include <regex.h>
#endif

#ifdef ENABLE_CHGUSER
    #include <pwd.h>
#endif

#ifdef ENABLE_REGEX
struct regex {
    regex_t preg;
    const char *dest;
    struct regex *next;
};
#endif

int setup_server(int *serv_sock, const char *addr, const char *port);
int serve(int serv_sock, const char *dest
#ifdef ENABLE_REGEX
        , struct regex *regexes
#endif
        );

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
    fprintf(f,
            "%s%s",
#ifdef ENABLE_REGEX
            "Usage: http-redirect [options] [-r <regex1> <dest1> [...]] "
            "<default_dest>\n"
#else
            "Usage: http-redirect [options] <destination>\n"
#endif
            "\n"
            "  Starts a very simple HTTP server that will always send back 301 "
            "redirects to\n"
            "the specified destination.\n"
            "  Example:\n"
            "    http-redirect -p 80 http://www.google.com/\n"
            "\n"
            "Recognized options:\n"
            "  -h, --help: print this message and exit\n",
#ifdef ENABLE_REGEX
            "  -r, --regex <regex> <dest>: use destination if the request "
            "matches the\nregular expression. Can be specified multiple "
            "times; regex are tested in order.\nIf no regex matches, the "
            "default destination is used.\n"
            "The regex is matched against {HOST}{URI}, for example\n"
            "\"google.com/search\"\n"
            "  -e, --eregex <regex> <dest>: same thing, using extended "
            "regular expressions.\n"
#endif
#ifndef ENABLE_FORK
            "  -d, --daemon: use fork() to daemonize\n"
#endif
#ifndef ENABLE_CHGUSER
            "  -u, --user <name>: change to user after binding the socket\n"
#endif
            "  -p, --port <port>: port on which to listen\n");
}

int main(int argc, char **argv)
{
    const char *bind_addr = NULL;
    const char *port = NULL;
    const char *dest = NULL;
#ifdef ENABLE_REGEX
    struct regex *regexes = NULL, *regexes_tail = NULL; /* not free'd */
#endif
#ifdef ENABLE_FORK
    int daemonize = 0;
#endif
#ifdef ENABLE_CHGUSER
    const char *user = NULL;
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
        else if(strcmp(*argv, "-r") == 0 || strcmp(*argv, "--regex") == 0
             || strcmp(*argv, "-e") == 0 || strcmp(*argv, "--eregex") == 0)
        {
#ifdef ENABLE_REGEX
            struct regex *new_reg = malloc(sizeof(struct regex));
            char extended = ((*argv)[1] == '-')?
                    ((*argv)[2] == 'e'):
                    ((*argv)[1] == 'e');
            int res;
            if(*(++argv) == NULL)
            {
                fprintf(stderr,
                        "Error: missing regular expression for --(e)regex\n");
                return 1;
            }
            res = regcomp(
                    &new_reg->preg,
                    *argv,
                    REG_NOSUB | (extended?REG_EXTENDED:0));
            if(res != 0)
            {
                size_t errsize = regerror(res, &new_reg->preg, NULL, 0);
                char *err = malloc(errsize); /* not free'd */
                regerror(res, &new_reg->preg, err, errsize);
                fprintf(stderr, "Error compiling regular expression: %s\n",
                        err);
                return 2;
            }
            if(*(++argv) == NULL)
            {
                fprintf(stderr, "Error: missing destination for --regex\n");
                return 1;
            }
            new_reg->dest = *argv;
            if(regexes_tail != NULL)
                regexes_tail->next = new_reg;
            else
                regexes = new_reg;
            new_reg->next = NULL;
            regexes_tail = new_reg;
#else
            fprintf(stderr, "Error: --(e)regex is not available\n");
            return 1;
#endif
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
        else if(strcmp(*argv, "-u") == 0 || strcmp(*argv, "--user") == 0)
        {
#ifdef ENABLE_CHGUSER
            if(user != NULL)
            {
                fprintf(stderr, "Error: --user was passed multiple times\n");
                return 1;
            }
            if(*(++argv) == NULL)
            {
                fprintf(stderr, "Error: missing argument for --user\n");
                return 1;
            }
            user = *argv;
#else
            fprintf(stderr, "Error: --user is not available\n");
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

#ifdef ENABLE_CHGUSER
        if(user != NULL)
        {
            struct passwd *pwd = getpwnam(user);
            if(pwd == NULL)
            {
                fprintf(stderr, "Error: user %s is unknown\n", user);
                return 2;
            }
            if(setresuid(pwd->pw_uid, pwd->pw_uid, pwd->pw_uid) == -1)
            {
                fprintf(stderr, "Error: can't change user to %s\n", user);
                return 2;
            }
        }
#endif

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

        ret = serve(
                serv_sock,
                dest
#ifdef ENABLE_REGEX
                , regexes
#endif
                );
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

void send_redirect(int sock, const char *dest)
{
    send(
            sock,
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: ",
            42,
            0);
    send(sock, dest, strlen(dest), 0);
    send(
            sock,
            "\r\n"
            "Server: http-redirect\r\n"
            "\r\n",
            27,
            0);
}

void send_http_error(int sock)
{
    send(
            sock,
            "HTTP/1.1 400 Bad Request\r\n"
            "Server: http-redirect\r\n"
            "\r\n",
            51,
            0);
}

struct Client {
    int sock;
#ifdef ENABLE_REGEX
    char buffer[RECV_BUFFER_SIZE];
    int state;
    /*    http request      host header       end
     * 0 --------------> 1 --------------> 2 -----> 3
     */
    size_t bufsize;
    char uri[RECV_BUFFER_SIZE];
    char host[RECV_BUFFER_SIZE];
#else
    /* We don't actually read anything, we just wait for the end of the header
     * to send the precomputed reply */
    int state;
    /* State machine:
     *    \r      \n      \r      \n
     * 0 ----> 1 ----> 2 ----> 3 ----> 4
     *  \             ^ \             ^
     *   \-----------/   \-----------/
     *        \n              \n
     */
#endif
};

int serve(int serv_sock, const char *dest
#ifdef ENABLE_REGEX
        , struct regex *regexes
#endif
        )
{
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
#ifdef ENABLE_REGEX
                    connections[i]->state = 0;
                    connections[i]->bufsize = 0;
                    connections[i]->uri[0] = '\0';
                    connections[i]->host[0] = '\0';
#else
                    connections[i]->state = 0;
#endif
                }
            }
        }
        else for(i = 0; i < MAX_PENDING_REQUESTS && connections[i] != NULL; ++i)
        {
            int s = connections[i]->sock;
            if(FD_ISSET(s, &fds))
            {
                char done = 0;
#ifdef ENABLE_REGEX
                struct Client *const c = connections[i];
                size_t j;
                int len = recv(
                        s,
                        c->buffer + c->bufsize,
                        RECV_BUFFER_SIZE - c->bufsize,
                        0);
                j = c->bufsize;
                if(len <= 0)
                    done = 1;
                else
                    c->bufsize += len;
                while(j < c->bufsize && !done)
                {
                    if(c->buffer[j] == '\n')
                    {
                        size_t skip = 0;
                        if(j > 0 && c->buffer[j-1] == '\r')
                            c->buffer[j - 1] = '\0';
                        else
                            c->buffer[j] = '\0';
                        if(strncmp(c->buffer, "GET ", skip = 4) == 0
                        || strncmp(c->buffer, "POST ", skip = 5) == 0
                        || strncmp(c->buffer, "HEAD ", skip = 5) == 0)
                        {
                            if(c->state != 0)
                            {
                                send_http_error(s);
                                done = 1;
                            }
                            else
                            {
                                const char *uri_start = c->buffer + skip;
                                const char *uri_end = strchr(uri_start, ' ');
                                if(uri_end == NULL)
                                {
                                    send_http_error(s);
                                    done = 1;
                                }
                                else
                                {
                                    size_t uri_len = uri_end - uri_start;
                                    memcpy(c->uri, uri_start, uri_len);
                                    c->uri[uri_len] = '\0';
                                    c->state = 1;
                                }
                            }
                        }
                        else if(strncmp(c->buffer, "Host: ", 6) == 0)
                        {
                            if(c->state != 1)
                            {
                                send_http_error(s);
                                done = 1;
                            }
                            else
                            {
                                strcpy(c->host, c->buffer + 6);
                                c->state = 2;
                            }
                        }
                        else if(c->buffer[0] == '\0') /* empty line */
                        {
                            if(c->state == 0)
                            {
                                send_http_error(s);
                                done = 1;
                            }
                            else
                                c->state = 3;
                        }

                        /* Remove the line we just read */
                        ++j;
                        c->bufsize = c->bufsize - j;
                        {
                            size_t i;
                            for(i = 0; i < c->bufsize; ++i)
                                c->buffer[i] = c->buffer[j + i];
                        }
                        j = 0;
                    }
                    else
                       ++j;
                }

                if(c->state == 3)
                {
                    struct regex *r;
                    char request[RECV_BUFFER_SIZE * 2];
                    strcpy(request, c->host);
                    strcat(request, c->uri);
                    for(r = regexes; r != NULL; r = r->next)
                    {
                        fprintf(stderr, "regexec...\n");
                        if(regexec(&r->preg, request, 0, NULL, 0) == 0)
                        {
                            /* Print redirect */
                            send_redirect(s, r->dest);
                            done = 1;
                        }
                    }
                    if(!done)
                        send_redirect(s, dest); /* default */
                    done = 1;
                }
                else if(c->bufsize >= RECV_BUFFER_SIZE)
                {
                    /* Overflow */
                    send_http_error(s);
                    done = 1;
                }
#else
                /* Just wait for "\r\n\r\n" */
                int *const state = &connections[i]->state;
                size_t j;
                /* Read stuff */
                static char buffer[RECV_BUFFER_SIZE];
                int len = recv(s, buffer, RECV_BUFFER_SIZE, 0);
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
                {
                    /* Print redirect */
                    send_redirect(s, dest);
                    done = 1;
                }
#endif

                /* Client closed the connection OR request complete */
                if(len <= 0 || done)
                {
                    my_closesocket(s);
                    free(connections[i]);
                    connections[i] = NULL;
                    pack_array((void**)connections, MAX_PENDING_REQUESTS);
                }
            }
        }
    }

    return 0;
}
