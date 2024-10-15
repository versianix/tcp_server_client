#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 4096
#define QUEUE_SIZE 10

// Estrutura para armazenar o último acesso de cada cliente
struct ClientAccess
{
    char client_ip[INET_ADDRSTRLEN];  // Endereço IP do cliente
    time_t last_access;               // Timestamp do último acesso
    struct ClientAccess *next;        // Próximo cliente na lista
};

struct ClientAccess *client_list = NULL;  // Cabeça da lista de acessos
pthread_mutex_t access_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex para sincronizar o acesso à lista

// Função para encontrar um cliente pelo IP
struct ClientAccess *find_client(const char *client_ip)
{
    struct ClientAccess *current = client_list;
    while (current != NULL)
    {
        if (strcmp(current->client_ip, client_ip) == 0)
        {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Função para atualizar ou adicionar o último acesso de um cliente
void update_client_access(const char *client_ip, time_t last_access)
{
    pthread_mutex_lock(&access_mutex);

    struct ClientAccess *client = find_client(client_ip);
    if (client != NULL)
    {
        client->last_access = last_access;  // Atualizar o último acesso
    }
    else
    {
        struct ClientAccess *new_client = (struct ClientAccess *)malloc(sizeof(struct ClientAccess));
        strcpy(new_client->client_ip, client_ip);
        new_client->last_access = last_access;
        new_client->next = client_list;
        client_list = new_client;  // Adicionar novo cliente à lista
    }

    pthread_mutex_unlock(&access_mutex);
}

// Função para obter o último acesso de um cliente
time_t get_last_access(const char *client_ip)
{
    pthread_mutex_lock(&access_mutex);

    struct ClientAccess *client = find_client(client_ip);
    time_t last_access = client ? client->last_access : 0;

    pthread_mutex_unlock(&access_mutex);
    return last_access;
}

// Função para liberar a lista de acessos dos clientes
void free_client_list(void)
{
    pthread_mutex_lock(&access_mutex);

    struct ClientAccess *current = client_list;
    struct ClientAccess *next;
    while (current != NULL)
    {
        next = current->next;
        free(current);
        current = next;
    }
    client_list = NULL;

    pthread_mutex_unlock(&access_mutex);
}

// Função para lidar com a requisição do cliente
void *handle_request(void *arg)
{
    int client_socket = *(int *)arg;
    char buffer[BUFFER_SIZE];
    int file_descriptor, bytes_read;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    time_t current_time;
    char client_ip[INET_ADDRSTRLEN];

    free(arg);

    // Obter o endereço IP do cliente
    getpeername(client_socket, (struct sockaddr *)&client_addr, &client_len);
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

    // Ler o comando enviado pelo cliente
    read(client_socket, buffer, BUFFER_SIZE);

    // Dividir o comando em partes
    char command[50], address[INET_ADDRSTRLEN], filename[BUFFER_SIZE];
    sscanf(buffer, "%s %s %s", command, address, filename);

    // Obter o último acesso antes de processar a nova requisição
    time_t last_access = get_last_access(client_ip);

    if (strcmp(command, "MyGet") == 0)
    {
        printf("Cliente %s solicitou o arquivo: '%s'\n", client_ip, filename);

        file_descriptor = open(filename, O_RDONLY);
        if (file_descriptor < 0)
        {
            perror("Erro ao abrir o arquivo");
            snprintf(buffer, BUFFER_SIZE, "Erro ao abrir o arquivo: '%s'\n", strerror(errno));
            write(client_socket, buffer, strlen(buffer));
            close(client_socket);
            return NULL;
        }

        // Enviar uma mensagem inicial sobre o conteúdo do arquivo
        snprintf(buffer, BUFFER_SIZE, "Conteúdo do arquivo '%s':\n", filename);
        write(client_socket, buffer, strlen(buffer));

        // Enviar o conteúdo do arquivo para o cliente
        while ((bytes_read = read(file_descriptor, buffer, BUFFER_SIZE)) > 0)
        {
            write(client_socket, buffer, bytes_read);
        }
        close(file_descriptor);

        printf("Arquivo '%s' enviado com sucesso para o cliente %s.\n", filename, client_ip);

        // Atualizar o timestamp após processar MyGet
        time(&current_time);
        update_client_access(client_ip, current_time);
    }
    else if (strcmp(command, "MyLastAccess") == 0)
    {
        printf("Cliente %s solicitou o último acesso.\n", client_ip);

        // Verificar o último acesso antes de atualizar o timestamp
        if (last_access != 0)
        {
            char time_buffer[BUFFER_SIZE];
            strftime(time_buffer, BUFFER_SIZE, "Last Access = %Y-%m-%d %H:%M:%S.\n", localtime(&last_access));
            write(client_socket, time_buffer, strlen(time_buffer));
        }
        else
        {
            write(client_socket, "Last Access = Null.\n", 21);
        }

        // Atualizar o timestamp após processar MyLastAccess
        time(&current_time);
        update_client_access(client_ip, current_time);
    }
    else
    {
        printf("Comando não reconhecido do cliente %s: %s\n", client_ip, command);
        snprintf(buffer, BUFFER_SIZE, "Comando '%s' não reconhecido.\n", command);
        write(client_socket, buffer, strlen(buffer));

        // Atualizar o timestamp para comandos inválidos também
        time(&current_time);
        update_client_access(client_ip, current_time);
    }

    close(client_socket);
    return NULL;
}

// Função para encerrar o servidor de forma segura
void signal_handler(int signal)
{
    (void) signal;

    printf("\nEncerrando o servidor...\n");

    // Liberar a lista de acessos dos clientes
    free_client_list();
    exit(0);
}

int main(void)
{
    int server_socket, *client_socket_ptr;
    struct sockaddr_in server_address;
    int opt = 1;

    // Configurar o handler para sinal de interrupção (Ctrl+C)
    signal(SIGINT, signal_handler);

    // Configurar o endereço do servidor
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(SERVER_PORT);

    // Criar o socket do servidor
    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket < 0)
    {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    // Permitir reuso do endereço e porta
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Associar o socket ao endereço e porta
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Erro no bind()");
        exit(EXIT_FAILURE);
    }

    // Colocar o socket em modo de escuta
    if (listen(server_socket, QUEUE_SIZE) < 0)
    {
        perror("Erro no listen()");
        exit(EXIT_FAILURE);
    }

    printf("Servidor esperando por conexões na porta %d...\n", SERVER_PORT);

    while (1)
    {
        client_socket_ptr = (int *)malloc(sizeof(int));
        *client_socket_ptr = accept(server_socket, NULL, NULL);
        if (*client_socket_ptr < 0)
        {
            perror("Erro no accept()");
            free(client_socket_ptr);
            continue;
        }

        // Criar uma nova thread para lidar com a requisição do cliente
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_request, client_socket_ptr);
        pthread_detach(thread_id);
    }

    return 0;
}
