/* Host test tripwire only: the actual Retail/CascLib implementation stays linked.
 * Abort on its public socket/DNS interfaces rather than substituting asset data.
 */
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

static void reject_network(void) {
    static const char message[] = "FAIL: Retail asset fixture attempted socket/DNS access\n";
    (void)write(2, message, sizeof(message) - 1);
    _exit(97);
}
int socket(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol; reject_network(); return -1;
}
int connect(int descriptor, const struct sockaddr* address, socklen_t length) {
    (void)descriptor; (void)address; (void)length; reject_network(); return -1;
}
int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** result) {
    (void)node; (void)service; (void)hints; (void)result; reject_network(); return -1;
}
