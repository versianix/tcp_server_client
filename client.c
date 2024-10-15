#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 4096
#define TIMEOUT_SECONDS 5  // Timeout de 5 segundos

int main(void)
{
    int socket_fd, connection_status, bytes_received;
    char buffer[BUFFER_SIZE];
    struct hostent *server_info;
    struct sockaddr_in server_address;
    char command[BUFFER_SIZE], filename[BUFFER_SIZE];
    struct timeval timeout;
    fd_set socket_set;
    int socket_flags, select_result;

    while (1)
    {
        // Solicitar o comando ao usuário
        printf("Digite o comando no formato 'MyGet localhost arquivo' ou 'MyLastAccess': ");
        fgets(command, sizeof(command), stdin);

        // Remover a quebra de linha do comando
        command[strcspn(command, "\n")] = 0;

        // Dividir o comando em partes
        char cmd[20], address[50] = "";
        int parsed_arguments = sscanf(command, "%s %s %s", cmd, address, filename);

        // Validar o comando
        if (strcmp(cmd, "MyGet") != 0 && strcmp(cmd, "MyLastAccess") != 0)
        {
            printf("Comando inválido!\n");
            continue;
        }

        // Validar se MyGet tem os argumentos corretos
        if (strcmp(cmd, "MyGet") == 0 && parsed_arguments < 3)
        {
            printf("Nome do arquivo ou 'localhost' ausentes!\n");
            continue;
        }

        // Verificar se MyLastAccess tem argumentos extras
        if (strcmp(cmd, "MyLastAccess") == 0 && parsed_arguments > 1)
        {
            printf("Comando 'MyLastAccess' não requer parâmetros adicionais!\n");
            continue;
        }

        // Para 'MyLastAccess', usar 'localhost' como endereço
        if (strcmp(cmd, "MyLastAccess") == 0)
        {
            strcpy(address, "localhost");
        }

        // Obter informações do servidor
        server_info = gethostbyname(address);
        if (!server_info)
        {
            printf("[gethostbyname] Falhou ao localizar o endereço.\n");
            continue;
        }

        // Criar o socket
        socket_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_fd < 0)
        {
            printf("Falha ao criar o socket.\n");
            continue;
        }

        // Configurar o endereço do servidor
        memset(&server_address, 0, sizeof(server_address));
        server_address.sin_family = AF_INET;
        memcpy(&server_address.sin_addr.s_addr, server_info->h_addr, server_info->h_length);
        server_address.sin_port = htons(SERVER_PORT);

        // Colocar o socket em modo não bloqueante
        socket_flags = fcntl(socket_fd, F_GETFL, 0);
        fcntl(socket_fd, F_SETFL, socket_flags | O_NONBLOCK);

        // Tentar conectar ao servidor
        connection_status = connect(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address));
        if (connection_status < 0)
        {
            if (errno != EINPROGRESS)
            {
                perror("Erro ao tentar conectar");
                close(socket_fd);
                continue;
            }
        }

        // Usar select() para aguardar a conexão ou timeout
        FD_ZERO(&socket_set);
        FD_SET(socket_fd, &socket_set);

        timeout.tv_sec = TIMEOUT_SECONDS;
        timeout.tv_usec = 0;

        select_result = select(socket_fd + 1, NULL, &socket_set, NULL, &timeout);
        if (select_result == 1)  // Socket pronto para conexão
        {
            int socket_error;
            socklen_t len = sizeof(socket_error);

            getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &len);

            if (socket_error == 0)
            {
                // Conexão estabelecida
                fcntl(socket_fd, F_SETFL, socket_flags);  // Voltar ao modo bloqueante
                printf("Conexão com o servidor estabelecida com sucesso.\n");
            }
            else
            {
                printf("Erro durante a conexão: %s\n", strerror(socket_error));
                close(socket_fd);
                continue;
            }
        }
        else if (select_result == 0)  // Timeout
        {
            printf("Timeout ao tentar se conectar ao servidor.\n");
            close(socket_fd);
            continue;
        }
        else  // Erro
        {
            perror("Erro no select()");
            close(socket_fd);
            continue;
        }

        // Enviar o comando ao servidor
        write(socket_fd, command, strlen(command) + 1);

        // Receber e exibir a resposta do servidor
        while ((bytes_received = read(socket_fd, buffer, BUFFER_SIZE)) > 0)
        {
            write(1, buffer, bytes_received);  // Exibir a resposta no terminal
        }

        close(socket_fd);
        printf("\n");
    }

    return 0;
}
