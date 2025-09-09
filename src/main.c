#include "common.h"
#include "dynamic_array.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 5555
#define HOST "localhost"
#define MAX_LENGTH 2056

#define K_MSG (32 << 20)

typedef enum
{
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_NIL,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_ARRAY,
    TYPE_ERROR,
} ValueType;

typedef enum
{
    ERR_UNKNOWN,
    ERR_TO_BIG
} ErrorType;

#define buf_append(buff, data, size)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        for (int i = 0; i < (size); ++i)                                                                               \
        {                                                                                                              \
            arr_push((buff), ((char *)(data))[i]);                                                                     \
        }                                                                                                              \
    } while (0);

#define buf_consume(arr, n)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        for (int i = 0; i < n; ++i)                                                                                    \
        {                                                                                                              \
            pop_front((arr));                                                                                          \
        }                                                                                                              \
    } while (0);

int write_all(int fd, int8 *buff, size_t n)
{
    while (n > 0)
    {
        int writed = write(fd, buff, n);
        if (writed <= 0)
        {
            return -1;
        }
        n -= writed;
        buff += writed;
    }
    return 0;
}

int read_all(int fd, char *buff, size_t n)
{
    while (n > 0)
    {
        int readed = read(fd, buff, n);
        if (readed <= 0)
        {
            return -1;
        }
        n -= readed;
        buff += readed;
    }
    return 0;
}

int read_response(int fd, char *buff, int *len)
{
    errno = 0;
    if (read_all(fd, buff, 4) == -1)
    {
        printf(errno == 0 ? "eof\n" : "read()\n");
        if (errno != 0)
            return -1;
    }

    memcpy(len, buff, 4);

    if (*len > K_MSG)
    {
        fprintf(stderr, "Message to long\n");
        return -1;
    }

    errno = 0;
    if (read_all(fd, buff, *len) == -1)
    {
        printf(errno == 0 ? "eof\n" : "read()\n");
        if (errno != 0)
            return -1;
    }
    return 0;
}

int write_response(int fd, char *msg)
{
    int len = {0};

    len = strlen(msg);
    assert(len <= K_MSG);

    int8 *buff = NULL;
    arr_init(buff);

    buf_append(buff, &len, 4);
    buf_append(buff, msg, len);

    if (write_all(fd, buff, 4) == -1)
    {
        assert(0 && "write_all()");
    }
    if (write_all(fd, &buff[4], len) == -1)
    {
        assert(0 && "write_all()");
    }

    return 0;
}

#define consume(data, buff, idx, n)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        memcpy(data, &buff[idx], n);                                                                                   \
        idx += n;                                                                                                      \
    } while (0);

static int display_response(char *buff)
{
    int idx = 0;

    switch (buff[idx++])
    {
    case TYPE_INT: {
        unsigned long val = {0};
        consume(&val, buff, idx, 8);
        printf("%lu", val);
        break;
    }
    case TYPE_DOUBLE: {
        double dv = {0};
        consume(&dv, buff, idx, 8);
        printf("%lf", dv);
        break;
    }
    case TYPE_STRING: {
        size_t sz = {0};
        consume(&sz, buff, idx, 4);

        char *str = malloc(sz);
        consume(str, buff, idx, sz);
        printf("%.*s", (int)sz, str);

        free(str);
        break;
    }

    case TYPE_ARRAY: {
        size_t sz = {0};
        consume(&sz, buff, idx, 4);
        printf("[");
        for (size_t i = 0; i < sz; ++i)
        {
            if (i != 0)
                printf(",");
            int last_idx = display_response(&buff[idx]);
            idx += last_idx;
        }
        printf("]");
        break;
    }

    case TYPE_NIL: {
        printf("<nil>");
        break;
    }

    case TYPE_ERROR: {
        ErrorType err_type;
        consume(&err_type, buff, idx, 4);

        size_t sz = {0};
        consume(&sz, buff, idx, 4);

        char *str = malloc(sz);
        consume(str, buff, idx, sz);
        printf("Error : %.*s", (int)sz, str);

        free(str);
        break;
    }
    default:
        assert(0 && "Unexpected in : display_response");
        break;
    }
    return idx;
}

void process_response(int fd)
{
    char buff[MAX_LENGTH + 4] = {0};
    int len = {0};
    if (read_response(fd, buff, &len) == -1)
    {
        return;
    };

    // printf("Retrieved final_size : %d\n", len);

    display_response(buff);
    printf("\n");
}

void send_request(int fd, char **data, int len)
{
    // request = [n, payload]
    // payload = [nstring, nchar, @@@ nchar, @@@]

    int8 *buffer = NULL;
    arr_init(buffer);

    int total_len = {0};
    int nstring = {0};
    buf_append(buffer, &total_len, 4);
    buf_append(buffer, &nstring, 4);

    for (int i = 0; i < len; ++i)
    {
        char *curr = data[i];
        int char_len = strlen(curr);
        buf_append(buffer, &char_len, 4);
        buf_append(buffer, curr, char_len);
        ++nstring;
    }

    total_len = arr_len(buffer);

    memcpy(buffer, &total_len, 4);
    memcpy(&buffer[4], &nstring, 4);

    if (write_all(fd, buffer, 4) == -1)
    {
        assert(0 && "write_all()");
    }
    if (write_all(fd, &buffer[4], total_len) == -1)
    {
        assert(0 && "write_all()");
    }
}

void setup_connection(int *fd, int *conn_fd)
{
    *fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*fd < 0)
    {
        assert(0 && "socket()");
    };

    printf("Connecting to port %d...\n", PORT);

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = ntohs(PORT);
    server_addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);

    *conn_fd = connect(*fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (*conn_fd < 0)
    {
        assert(0 && "error : connect()");
    }

    printf("Connected to %d\n", PORT);
}

void create_connection(int id)
{
    (void)id;
}


int start = 0;
char *read_line (char *content) {
    int end = start;
    while(content[end] != '\n' && content[end] != '\0') {
        ++end;
    }
    int len = end - start;

    if (content[end] == '\0') return NULL;

    // abc\n efg\n
    char *line = (char *)malloc(len + 1);
    memcpy(line, &content[start], len);
    line[len] = '\0';

    start = end + 1;
    return line;
}

char *read_file(const char *file_path) {
    FILE *fd = fopen(file_path, "r");
    if (fd == NULL)
    {
        fprintf(stderr, "Cannot open the file\n");
        exit(60);
    }

    fseek(fd, 0L, SEEK_END);
    size_t file_size = ftell(fd);
    rewind(fd);

    char *buff = malloc(file_size + 1);
    if (buff == NULL)
    {
        fprintf(stderr, "Not enough memory to read\n");
        exit(74);
    }

    size_t bytes_read = fread(buff, sizeof(char), file_size, fd);
    if (bytes_read < file_size)
    {
        fprintf(stderr, "Failed to read the file\n");
        exit(60);
    }

    buff[bytes_read] = '\0';
    fclose(fd);
    return buff;
}

int main(int argc, char * argv[])
{
    int fd, conn_fd;
    setup_connection(&fd, &conn_fd);

    if (argc == 1) {
        bool s_cont = true;
        char line[1024];
        while (s_cont)
        {
            printf("> ");
            memset(line, 0, sizeof(line));
            // [GET, ID]
            if (!fgets(line, sizeof(line), stdin))
                break;

            if (line[0] == '\n')
                break;

            char **input = NULL;
            arr_init(input);

            char *pch = NULL;
            pch = strtok(line, " \n");
            while (pch != NULL)
            {
                arr_push(input, pch);
                pch = strtok(NULL, " \n");
            }

            // printf("Length : %d\n", arr_len(input));
            // for(int i=0;i<arr_len(input); ++i) {
            //     printf("%s,", input[i]);
            // }
            // printf("\n");


            send_request(fd, input, arr_len(input));
            process_response(fd);
        }

    }

    else if (argc == 2){
        char *content = read_file(argv[1]);
        char *line = NULL;
        while ((line = read_line(content)) != NULL) {
            char **input = NULL;
            arr_init(input);

            size_t len = strlen(line);
            if (len > 0) {
                char *pch = NULL;
                pch = strtok(line, " \n");
                while (pch != NULL)
                {
                    arr_push(input, pch);
                    pch = strtok(NULL, " \n");
                }
                send_request(fd, input, arr_len(input));
                process_response(fd);
            }
        }

    }

    close(conn_fd);
    close(fd);

    return EXIT_SUCCESS;
}
