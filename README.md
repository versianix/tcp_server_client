# Implementação de Protocolo TCP utilizando um cliente e um servidor

## Para compilar o programa, execute no terminal:

g++ -pthread -o server server.cpp
g++ -o client client.cpp

Para verificar seu funcionamento, abra dois terminais diferentes e execute ./client e ./server.
 
## Os comandos implementados para o cliente foram:

### MyGet endereço path-do-arquivo (substitua o endereço por localhost para testar em seu computador)
Esta função envia um arquivo do servidor (diretório do processo) para o cliente, imprimindo os resultados no terminal.

### MyLastAccess
Esta função recupera o timestamp da última requisição feita pelo cliente.
