#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <crypt.h>
#include <fcntl.h>

#define PASSWORD_SIZE 128
#define SALT_LENGTH 16

static int read_random_bytes(unsigned char *buffer, size_t length)
{
    int fd = open("/dev/urandom", O_RDONLY);

    if (fd == -1) {
        perror("open /dev/urandom");
        return -1;
    }

    size_t total = 0;

    while (total < length) {

        ssize_t n = read(fd, buffer + total, length - total);

        if (n <= 0) {
            perror("read /dev/urandom");
            close(fd);
            return -1;
        }

        total += (size_t)n;
    }

    close(fd);
    return 0;
}

static int read_password(char *password, size_t size)
{
    struct termios old_settings;
    struct termios new_settings;

    if (tcgetattr(STDIN_FILENO, &old_settings) == -1) {
        perror("tcgetattr");
        return -1;
    }

    new_settings = old_settings;

    /*
     * Disable terminal echo so the password is not displayed.
     */
    new_settings.c_lflag &= ~(ECHO);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_settings) == -1) {
        perror("tcsetattr");
        return -1;
    }

    printf("Enter password: ");
    fflush(stdout);

    if (fgets(password, size, stdin) == NULL) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_settings);
        return -1;
    }

    /*
     * Restore normal terminal behaviour.
     */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_settings);

    printf("\n");

    password[strcspn(password, "\n")] = '\0';

    if (password[0] == '\0')
        return -1;

    return 0;
}

int main(void)
{
    char password[PASSWORD_SIZE];

    unsigned char random_bytes[SALT_LENGTH];

    const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789./";

    char salt[SALT_LENGTH + 4];

    if (read_password(password, sizeof(password)) == -1) {
        fprintf(stderr, "Could not read password.\n");
        return EXIT_FAILURE;
    }

    if (read_random_bytes(random_bytes,
                          sizeof(random_bytes)) == -1) {
        return EXIT_FAILURE;
    }

    /*
     * "$6$" selects SHA-512 crypt.
     */
    strcpy(salt, "$6$");

    for (int i = 0; i < SALT_LENGTH; i++) {
        salt[i + 3] =
            charset[random_bytes[i] %
                    (sizeof(charset) - 1)];
    }

    salt[SALT_LENGTH + 3] = '\0';

    char *hash = crypt(password, salt);

    if (hash == NULL) {
        perror("crypt");
        return EXIT_FAILURE;
    }

    printf("Password hash:\n%s\n", hash);

    /*
     * Clear plaintext password from our local buffer.
     */
    memset(password, 0, sizeof(password));

    return EXIT_SUCCESS;
}
